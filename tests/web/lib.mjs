// Shared bits for the web tests: Playwright, a tiny static/mock server, and check().
import { createRequire } from "node:module";
import http from "node:http";
import { readFileSync, existsSync, statSync } from "node:fs";
import { extname, join, normalize } from "node:path";

const require = createRequire(import.meta.url);
export const { chromium } = require("playwright");

export let failures = 0;
export function check(name, ok, extra = "") {
  console.log(`${ok ? "PASS" : "FAIL"}  ${name}${ok || !extra ? "" : "  " + extra}`);
  if (!ok) failures++;
}
export function finish() {
  console.log(failures ? `\n${failures} failed` : "\nall passed");
  process.exit(failures ? 1 : 0);
}

const TYPES = { ".html": "text/html", ".css": "text/css", ".js": "text/javascript", ".png": "image/png", ".webp": "image/webp", ".wav": "audio/wav", ".svg": "image/svg+xml" };

/** Serve a folder, with an optional handler that gets first go at each request. */
export function serve(root, handler) {
  const server = http.createServer(async (req, res) => {
    if (handler && (await handler(req, res))) return;
    let p = normalize(decodeURIComponent(new URL(req.url, "http://x").pathname)).replace(/^(\.\.[/\\])+/, "");
    let f = join(root, p);
    if (existsSync(f) && statSync(f).isDirectory()) f = join(f, "index.html");
    if (!existsSync(f)) {
      res.writeHead(404);
      return res.end("not found");
    }
    res.writeHead(200, { "content-type": TYPES[extname(f)] ?? "application/octet-stream" });
    res.end(readFileSync(f));
  });
  return new Promise((r) => server.listen(0, "127.0.0.1", () => r({ server, url: `http://127.0.0.1:${server.address().port}` })));
}
