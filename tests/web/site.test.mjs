// The landing page: no sideways scroll on phones, no errors, the basics of accessibility, and the
// interactive bits (menu, screen tabs, Deskling's moods, the Motion switch) working.
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { check, chromium, finish, serve } from "./lib.mjs";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..", "..", "site", "public");
const { server, url } = await serve(ROOT);
const browser = await chromium.launch();

for (const [w, h] of [[360, 740], [390, 844], [1440, 900]]) {
  for (const scheme of ["light", "dark"]) {
    const page = await browser.newPage({ viewport: { width: w, height: h }, colorScheme: scheme });
    const errors = [];
    page.on("pageerror", (e) => errors.push(e.message));
    page.on("console", (m) => m.type() === "error" && !/fonts\.g|deploy\.workers|ERR_/.test(m.text()) && errors.push(m.text()));
    await page.goto(url + "/", { waitUntil: "load" });
    await page.waitForTimeout(600);
    const sw = await page.evaluate(() => document.documentElement.scrollWidth);
    check(`${w}px ${scheme}: nothing scrolls sideways`, sw <= w, `scrollWidth ${sw}`);
    check(`${w}px ${scheme}: no script errors`, errors.length === 0, errors.join(" | "));
    await page.close();
  }
}

const page = await browser.newPage({ viewport: { width: 390, height: 844 } });
await page.goto(url + "/", { waitUntil: "load" });
const a11y = await page.evaluate(() => ({
  lang: document.documentElement.lang,
  h1: document.querySelectorAll("h1").length,
  skip: !!document.querySelector('a.skip[href="#main"]'),
  noAlt: [...document.querySelectorAll("img")].filter((i) => !i.hasAttribute("alt")).length,
  unnamed: [...document.querySelectorAll("button, a[href]")].filter((b) => !(b.textContent.trim() || b.getAttribute("aria-label") || b.querySelector("img[alt]") || b.querySelector(".sr-only"))).map((b) => b.outerHTML.slice(0, 60)),
  switches: [...document.querySelectorAll('[role="switch"]')].every((s) => s.hasAttribute("aria-checked")),
}));
check("the page has a language, one h1 and a skip link", a11y.lang === "en" && a11y.h1 === 1 && a11y.skip);
check("every picture has alt text", a11y.noAlt === 0);
check("every button and link has a name", a11y.unnamed.length === 0, a11y.unnamed.join(", "));
check("every switch says whether it's on", a11y.switches);

await page.click(".menu-btn");
check("the phone menu opens", await page.isVisible("#site-menu"));
await page.keyboard.press("Escape");
check("Escape closes it and returns focus", !(await page.isVisible("#site-menu")) && (await page.evaluate(() => document.activeElement.classList.contains("menu-btn"))));

await page.click("#t-night");
check("a screen tab shows its screen", await page.evaluate(() => document.getElementById("p-night").classList.contains("on")));
await page.focus("#t-night");
await page.keyboard.press("ArrowRight");
check("arrow keys move between tabs (and wrap)", (await page.evaluate(() => document.activeElement.id)) === "t-permission");

await page.click('.chip[data-ev="permission"]');
check("a moment changes Deskling's face and shows its banner", (await page.getAttribute("#device", "class")) === "m-worried b-on");

await page.click("[data-motion-toggle]");
check("the Motion switch turns motion off", (await page.evaluate(() => document.documentElement.classList.contains("calm"))) && (await page.getAttribute("[data-motion-toggle]", "aria-checked")) === "false");
await page.reload();
check("and it's remembered", await page.evaluate(() => document.documentElement.classList.contains("calm")));

const reduced = await browser.newPage({ reducedMotion: "reduce" });
await reduced.goto(url + "/");
check("motion defaults to off when the system asks for less motion", await reduced.evaluate(() => document.documentElement.classList.contains("calm")));

const nojs = await browser.newPage({ javaScriptEnabled: false });
await nojs.goto(url + "/");
check("without JavaScript every screen is still there", (await nojs.locator(".panel").evaluateAll((els) => els.filter((e) => e.offsetHeight > 0).length)) === 6);

await browser.close();
server.close();
finish();
