// Deskling in a browser (the relay's /app page) against a running relay:
//   DESKLING_URL=http://127.0.0.1:8787 DESKLING_TOKEN=dev-token node tests/web/app.test.mjs
// A bridged session with a conversation, a permission answered in the page, a message sent from
// the page, a new OpenCode session started from the page, and the phone layout.
import { check, chromium, finish } from "./lib.mjs";

const URL_BASE = process.env.DESKLING_URL ?? "http://127.0.0.1:8787";
const TOKEN = process.env.DESKLING_TOKEN ?? "dev-token";
const SHOTS = process.env.DESKLING_SHOTS || "";
const auth = { authorization: `Bearer ${TOKEN}`, "content-type": "application/json" };
const post = (path, body) => fetch(URL_BASE + path, { method: "POST", headers: auth, body: JSON.stringify(body) });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const base = { id: "oc-app", source: "opencode", name: "api-server", host: "MacBook", os: "mac" };
await post("/agent/update", {
  ...base,
  state: "working",
  task: "Running the tests",
  model: "ollama/qwen3-coder:30b",
  context: 18400,
  helpers: [{ id: "h1", name: "Plan the change", task: "", done: true }],
  lines: [
    { key: "a1", from: "you", text: "Add rate limiting to the login endpoint" },
    { key: "a2", from: "thought", text: "The login route is in src/routes/auth.ts." },
    { key: "a3", from: "tool", text: "Editing auth.ts" },
    { key: "a4", from: "agent", text: "I'll allow 5 attempts a minute per IP and answer 429 after that." },
  ],
});
// A bridge that's polling can be messaged.
await fetch(`${URL_BASE}/agent/poll?ids=oc-app&wait=0`, { headers: auth });
await post("/agent/update", { ...base, task: "Running the tests" });
await post("/runner/register", {
  machine: "MacBook",
  os: "mac",
  projects: [{ name: "api-server", path: "/Users/joe/code/api-server" }, { name: "web-app", path: "/Users/joe/code/web-app" }],
  engines: [
    { id: "claude", name: "Claude Code", models: [] },
    { id: "opencode", name: "OpenCode", models: [{ id: "ollama/qwen3-coder:30b", name: "Qwen3 Coder 30B" }, { id: "opencode/big-pickle", name: "Big Pickle" }] },
  ],
});

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1280, height: 860 } });
const errors = [];
page.on("pageerror", (e) => errors.push(String(e)));
await page.addInitScript((t) => localStorage.setItem("deskling-token", t), TOKEN);
await page.goto(URL_BASE + "/app");
await page.getByText("api-server", { exact: true }).first().waitFor({ timeout: 10000 });
check("sessions show up", await page.getByText("MacBook · Running the tests").isVisible());

await page.getByText("api-server", { exact: true }).first().click();
await page.getByText("I'll allow 5 attempts").waitFor({ timeout: 5000 });
check("the conversation shows: your prompt, thinking, steps and replies", (await page.locator(".b.you").count()) === 1 && (await page.locator(".step.thought").count()) === 1 && (await page.locator(".step.tool").count()) === 1);
check("model, context and helpers show", await page.getByText("ollama/qwen3-coder:30b · 18k tokens in context").isVisible() && await page.getByText("Plan the change · done").isVisible());
if (SHOTS) await page.screenshot({ path: `${SHOTS}/app-chat.png` });

// A permission, answered in the page.
const ask = post("/agent/ask", { id: "oc-app", key: "k1", kind: "permission", tool: "Bash", summary: "npm test", detail: "Run the tests", wait: 20 }).then((r) => r.json());
await page.getByText("OpenCode wants to run a command").first().waitFor({ timeout: 5000 });
if (SHOTS) await page.screenshot({ path: `${SHOTS}/app-permission.png` });
await page.getByRole("button", { name: "Allow once" }).first().click();
check("a permission is answered from the page", (await ask).decision === "allow");

// A message, typed in the page.
const poll = fetch(`${URL_BASE}/agent/poll?ids=oc-app&wait=15`, { headers: auth }).then((r) => r.json());
await sleep(300);
await page.getByLabel("Message").fill("also add a test for it");
await page.getByLabel("Message").press("Enter");
const got = await poll;
check("a message typed in the page reaches the session", got.messages?.[0]?.text === "also add a test for it", JSON.stringify(got));

// A new OpenCode session with a local model.
const job = fetch(`${URL_BASE}/runner/poll?machine=MacBook`, { headers: auth }).then((r) => r.json());
await page.getByRole("button", { name: "New session" }).click();
await page.locator("#n-where").getByRole("button", { name: "MacBook" }).click();
await page.locator("#n-engine").getByRole("button", { name: "OpenCode" }).click();
await page.locator("#n-model").getByRole("button", { name: "Big Pickle" }).click();
await page.locator("#n-project").getByRole("button", { name: "web-app" }).click();
check("the new session sheet asks the right agent", await page.getByText("What should OpenCode do?").isVisible());
if (SHOTS) await page.screenshot({ path: `${SHOTS}/app-new.png` });
await page.locator("#n-prompt").fill("Tidy the CSS");
await page.getByRole("button", { name: "Start" }).click();
const j = (await job).job;
check("it starts on that computer with that agent, model and project", j?.engine === "opencode" && j.model === "opencode/big-pickle" && j.project === "/Users/joe/code/web-app" && j.prompt === "Tidy the CSS", JSON.stringify(j));

// Phone: the list first, then the chat with a way back.
const phone = await browser.newPage({ viewport: { width: 390, height: 844 } });
await phone.addInitScript((t) => localStorage.setItem("deskling-token", t), TOKEN);
await phone.goto(URL_BASE + "/app");
await phone.getByText("api-server", { exact: true }).first().waitFor({ timeout: 10000 });
check("on a phone it fits the width", (await phone.evaluate(() => document.documentElement.scrollWidth)) <= 390);
await phone.getByText("api-server", { exact: true }).first().click();
await phone.getByText("I'll allow 5 attempts").waitFor({ timeout: 5000 });
if (SHOTS) await phone.screenshot({ path: `${SHOTS}/app-phone.png` });
await phone.getByRole("button", { name: "Sessions" }).click();
check("on a phone, Sessions goes back to the list", await phone.getByText("MacBook · Running the tests").isVisible());

// A wrong token asks again.
const wrong = await browser.newPage();
await wrong.addInitScript(() => localStorage.setItem("deskling-token", "nope"));
await wrong.goto(URL_BASE + "/app");
await wrong.getByText("That token didn't work").waitFor({ timeout: 8000 }).catch(() => {});
check("a wrong token asks for it again", await wrong.getByText("That token didn't work").isVisible());

check("no errors in the page", !errors.length, errors.join("; "));
await post("/agent/update", { id: "oc-app", end: true });
await browser.close();
finish();
