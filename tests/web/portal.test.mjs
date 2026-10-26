// The board's own page (firmware/main/net/portal.html) against a fake board: the 4-digit code,
// what happens when the board restarts with a new code, and that it doesn't hammer the board.
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { check, chromium, finish, serve } from "./lib.mjs";

const NET = join(dirname(fileURLToPath(import.meta.url)), "..", "..", "firmware", "main", "net");
let code = "4321";
let hits = 0;
const status = { wifi: { ssid: "Home", ip: "192.168.1.121", hotspot: "Deskling-7F3A", local_name: { name: "deskling.local", heard: 3, example: "" } }, relay: { url: "https://relay.example", configured: true, connected: true, error: "", token_end: "1844" }, display: { frames: 70, avg_ms: 9.8, worst_ms: 44, busy_pct: 12, draw_ms: 6, rotate_ms: 2, copy_wait_ms: 1, kpx: 240, bands: "internal", band_lines: 40 }, images: { requested: 3, done: 3, failed: 0, last: "x: shown at 520x325" }, last_restart: "", firmware: { version: "v0.3.2-test18", variant: "p4r1-lcd1" } };
const { server, url } = await serve(NET, (req, res) => {
  const path = new URL(req.url, "http://x").pathname;
  if (path === "/") {
    res.writeHead(200, { "content-type": "text/html" });
    res.end(require_html());
    return true;
  }
  if (!path.startsWith("/api/")) return false;
  hits++;
  if (req.headers["x-deskling-code"] !== code) {
    res.writeHead(401, { "content-type": "application/json" });
    res.end('{"message":"That code doesn\'t match the screen."}');
    return true;
  }
  res.writeHead(200, { "content-type": "application/json" });
  res.end(path === "/api/status" ? JSON.stringify(status) : "{}");
  return true;
});
import { readFileSync } from "node:fs";
function require_html() {
  return readFileSync(join(NET, "portal.html"), "utf8");
}

const browser = await chromium.launch();
const page = await browser.newPage();
const errors = [];
page.on("pageerror", (e) => errors.push(e.message));
await page.goto(url + "/");
check("asks for the code first", await page.isVisible("#gate"));
await page.fill("#code", "1111");
await page.click("#go");
await page.waitForTimeout(300);
check("a wrong code says so", (await page.textContent("#gatemsg")).includes("doesn"));
await page.fill("#code", code);
await page.click("#go");
await page.waitForTimeout(800);
check("the right code opens it", !(await page.isVisible("#gate")));
check("it shows the board's status", (await page.textContent("body")).includes("deskling.local"));
check("it shows how chat pictures are doing", (await page.textContent("#pics")).includes("3 shown"));

// The board restarts and shows a new code: the page stops after one try and asks for the new one.
code = "9876";
hits = 0;
await page.waitForTimeout(9000);
check("after a restart it stops asking with the old code", hits <= 1, `${hits} requests`);
check("and says the board has a new code", (await page.textContent("#gatemsg")).includes("new code"));
await page.reload();
await page.waitForTimeout(500);
check("a reload doesn't retry the old code", hits <= 1);
check("no script errors", errors.length === 0, errors.join(" | "));

status.last_restart = "Last restart: crashed in images: LoadProhibited (pc 0x4801902e, ra 0x48019100)";
await page.fill("#code", code);
await page.click("#go");
await page.waitForTimeout(800);
check("a crash shows on the page", (await page.isVisible("#crash")) && (await page.textContent("#crash")).includes("crashed in images"));

await browser.close();
server.close();
finish();
