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

// Replies in Markdown, "Show all" for a long one, and earlier steps that open.
const long = "## Plan\n\n**Rate limit** the `login` route:\n\n1. Count per IP\n2. Answer *429*\n   - with `Retry-After`\n\n| Limit | Window |\n| --- | --- |\n| 5 | 1 min |\n\n```\nnpm test\n```\n\n<img src=x onerror=\"window.pwned=1\"> [docs](javascript:alert(1)) and [site](https://example.com)\n\n" + "More words to make it long. ".repeat(60) + "\n\nThe very end.";
await post("/agent/update", { id: "oc-md", source: "opencode", name: "md-demo", host: "MacBook", os: "mac", state: "done", task: "Done",
  lines: [...Array.from({ length: 6 }, (_, i) => ({ key: "s" + i, from: "tool", text: "Reading file" + i + ".ts" })), { key: "r", from: "agent", text: long }] });
await page.getByText("md-demo", { exact: true }).first().click();
await page.locator(".b.agent h4").first().waitFor({ timeout: 5000 });
check("a reply's Markdown is drawn: heading, bold, code, lists, table, code block", (await page.locator(".b.agent strong").count()) >= 1 && (await page.locator(".b.agent ol > li").count()) === 2 && (await page.locator(".b.agent ul li").count()) === 1 && (await page.locator(".b.agent table td").count()) === 2 && (await page.locator(".b.agent pre code").count()) === 1);
check("no raw Markdown marks show", !(await page.locator(".b.agent").first().innerText()).includes("**"));
check("HTML in a reply stays text, and only web links are links", (await page.evaluate(() => !window.pwned && !document.querySelector(".b.agent img"))) && (await page.locator('.b.agent a[href^="javascript"]').count()) === 0 && (await page.locator('.b.agent a[href="https://example.com"]').count()) === 1);
check("a long reply is cut, with Show all", !(await page.getByText("The very end.").isVisible()) && (await page.getByRole("button", { name: "Show all" }).isVisible()));
await page.getByRole("button", { name: "Show all" }).click();
await page.getByText("The very end.").waitFor({ timeout: 5000 });
check("Show all shows the whole reply", await page.getByRole("button", { name: "Show less" }).isVisible());
await page.getByRole("button", { name: "3 earlier steps" }).click();
await page.locator(".stepsopen .step.tool").first().waitFor({ timeout: 5000 });
check("earlier steps open in place", (await page.locator(".stepsopen .step.tool").count()) === 3 && (await page.getByText("Reading file0.ts").isVisible()));
if (SHOTS) await page.screenshot({ path: `${SHOTS}/app-markdown.png` });
await page.getByRole("button", { name: "3 earlier steps" }).click();
check("and fold away again", (await page.locator(".stepsopen").count()) === 0);
await post("/agent/update", { id: "oc-md", end: true });
await page.getByText("api-server", { exact: true }).first().click();
await page.getByText("I'll allow 5 attempts").waitFor({ timeout: 5000 });

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

// Wait for my reply: armed while Claude works, then a clear "waiting for your reply" when it finishes.
const ccHook = (body, q = "") => fetch(`${URL_BASE}/hook${q}`, { method: "POST", headers: { ...auth, "x-deskling-host": "MacBook", "x-deskling-os": "mac" }, body: JSON.stringify({ session_id: "cc-wait", cwd: "/Users/joe/code/shop", ...body }) });
await ccHook({ hook_event_name: "SessionStart", source: "startup" });
await ccHook({ hook_event_name: "UserPromptSubmit", prompt: "Tidy the checkout page" });
await page.getByText("shop", { exact: true }).first().click();
await page.getByRole("button", { name: "Wait for my reply" }).click();
await page.getByRole("button", { name: /^On for (29|30) min · stop$/ }).waitFor({ timeout: 5000 });
check("Wait for my reply turns on, with the minutes left", true);
const hold = ccHook({ hook_event_name: "Stop", last_assistant_message: "Tidied. **Ship it?**" }, "?hold=1&round=0");
await page.locator(".waitcard").getByText("Claude is waiting for your reply").waitFor({ timeout: 5000 });
check("when Claude finishes, the chat says it's waiting for your reply", await page.getByRole("button", { name: "Stop waiting" }).isVisible() && await page.getByText("Your turn · Waiting for your reply").first().isVisible());
if (SHOTS) await page.screenshot({ path: `${SHOTS}/app-waiting.png` });
await page.getByRole("button", { name: "Stop waiting" }).click();
const let_go = await hold;
check("Stop waiting lets Claude finish", let_go.status === 204 || (await let_go.text()) === "");
await page.getByRole("button", { name: "Wait for my reply" }).waitFor({ timeout: 5000 });
check("and it shows as done", await page.getByText("Done · Finished").first().isVisible());
await ccHook({ hook_event_name: "SessionEnd", reason: "other" });
await page.getByText("api-server", { exact: true }).first().click();

// Quests: connect a (fake) Mojo Desk in the page, then play.
{
  const { createServer } = await import("node:http");
  const issues = [
    { key: "WEB-1", title: "Ship the pricing page", status: "todo", priority: "urgent", estDevMin: 120, project: "web" },
    { key: "WEB-2", title: "Fix the footer links", status: "progress", priority: "medium", estDevMin: 30, project: "web" },
  ];
  const fake = createServer((req, res) => {
    let b = "";
    req.on("data", (c) => (b += c));
    req.on("end", () => {
      const send = (code, d) => { res.writeHead(code, { "content-type": "application/json" }); res.end(JSON.stringify(d)); };
      if (req.headers.authorization !== "Bearer fake-desk-key") return send(401, {});
      if (req.url.startsWith("/api/v1/me/queue")) return send(200, { progress: issues.filter((i) => i.status === "progress"), todo: issues.filter((i) => i.status === "todo"), waiting: [], done: issues.filter((i) => i.status === "done") });
      if (req.url === "/api/v1/me") return send(200, { name: "Joe" });
      const m = /^\/api\/v1\/issues\/([^/]+)$/.exec(req.url);
      if (m && req.method === "PATCH") { const i = issues.find((x) => x.key === m[1]); Object.assign(i, JSON.parse(b)); return send(200, i); }
      send(404, {});
    });
  });
  await new Promise((r) => fake.listen(0, "127.0.0.1", r));
  const deskUrl = `http://127.0.0.1:${fake.address().port}`;
  await page.getByRole("button", { name: /^Quests/ }).first().click();
  await page.getByText("Turn your tickets into quests").waitFor({ timeout: 5000 });
  check("Quests says how to start when nothing is connected", true);
  await page.getByRole("button", { name: "Connect a service" }).click();
  await page.locator("#q-url").fill(deskUrl);
  await page.locator("#q-token").fill("wrong-key");
  await page.locator("#qsave").click();
  await page.locator("#qerr:not(.hide)").waitFor({ timeout: 8000 });
  check("a wrong key says so in the dialog", /didn't accept the key/.test(await page.locator("#qerr").innerText()));
  await page.locator("#q-token").fill("fake-desk-key");
  await page.locator("#qsave").click();
  await page.getByText("Disconnect").waitFor({ timeout: 8000 });
  check("the key is accepted and shows who it belongs to", await page.locator("#qconns").innerText().then((t) => t.includes("Joe")));
  check("the key isn't left in the form", (await page.locator("#q-token").inputValue()) === "");
  await page.getByRole("button", { name: "Close" }).click();
  await page.getByText("Ship the pricing page").waitFor({ timeout: 8000 });
  check("tickets show as today's quests, the urgent one as a boss", (await page.locator(".quest.boss").count()) === 1 && (await page.getByText("Fix the footer links").isVisible()));
  if (SHOTS) await page.screenshot({ path: `${SHOTS}/app-quests.png` });
  await page.getByRole("button", { name: "Finish WEB-1", exact: true }).click();
  await page.getByRole("button", { name: "Tap again to finish WEB-1", exact: true }).click();
  await page.locator(".cele .pop.boss").waitFor({ timeout: 8000 });
  check("finishing a boss celebrates", await page.getByText("Boss defeated!").isVisible());
  if (SHOTS) await page.screenshot({ path: `${SHOTS}/app-boss.png` });
  check("and moved the ticket in Mojo Desk", issues[0].status === "done");
  await page.locator(".cele").click();
  await page.locator(".cele").waitFor({ state: "detached", timeout: 8000 }).catch(() => {});
  await page.getByText("Fix the footer links").click();
  await page.getByRole("button", { name: "Focus 25 min" }).click();
  await page.locator(".focusbar").waitFor({ timeout: 5000 });
  check("a focus block shows its countdown", /^2[45]:\d\d$/.test(await page.locator("#ftime").innerText()));
  await page.locator(".focusbar").getByRole("button", { name: "Stop" }).click();
  await page.locator(".focusbar").waitFor({ state: "detached", timeout: 5000 });
  check("and stops", true);
  await fetch(URL_BASE + "/api/integrations?kind=mojo", { method: "DELETE", headers: auth });
  fake.close();
  await page.getByText("api-server", { exact: true }).first().click();
  await page.getByText("I'll allow 5 attempts").waitFor({ timeout: 5000 });
}

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

// Notifications: the page asks, registers its service worker, and a link to a chat opens it.
const ctx = await browser.newContext();
await ctx.grantPermissions(["notifications"], { origin: URL_BASE });
const np = await ctx.newPage();
await np.addInitScript((t) => localStorage.setItem("deskling-token", t), TOKEN);
// Headless Chromium always says "denied": stand in for a browser where you said yes.
await np.addInitScript(() => {
  let perm = "default";
  window.Notification = class {
    static get permission() { return perm; }
    static requestPermission() { perm = "granted"; return Promise.resolve(perm); }
  };
});
await np.goto(URL_BASE + "/app#oc-app");
await np.getByText("I'll allow 5 attempts").waitFor({ timeout: 8000 }).catch(() => {});
check("a link to a session opens its chat (how a tapped notification lands)", await np.getByText("I'll allow 5 attempts").isVisible());
await np.getByRole("button", { name: "Notify me here" }).click();
await np.getByRole("button", { name: "Notifying you here" }).waitFor({ timeout: 5000 }).catch(() => {});
check("notifications can be turned on", await np.getByRole("button", { name: "Notifying you here" }).isVisible());
await np.waitForTimeout(1500);
check("its service worker is registered", await np.evaluate(() => navigator.serviceWorker.getRegistration("/app-sw.js").then((r) => !!r)));
await ctx.close();

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
