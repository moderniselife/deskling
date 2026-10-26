// The Deskling plugin inside a real OpenCode, end to end: a mock model (no API keys, no network),
// a fake relay, and `opencode serve`. Skips when OpenCode isn't installed.
//   node hooks/opencode/test/opencode.test.mjs            (finds `opencode` on PATH)
//   OPENCODE_BIN=/path/to/opencode node hooks/opencode/test/opencode.test.mjs
import { spawn, execFileSync } from "node:child_process";
import { mkdtempSync, mkdirSync, writeFileSync, copyFileSync, rmSync, existsSync } from "node:fs";
import { createServer } from "node:http";
import { tmpdir } from "node:os";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { startMockModel } from "./mock-model.mjs";

const here = dirname(fileURLToPath(import.meta.url));
let failures = 0;
const check = (name, cond, extra = "") => {
  console.log(`${cond ? "PASS" : "FAIL"}  ${name}${extra ? "  " + extra : ""}`);
  if (!cond) failures++;
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

let BIN = process.env.OPENCODE_BIN || "opencode";
try {
  execFileSync(BIN, ["--version"], { stdio: "ignore" });
} catch {
  console.log("SKIP  OpenCode isn't installed (set OPENCODE_BIN)");
  process.exit(0);
}

// ---- a fake relay that records what the plugin sends
const relay = { updates: [], asks: [], outbox: [], resolved: [], events: [], polledIds: [], answer: { decision: "allow" }, registered: null, job: null, done: null };
const relayServer = createServer((req, res) => {
  let body = "";
  req.on("data", (d) => (body += d));
  req.on("end", async () => {
    const ok = req.headers.authorization === "Bearer test-token";
    const send = (o) => {
      res.writeHead(ok ? 200 : 401, { "content-type": "application/json" });
      res.end(JSON.stringify(o));
    };
    if (!ok) return send({});
    if (req.url === "/agent/update") {
      relay.updates.push(JSON.parse(body));
      return send({ ok: true });
    }
    if (req.url === "/agent/ask") {
      relay.asks.push(JSON.parse(body));
      return send(relay.answer);
    }
    if (req.url === "/api/event") {
      relay.events.push(JSON.parse(body));
      return send({ ok: true });
    }
    if (req.url === "/runner/register") {
      relay.registered = JSON.parse(body);
      return send({ ok: true });
    }
    if (req.url.startsWith("/runner/poll")) {
      const job = relay.job;
      relay.job = null;
      if (!job) await sleep(300);
      return send({ job });
    }
    if (req.url === "/runner/done") {
      relay.done = JSON.parse(body);
      return send({ ok: true });
    }
    if (req.url === "/agent/resolve") {
      relay.resolved.push(JSON.parse(body));
      return send({ ok: true });
    }
    if (req.url.startsWith("/agent/poll")) {
      const ids = new URL(req.url, "http://x").searchParams.get("ids").split(",");
      const mine = () => relay.outbox.filter((m) => ids.includes(m.id));
      for (let i = 0; i < 10 && !mine().length; i++) await sleep(100);
      const messages = mine();
      relay.outbox = relay.outbox.filter((m) => !messages.includes(m));
      relay.polledIds.push(...ids);
      return send({ messages });
    }
    send({});
  });
});
await new Promise((r) => relayServer.listen(0, "127.0.0.1", r));
const RELAY = `http://127.0.0.1:${relayServer.address().port}`;

// ---- OpenCode with its own config, data and project folders
const model = await startMockModel();
const T = mkdtempSync(join(tmpdir(), "deskling-oc-"));
const cfg = join(T, "config", "opencode");
mkdirSync(join(cfg, "plugins"), { recursive: true });
copyFileSync(join(here, "..", "deskling.js"), join(cfg, "plugins", "deskling.js"));
mkdirSync(join(cfg, "agents"), { recursive: true });
for (const f of ["deskling.md", "deskling-planner.md", "deskling-tester.md", "deskling-reviewer.md"]) copyFileSync(join(here, "..", "agents", f), join(cfg, "agents", f));
writeFileSync(
  join(cfg, "opencode.json"),
  JSON.stringify({
    model: "mock/mock-coder",
    small_model: "mock/mock-coder",
    autoupdate: false,
    share: "disabled",
    permission: { bash: "ask" },
    provider: { mock: { npm: "@ai-sdk/openai-compatible", name: "Mock", options: { baseURL: model.url, apiKey: "x" }, models: { "mock-coder": { name: "Mock coder", tool_call: true, reasoning: true } } } },
  }),
);
const proj = join(T, "web-app");
mkdirSync(proj);
mkdirSync(join(T, "home"));
const port = 30000 + Math.floor(Math.random() * 20000);
const env = {
  ...process.env,
  HOME: join(T, "home"),
  XDG_CONFIG_HOME: join(T, "config"),
  XDG_DATA_HOME: join(T, "data"),
  XDG_STATE_HOME: join(T, "state"),
  XDG_CACHE_HOME: join(T, "cache"),
  OPENCODE_DISABLE_AUTOUPDATE: "1",
  OPENCODE_DISABLE_LSP_DOWNLOAD: "1",
  OPENCODE_DISABLE_CLAUDE_CODE: "1",
  DESKLING_URL: RELAY,
  DESKLING_TOKEN: "test-token",
  DESKLING_NAME: "test-mac",
};
let log = "";
const oc = spawn(BIN, ["serve", "--port", String(port), "--hostname", "127.0.0.1"], { cwd: proj, env, stdio: ["ignore", "pipe", "pipe"], detached: true });
// `opencode` from npm is a launcher: stop the whole group.
const stop = (p) => {
  try {
    process.kill(-p.pid, "SIGTERM");
  } catch {
    p.kill();
  }
};
oc.stdout.on("data", (d) => (log += d));
oc.stderr.on("data", (d) => (log += d));
const OC = `http://127.0.0.1:${port}`;
let runner = null;
const api = async (method, path, body) => {
  const r = await fetch(OC + path, { signal: AbortSignal.timeout(20000), method, headers: { "content-type": "application/json", "x-opencode-directory": proj }, body: body ? JSON.stringify(body) : undefined });
  const t = await r.text();
  try {
    return JSON.parse(t);
  } catch {
    return t;
  }
};
const until = async (what, pred, ms = 30000) => {
  const t0 = Date.now();
  while (Date.now() - t0 < ms) {
    const v = await pred();
    if (v) return v;
    await sleep(200);
  }
  throw new Error("timed out waiting for " + what);
};
const lines = (sid) => {
  const out = new Map();
  for (const u of relay.updates) if (u.id === `oc-${sid}`) for (const l of u.lines || []) out.set(l.key, l);
  return [...out.values()];
};
const last = (sid, f) => [...relay.updates].reverse().find((u) => u.id === `oc-${sid}` && u[f] !== undefined)?.[f];

try {
  await until("OpenCode to start", async () => (await fetch(OC + "/session?directory=" + encodeURIComponent(proj), { signal: AbortSignal.timeout(5000) }).catch(() => null))?.ok);
  const session = await api("POST", `/session?directory=${encodeURIComponent(proj)}`, {});
  const sid = session.id;
  check("OpenCode makes a session", !!sid, JSON.stringify(session).slice(0, 200));
  await until("the session to reach the relay", () => relay.updates.some((u) => u.id === `oc-${sid}`), 15000);
  const first = relay.updates.find((u) => u.id === `oc-${sid}`);
  check("the session shows up on the relay, named after its folder", first.source === "opencode" && first.name === "web-app" && first.host === "test-mac");

  await api("POST", `/session/${sid}/prompt_async?directory=${encodeURIComponent(proj)}`, { parts: [{ type: "text", text: "please run echo hi" }] });
  await until("a permission ask", () => relay.asks.length > 0, 30000);
  check("its permission prompt is asked on the screen", relay.asks[0].kind === "permission" && relay.asks[0].tool === "Bash" && /echo hi/.test(relay.asks[0].summary), JSON.stringify(relay.asks[0]));
  await until("the reply after the tool", () => lines(sid).some((l) => l.from === "agent" && /shell works/.test(l.text)), 30000);
  const ls = lines(sid);
  check("your prompt is in the chat", ls.some((l) => l.from === "you" && l.text === "please run echo hi"));
  check("its thinking is in the chat", ls.some((l) => l.from === "thought" && /command run/.test(l.text)), JSON.stringify(ls.filter((l) => l.from === "thought")));
  check("the tool step is in the chat", ls.some((l) => l.from === "tool" && l.text === "Say hi"), JSON.stringify(ls.filter((l) => l.from === "tool")));
  check("allowing on the screen lets the command run", ls.some((l) => l.from === "agent" && /printed hi/.test(l.text)));
  await until("done", () => last(sid, "state") === "done", 15000);
  check("it's marked done when the turn ends", true);
  check("the model is reported", last(sid, "model") === "mock/mock-coder", String(last(sid, "model")));
  check("the context size is reported", last(sid, "context") > 0, String(last(sid, "context")));

  // A message typed on the screen starts the next turn straight away.
  relay.outbox.push({ id: `oc-${sid}`, text: "say hello" });
  await until("the reply to the screen message", () => lines(sid).some((l) => l.from === "agent" && /local model\. say hello/.test(l.text)), 30000);
  check("a message from the screen starts a turn", true);

  // A picture you attach reaches the screen.
  const PNG1 = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";
  await until("idle", async () => (await api("GET", `/session/status?directory=${encodeURIComponent(proj)}`))[sid] === undefined, 30000);
  await api("POST", `/session/${sid}/prompt_async?directory=${encodeURIComponent(proj)}`, { parts: [{ type: "text", text: "what is in this picture" }, { type: "file", mime: "image/png", filename: "dot.png", url: "data:image/png;base64," + PNG1 }] });
  await until("the picture", () => lines(sid).some((l) => l.from === "you" && l.image?.startsWith("data:image/png")), 30000).catch(() => {});
  check("a picture you attach goes to the screen", lines(sid).some((l) => l.from === "you" && l.image?.startsWith("data:image/png")), JSON.stringify(lines(sid).map((l) => [l.from, (l.text || "").slice(0, 30), !!l.image])));

  // Denied on the screen: the command doesn't run.
  relay.answer = { decision: "deny" };
  const before = relay.asks.length;
  await api("POST", `/session/${sid}/prompt_async?directory=${encodeURIComponent(proj)}`, { parts: [{ type: "text", text: "run it again" }] });
  await until("the second ask", () => relay.asks.length > before, 30000);
  await until("the turn to end", async () => (await api("GET", `/session/status?directory=${encodeURIComponent(proj)}`))[sid] === undefined || (await api("GET", `/session/status?directory=${encodeURIComponent(proj)}`))[sid]?.type === "idle", 30000);
  await sleep(800);
  const tools = lines(sid).filter((l) => l.from === "tool");
  check("each tool step is its own line", tools.length === 2, JSON.stringify(tools));
  check("denying on the screen stops the command", /failed/.test(tools[tools.length - 1]?.text), JSON.stringify(tools));

  // Answered on the computer: the request comes off the screen.
  relay.answer = {};
  relay.resolved = [];
  const before2 = relay.asks.length;
  await api("POST", `/session/${sid}/prompt_async?directory=${encodeURIComponent(proj)}`, { parts: [{ type: "text", text: "run once more" }] });
  await until("the third ask", () => relay.asks.length > before2, 30000);
  const pending = await until("OpenCode's own prompt", async () => (await api("GET", `/permission?directory=${encodeURIComponent(proj)}`))?.[0], 10000);
  await api("POST", `/permission/${pending.id}/reply?directory=${encodeURIComponent(proj)}`, { reply: "once" });
  await until("the resolve", () => relay.resolved.some((r) => r.key === relay.asks[relay.asks.length - 1].key), 10000);
  check("answering on the computer takes it off the screen", true);
  await until("the turn to end", () => lines(sid).filter((l) => l.from === "agent" && /printed hi/.test(l.text)).length >= 2, 30000);

  // The Deskling agent and its tools: a notification, a question on the screen, and a helper.
  const idle = () => until("the turn to end", async () => (await api("GET", `/session/status?directory=${encodeURIComponent(proj)}`))[sid] === undefined, 30000);
  const say = (text) => api("POST", `/session/${sid}/prompt_async?directory=${encodeURIComponent(proj)}`, { agent: "deskling", parts: [{ type: "text", text }] });
  await idle();
  await say("notify me when done");
  await until("the notification", () => relay.events.some((e) => e.kind === "notify"), 30000);
  check("an agent can notify you on the screen", relay.events.find((e) => e.kind === "notify").title === "Tests pass");
  await idle();
  relay.answer = { answers: { "Which database?": "SQLite" } };
  const asksBefore = relay.asks.length;
  await say("ask me which database");
  await until("the question", () => relay.asks.length > asksBefore, 30000);
  const q = relay.asks[relay.asks.length - 1];
  check("an agent can ask you on the screen", q.kind === "question" && q.questions[0].options.map((o) => o.label).join() === "Postgres,SQLite", JSON.stringify(q));
  await until("its reply with your answer", () => lines(sid).some((l) => l.from === "agent" && /SQLite/.test(l.text)), 30000);
  check("your answer reaches the agent", true);
  await idle();
  await say("make a plan for dark mode");
  await until("a helper", () => relay.updates.some((u) => u.id === `oc-${sid}` && u.helpers?.length), 30000);
  check("its helper shows up", true);
  await until("the helper to finish", () => relay.updates.some((u) => u.id === `oc-${sid}` && u.helpers?.some((h) => h.done)), 30000);
  const h = [...relay.updates].reverse().find((u) => u.id === `oc-${sid}` && u.helpers?.length).helpers[0];
  check("the helper is named and finishes", /plan/i.test(h.name) && h.done, JSON.stringify(h));
  check("the helper's own words stay out of the main chat", !lines(sid).some((l) => l.from === "agent" && /^planned: 1/.test(l.text)));
  await idle();

  await api("DELETE", `/session/${sid}?directory=${encodeURIComponent(proj)}`);
  await until("the end", () => relay.updates.some((u) => u.id === `oc-${sid}` && u.end), 10000);
  check("deleting the session removes it from the screen", true);

  // The runner: offers OpenCode with its models, installs the plugin, starts a session from the screen.
  stop(oc);
  rmSync(join(cfg, "plugins", "deskling.js"));
  rmSync(join(cfg, "agents"), { recursive: true });
  relay.updates.length = 0;
  const runnerPort = 30000 + Math.floor(Math.random() * 20000);
  runner = spawn(process.execPath, [join(here, "..", "..", "runner", "deskling-runner.mjs")], {
    env: { ...env, DESKLING_OPENCODE: BIN, DESKLING_CLAUDE: "off", DESKLING_OPENCODE_PORT: String(runnerPort), DESKLING_PROJECTS: proj, DESKLING_ACP: `OC ACP=${BIN} acp` },
    stdio: ["ignore", "pipe", "pipe"],
  });
  runner.stdout.on("data", (d) => (log += d));
  runner.stderr.on("data", (d) => (log += d));
  await until("the runner to register", () => relay.registered, 60000);
  const eng = relay.registered.engines?.find((e) => e.id === "opencode");
  check("the runner offers OpenCode", !!eng, JSON.stringify(relay.registered.engines));
  check("with the models you set up, your default first", eng?.models?.[0]?.id === "mock/mock-coder" && eng.models[0].name === "Mock coder", JSON.stringify(eng?.models));
  check("it installs the Deskling plugin and agents for OpenCode", existsSync(join(cfg, "plugins", "deskling.js")) && existsSync(join(cfg, "agents", "deskling-reviewer.md")));
  relay.job = { id: "j2", machine: relay.registered.machine, project: proj, prompt: "say hello to the screen", engine: "opencode", model: "mock/mock-coder" };
  await until("the job to start", () => relay.done, 30000);
  check("it starts an OpenCode session for the screen", relay.done.ok === true && relay.done.id === "j2", JSON.stringify(relay.done));
  await until("the new session's reply", () => relay.updates.some((u) => (u.lines || []).some((l) => l.from === "agent" && /say hello to the screen/.test(l.text))), 30000);
  const started = relay.updates.find((u) => (u.lines || []).some((l) => l.from === "you" && l.text === "say hello to the screen"));
  check("the session shows up with your prompt and its reply", started?.name === "web-app");

  // The same OpenCode over the Agent Client Protocol: how the runner drives Gemini CLI, Cursor,
  // Codex, Goose and the rest.
  check("the runner offers agents that speak ACP", relay.registered.engines.some((e) => e.id === "oc-acp" && e.name === "OC ACP"), JSON.stringify(relay.registered.engines.map((e) => e.id)));
  relay.done = null;
  relay.answer = { decision: "allow" };
  const asksBefore3 = relay.asks.length;
  relay.job = { id: "j3", machine: relay.registered.machine, project: proj, prompt: "please run echo hi", engine: "oc-acp" };
  await until("the ACP job to start", () => relay.done, 60000);
  check("it starts the agent and its session", relay.done.ok === true && relay.done.mode === "acp", JSON.stringify(relay.done));
  const aid = relay.done.session;
  const acpLines = () => {
    const out = new Map();
    for (const u of relay.updates) if (u.id === aid) for (const l of u.lines || []) out.set(l.key, l);
    return [...out.values()];
  };
  await until("an ACP permission ask", () => relay.asks.length > asksBefore3, 60000);
  const pa = relay.asks[relay.asks.length - 1];
  check("OpenCode's own Deskling plugin stays quiet when the runner drives it", !relay.updates.some((u) => u.id.startsWith("oc-ses_") && (u.lines || []).some((l) => l.text === "please run echo hi")));
  check("its permission prompts are asked on the screen", pa.id === aid && pa.kind === "permission", JSON.stringify(pa));
  await until("the ACP reply", () => acpLines().some((l) => l.from === "agent" && /printed hi/.test(l.text)), 60000);
  const al = acpLines();
  check("your prompt, its steps and its reply are in the chat", al.some((l) => l.from === "you" && l.text === "please run echo hi") && al.some((l) => l.from === "tool") && al.some((l) => l.from === "agent" && /Running it now/.test(l.text)), JSON.stringify(al));
  await until("the ACP turn to finish", () => relay.updates.some((u) => u.id === aid && u.state === "done"), 30000);
  check("it's marked done when the turn ends", true);
  relay.outbox.push({ id: aid, text: "say hello again" });
  await until("the reply to a screen message", () => acpLines().some((l) => l.from === "agent" && /local model\. say hello again/.test(l.text)), 60000);
  check("a message from the screen starts its next turn", true);
  runner.kill("SIGTERM");
  await until("the session to end", () => relay.updates.some((u) => u.id === aid && u.end), 10000);
  check("stopping the runner ends its sessions on the screen", true);
} catch (e) {
  check(String(e.message || e), false);
  console.log(log.split("\n").slice(-40).join("\n"));
  console.log(JSON.stringify(relay.updates.slice(-8), null, 1).slice(0, 3000));
} finally {
  stop(oc);
  runner?.kill("SIGTERM");
  await sleep(500);
  model.close();
  relayServer.close();
  rmSync(T, { recursive: true, force: true });
}
console.log(failures ? `\n${failures} failed` : "\nall passed");
process.exit(failures ? 1 : 0);
