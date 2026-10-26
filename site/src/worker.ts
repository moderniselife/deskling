// deskling.mojolayers.com
//   /            landing page (static, from public/)
//   /install     browser installer (static)
//   /firmware/*  update server: manifest.json and .bin builds, served from the firmware-dist branch
//                through Cloudflare's cache. Screens check /firmware/manifest.json for updates.
interface Env {
  ASSETS: Fetcher;
  FIRMWARE_ORIGIN: string;
}

const FILE = /^[A-Za-z0-9._-]{1,96}$/;

async function firmware(req: Request, url: URL, env: Env): Promise<Response> {
  if (req.method !== "GET" && req.method !== "HEAD") return new Response("method not allowed\n", { status: 405 });
  const name = url.pathname.slice("/firmware/".length) || "manifest.json";
  if (!FILE.test(name) || !(name === "manifest.json" || name.endsWith(".bin"))) return new Response("not found\n", { status: 404 });

  // Builds have the version in their name, so they never change: cache them hard. The manifest is what moves.
  const isManifest = name === "manifest.json";
  const upstream = await fetch(env.FIRMWARE_ORIGIN + name, {
    cf: { cacheEverything: true, cacheTtl: isManifest ? 60 : 31536000 },
  });
  if (!upstream.ok) return new Response(upstream.status === 404 ? "not found\n" : "upstream error\n", { status: upstream.status === 404 ? 404 : 502 });

  const headers = new Headers();
  headers.set("content-type", isManifest ? "application/json" : "application/octet-stream");
  const len = upstream.headers.get("content-length");
  if (len) headers.set("content-length", len);
  headers.set("cache-control", isManifest ? "public, max-age=60" : "public, max-age=31536000, immutable");
  headers.set("access-control-allow-origin", "*");
  return new Response(req.method === "HEAD" ? null : upstream.body, { status: 200, headers });
}

export default {
  async fetch(req: Request, env: Env): Promise<Response> {
    const url = new URL(req.url);
    if (url.pathname === "/firmware" || url.pathname.startsWith("/firmware/")) return firmware(req, url, env);
    return env.ASSETS.fetch(req);
  },
} satisfies ExportedHandler<Env>;
