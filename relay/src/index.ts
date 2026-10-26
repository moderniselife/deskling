import { Hub } from "./hub";
import { SIM_HTML } from "./sim";
import { FLASH_HTML } from "./pages/flash";
import { SETUP_HTML } from "./pages/setup";

export { Hub };

export interface Env {
  HUB: DurableObjectNamespace<Hub>;
  DESKLING_TOKEN?: string;
  FIRMWARE_URL?: string;
}

const DEFAULT_FIRMWARE_URL = "https://raw.githubusercontent.com/moderniselife/deskling/firmware-dist/";
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
    // The simulator page holds no secrets: it asks for the token and keeps it in the browser.
    // These pages hold no secrets either: they ask for the token and keep it in the browser tab.
    if (req.method === "GET") {
      if (url.pathname === "/sim") return html(SIM_HTML);
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
