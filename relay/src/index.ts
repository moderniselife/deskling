import { Hub } from "./hub";
import APP_HTML from "./pages/app.html";
import { FLASH_HTML } from "./pages/flash";
import { SETUP_HTML } from "./pages/setup";

export { Hub };

export interface Env {
  HUB: DurableObjectNamespace<Hub>;
  DESKLING_TOKEN?: string;
  FIRMWARE_URL?: string;
}

const DEFAULT_FIRMWARE_URL = "https://deskling.mojolayers.com/firmware/";
/** Deskling's face: lilac head with ears, ink eyes, peach cheeks, on cream. */
const APP_ICON = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512"><rect width="512" height="512" rx="112" fill="#FBF6EF"/><circle cx="160" cy="150" r="58" fill="#CFC4F2"/><circle cx="352" cy="150" r="58" fill="#CFC4F2"/><circle cx="160" cy="150" r="30" fill="#BDB1EA"/><circle cx="352" cy="150" r="30" fill="#BDB1EA"/><rect x="76" y="140" width="360" height="290" rx="120" fill="#CFC4F2"/><rect x="118" y="182" width="276" height="206" rx="88" fill="#FFF8EF"/><rect x="186" y="244" width="30" height="48" rx="15" fill="#2A2433"/><rect x="296" y="244" width="30" height="48" rx="15" fill="#2A2433"/><ellipse cx="170" cy="318" rx="26" ry="16" fill="#F4B393"/><ellipse cx="342" cy="318" rx="26" ry="16" fill="#F4B393"/><path d="M232 326q24 22 48 0" fill="none" stroke="#2A2433" stroke-width="12" stroke-linecap="round"/></svg>`;

const html = (body: string) => new Response(body, { headers: { "content-type": "text/html; charset=utf-8", "cache-control": "no-store" } });

async function tokenMatches(given: string | null, expected: string | undefined): Promise<boolean> {
  if (!given || !expected) return false;
  const enc = new TextEncoder();
  const [a, b] = await Promise.all([
    crypto.subtle.digest("SHA-256", enc.encode(given)),
    crypto.subtle.digest("SHA-256", enc.encode(expected)),
  ]);
  return crypto.subtle.timingSafeEqual(a, b);
}

function bearer(req: Request, url: URL): string | null {
  const h = req.headers.get("authorization");
  if (h && h.toLowerCase().startsWith("bearer ")) return h.slice(7).trim();
  // Browsers and some WebSocket clients can't set headers, so the socket may pass ?token=.
  if (url.pathname === "/ws") return url.searchParams.get("token");
  return null;
}

export default {
  async fetch(req: Request, env: Env): Promise<Response> {
    const url = new URL(req.url);

    if (url.pathname === "/" && req.method === "GET") {
      return Response.redirect(new URL("/setup", url).toString(), 302);
    }
    // These pages hold no secrets: they ask for the token and keep it in the browser.
    // /app is Deskling in a browser (/sim is its old address).
    if (req.method === "GET") {
      if (url.pathname === "/app" || url.pathname === "/sim") return html(APP_HTML);
      // So /app can be added to a phone's home screen.
      if (url.pathname === "/app.webmanifest")
        return new Response(JSON.stringify({ name: "Deskling", short_name: "Deskling", start_url: "/app", display: "standalone", background_color: "#FBF6EF", theme_color: "#FBF6EF", icons: [{ src: "/app-icon.svg", sizes: "any", type: "image/svg+xml", purpose: "any" }] }), { headers: { "content-type": "application/manifest+json" } });
      if (url.pathname === "/app-icon.svg") return new Response(APP_ICON, { headers: { "content-type": "image/svg+xml", "cache-control": "public, max-age=86400" } });
      if (url.pathname === "/setup") return html(SETUP_HTML);
      if (url.pathname === "/flash") {
        let fw = env.FIRMWARE_URL || DEFAULT_FIRMWARE_URL;
        if (!fw.endsWith("/")) fw += "/";
        return html(FLASH_HTML.replace("__FIRMWARE_URL__", fw));
      }
    }

    if (!env.DESKLING_TOKEN) {
      return new Response("DESKLING_TOKEN is not set on this relay\n", { status: 500 });
    }
    if (!(await tokenMatches(bearer(req, url), env.DESKLING_TOKEN))) {
      return new Response("unauthorized\n", { status: 401 });
    }

    const hub = env.HUB.get(env.HUB.idFromName("default"));
    return hub.fetch(req);
  },
} satisfies ExportedHandler<Env>;
