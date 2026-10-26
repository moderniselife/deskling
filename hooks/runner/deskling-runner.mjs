#!/usr/bin/env node
// Deskling runner: lets your Deskling start new sessions on this computer: Claude Code, OpenCode,
// and any agent that speaks the Agent Client Protocol (Gemini CLI, Cursor, Codex, Copilot, Goose…).
// Zero dependencies, Node 18+. Mac, Windows and Linux.
//
//   node deskling-runner.mjs            run (keeps going; Ctrl+C to stop)
//   node deskling-runner.mjs --install  start it automatically when you log in
//   node deskling-runner.mjs --uninstall
//   node deskling-runner.mjs --list     show the project folders it offers
//
// It tells your relay which project folders it can start sessions in, then waits. When you tap
// New session on Deskling and pick this computer, it runs `claude --bg "<your prompt>"` in that
// folder: a background session that shows up on Deskling (with the Deskling plugin installed),
// listening for your next message. Attach to it any time with `claude attach`.
//
// With OpenCode installed it can start OpenCode sessions too, with any model you've set up in
// OpenCode, local ones included (Ollama, LM Studio, llama.cpp…). It keeps one `opencode serve`
// running on this computer (only reachable from this computer) for them, and installs the
// Deskling plugin for OpenCode so they show up on the screen. See them in OpenCode with
// `opencode attach http://127.0.0.1:4097 --password "$(cat ~/.deskling/opencode-password)"`.
//
// Settings: ~/.deskling/config (or the environment):
//   DESKLING_URL, DESKLING_TOKEN   your relay
//   DESKLING_NAME                  this computer's name on Deskling (default: its hostname)
//   DESKLING_PROJECTS              folders to offer, separated by commas or semicolons. Default:
//                                  the git repos one level inside ~/code, ~/Projects, ~/dev,
//                                  ~/src, ~/Developer, ~/repos and ~/GitHub.
//   DESKLING_CLAUDE                the claude command (default: claude)
//   DESKLING_OPENCODE              the opencode command (default: opencode; "off" to not offer it)
//   DESKLING_OPENCODE_PORT         the port for its OpenCode server (default: 4097)
//   DESKLING_ACP                   more ACP agents, or other commands for known ones:
//                                  "Pi=npx pi-acp; Gemini CLI=gemini --experimental-acp"
//   DESKLING_ACP_IDLE_HOURS        close an ACP session after this long without a turn (default 4)
//   DESKLING_OPENCODE_AGENT        the OpenCode agent for sessions from the screen (default: deskling;
//                                  "default" for OpenCode's own default)

import { spawn, spawnSync } from "node:child_process";
import { randomBytes } from "node:crypto";
import { existsSync, mkdirSync, openSync, readdirSync, readFileSync, realpathSync, rmSync, statSync, writeFileSync } from "node:fs";
import { homedir, hostname, platform } from "node:os";
import { basename, dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const VERSION = "0.3.0";
const HOME = homedir();

function fileConfig() {
  const out = {};
  try {
    for (const line of readFileSync(join(HOME, ".deskling", "config"), "utf8").split(/\r?\n/)) {
      const m = line.match(/^\s*(?:export\s+)?([A-Z_]+)\s*=\s*"?([^"]*)"?\s*$/);
      if (m) out[m[1]] = m[2];
    }
  } catch {
    /* none */
  }
  return out;
}
const fc = fileConfig();
const get = (k, d = "") => (process.env[k] ?? fc[k] ?? d).trim();
const URL_BASE = get("DESKLING_URL").replace(/\/+$/, "");
const TOKEN = get("DESKLING_TOKEN");
const NAME = (get("DESKLING_NAME") || hostname()).split(".")[0];
const CLAUDE = get("DESKLING_CLAUDE", "claude");
const OPENCODE = get("DESKLING_OPENCODE", "opencode");
const OC_PORT = Number(get("DESKLING_OPENCODE_PORT", "4097")) || 4097;
const OC_BASE = "https://raw.githubusercontent.com/moderniselife/deskling/main/hooks/opencode";
const OS = { darwin: "mac", win32: "windows", linux: "linux" }[platform()] ?? platform();
const log = (...a) => console.log(new Date().toISOString().slice(11, 19), ...a);

function projects() {
  const listed = get("DESKLING_PROJECTS");
  let dirs = [];
  if (listed) {
    dirs = listed.split(/[;,]/).map((d) => d.trim().replace(/^~(?=$|[\\/])/, HOME)).filter(Boolean);
  } else {
    for (const root of ["code", "Projects", "projects", "dev", "src", "Developer", "repos", "GitHub"]) {
      const r = join(HOME, root);
      let names = [];
      try {
        names = readdirSync(r);
      } catch {
        continue;
      }
      for (const n of names) {
        const d = join(r, n);
        try {
          if (statSync(d).isDirectory() && existsSync(join(d, ".git"))) dirs.push(d);
        } catch {
          /* skip */
        }
      }
    }
  }
  // The same folder can turn up twice: ~/Projects and ~/projects are one folder on a Mac, and
  // links point at folders listed elsewhere. Compare real paths (case-blind where the disk is).
  const caseBlind = platform() === "darwin" || platform() === "win32";
  const seen = new Set();
  const list = [];
  for (const d of dirs) {
    let real;
    try {
      real = realpathSync.native(resolve(d));
    } catch {
      continue;
    }
    const key = caseBlind ? real.toLowerCase() : real;
    if (seen.has(key)) continue;
    seen.add(key);
    list.push(real);
  }
  const mtime = (p) => {
    try {
      return statSync(p).mtimeMs;
    } catch {
      return 0;
    }
  };
  const picked = list.sort((a, b) => mtime(b) - mtime(a)).slice(0, 40); // recently touched first
  // Two different folders with the same name: say which is which, "deskling (GitHub)".
  const count = {};
  for (const p of picked) count[basename(p)] = (count[basename(p)] ?? 0) + 1;
  return picked.map((path) => ({ name: count[basename(path)] > 1 ? `${basename(path)} (${basename(dirname(path))})` : basename(path), path }));
}

async function relay(path, init = {}) {
  const res = await fetch(URL_BASE + path, {
    ...init,
    headers: { authorization: `Bearer ${TOKEN}`, "content-type": "application/json", ...(init.headers ?? {}) },
  });
  if (!res.ok) throw new Error(`${path}: ${res.status} ${await res.text().catch(() => "")}`.trim());
  return res.json();
}

// ---- running commands safely on every platform
/** Where a command is on this computer's PATH (Windows: with its extension), or "". */
function which(bin) {
  if (!bin) return "";
  const win = platform() === "win32";
  const exts = win ? ["", ...(process.env.PATHEXT || ".EXE;.CMD;.BAT").split(";")] : [""];
  if (/[\\/]/.test(bin)) {
    for (const ext of exts) if (existsSync(bin + ext) && (!win || ext || /\.\w+$/.test(bin))) return bin + ext;
    return "";
  }
  for (const dir of (process.env.PATH || "").split(win ? ";" : ":")) {
    for (const ext of win ? exts.slice(1) : exts) {
      if (dir && existsSync(join(dir, bin + ext))) return join(dir, bin + ext);
    }
  }
  return "";
}
const onPath = (bin) => !!which(bin);

// Windows: npm installs commands as .cmd scripts, which only run through cmd.exe, and cmd.exe
// treats & | < > ^ % and quotes in your prompt as commands. Quote and escape every argument the
// way cross-spawn does (twice for .cmd scripts, which pass their arguments through cmd again).
const CMD_META = /([()\][%!^"`<>&|;, *?])/g;
function cmdArg(arg, shim) {
  let a = String(arg).replace(/\r?\n/g, " ");
  a = a.replace(/(?=(\\+?)?)\1"/g, '$1$1\\"').replace(/(?=(\\+?)?)\1$/, "$1$1");
  a = `"${a}"`.replace(CMD_META, "^$1");
  return shim ? a.replace(CMD_META, "^$1") : a;
}
function runCmd(bin, args, opts = {}) {
  if (platform() !== "win32") return spawn(bin, args, opts);
  const path = which(bin);
  if (/\.exe$/i.test(path)) return spawn(path, args, { ...opts, shell: false });
  const shim = /\.(cmd|bat)$/i.test(path);
  const line = [(path || bin).replace(CMD_META, "^$1"), ...args.map((a) => cmdArg(a, shim))].join(" ");
  return spawn(process.env.comspec || "cmd.exe", ["/d", "/s", "/c", `"${line}"`], { ...opts, shell: false, windowsVerbatimArguments: true });
}

// ---- which agents this computer can start
function has(cmd) {
  if (!cmd || cmd === "off") return false;
  try {
    if (platform() === "win32") return onPath(cmd); // starting .cmd scripts just to ask is slow there
    const r = spawnSync(cmd, ["--version"], { timeout: 15_000, stdio: "ignore" });
    return r.status === 0;
  } catch {
    return false;
  }
}

// ---- OpenCode: one server on this computer for the sessions Deskling starts
// Its password stays the same between runs (~/.deskling/opencode-password), so `opencode attach`
// can use it.
function ocPassword() {
  const f = join(HOME, ".deskling", "opencode-password");
  try {
    const p = readFileSync(f, "utf8").trim();
    if (p) return p;
  } catch {
    /* first run */
  }
  const p = randomBytes(18).toString("base64url");
  try {
    mkdirSync(join(HOME, ".deskling"), { recursive: true });
    writeFileSync(f, p + "\n", { mode: 0o600 });
  } catch {
    /* keep it in memory */
  }
  return p;
}
const oc = { url: `http://127.0.0.1:${OC_PORT}`, password: "", proc: null, ok: false };
const ocHeaders = () => ({ "content-type": "application/json", authorization: "Basic " + Buffer.from(`opencode:${oc.password}`).toString("base64") });
async function ocFetch(path, init = {}) {
  const res = await fetch(oc.url + path, { ...init, headers: { ...ocHeaders(), ...(init.headers ?? {}) }, signal: AbortSignal.timeout(init.timeout ?? 20_000) });
  if (!res.ok) throw new Error(`OpenCode ${path}: ${res.status} ${(await res.text().catch(() => "")).slice(0, 200)}`);
  const t = await res.text();
  return t ? JSON.parse(t) : null;
}

/**
 * The plugin that shows OpenCode sessions on Deskling, and the Deskling agents, in OpenCode's
 * global config folder. The plugin is kept up to date; agents are only added when missing, so
 * your edits to them stay.
 */
const OC_FILES = [
  { from: "deskling.js", to: "plugins/deskling.js", update: true, check: "DesklingPlugin" },
  ...["deskling", "deskling-planner", "deskling-tester", "deskling-reviewer"].map((n) => ({ from: `agents/${n}.md`, to: `agents/${n}.md`, update: false, check: "description:" })),
];
async function installPlugin() {
  const cfg = join(process.env.XDG_CONFIG_HOME || join(HOME, ".config"), "opencode");
  const here = join(dirname(fileURLToPath(import.meta.url)), "..", "opencode");
  let ok = true;
  for (const f of OC_FILES) {
    const dest = join(cfg, f.to);
    if (!f.update && existsSync(dest)) continue;
    let code = "";
    if (existsSync(join(here, f.from))) code = readFileSync(join(here, f.from), "utf8");
    else {
      try {
        const res = await fetch(`${OC_BASE}/${f.from}`, { signal: AbortSignal.timeout(15_000) });
        if (res.ok) code = await res.text();
      } catch {
        /* offline */
      }
    }
    if (!code.includes(f.check)) {
      ok &&= existsSync(dest);
      continue;
    }
    let old = "";
    try {
      old = readFileSync(dest, "utf8");
    } catch {
      /* new */
    }
    if (old !== code) {
      mkdirSync(dirname(dest), { recursive: true });
      writeFileSync(dest, code);
      log(`installed ${dest}`);
    }
  }
  return ok;
}

async function ensureOpenCode() {
  oc.password ||= ocPassword();
  if (oc.ok) {
    try {
      await ocFetch("/config", { timeout: 5000 });
      return true;
    } catch {
      oc.ok = false;
    }
  }
  stopOpenCode();
  mkdirSync(join(HOME, ".deskling"), { recursive: true });
  const fd = openSync(join(HOME, ".deskling", "opencode.log"), "a");
  oc.proc = runCmd(OPENCODE, ["serve", "--port", String(OC_PORT), "--hostname", "127.0.0.1"], {
    cwd: HOME,
    env: { ...process.env, OPENCODE_SERVER_PASSWORD: oc.password, OPENCODE_SERVER_USERNAME: "opencode" },
    stdio: ["ignore", fd, fd],
    windowsHide: true,
    detached: platform() !== "win32", // its own process group, so it can be stopped as a whole
  });
  const child = oc.proc;
  child.on("error", () => {});
  child.on("exit", () => {
    if (oc.proc !== child) return;
    oc.proc = null;
    oc.ok = false;
  });
  for (let i = 0; i < 60; i++) {
    await new Promise((r) => setTimeout(r, 500));
    try {
      await ocFetch("/config", { timeout: 3000 });
      oc.ok = true;
      log(`OpenCode server on ${oc.url}. Open its sessions with: opencode attach ${oc.url} --password "$(cat ~/.deskling/opencode-password)"`);
      return true;
    } catch {
      if (!oc.proc) break;
    }
  }
  log("OpenCode didn't start: see ~/.deskling/opencode.log");
  return false;
}

/** Up to 8 models to offer: your default first, then the ones you set up yourself (local ones). */
async function openCodeModels() {
  const [{ providers = [], default: defaults = {} }, config] = await Promise.all([ocFetch("/config/providers"), ocFetch("/config").catch(() => ({}))]);
  const out = [];
  const add = (pid, mid) => {
    const p = providers.find((x) => x.id === pid);
    const m = p?.models?.[mid];
    const id = `${pid}/${mid}`;
    if (!m || out.some((o) => o.id === id) || out.length >= 8) return;
    out.push({ id, name: m.name || mid, provider: p.name || pid });
  };
  if (typeof config?.model === "string" && config.model.includes("/")) {
    const i = config.model.indexOf("/");
    add(config.model.slice(0, i), config.model.slice(i + 1));
  }
  for (const p of providers.filter((x) => x.source === "config")) for (const mid of Object.keys(p.models ?? {})) add(p.id, mid);
  for (const p of providers.filter((x) => x.source !== "config")) if (defaults[p.id]) add(p.id, defaults[p.id]);
  // The same name from two providers: say which is which.
  return out.map((m) => ({ id: m.id, name: out.filter((o) => o.name === m.name).length > 1 ? `${m.name} (${m.provider})`.slice(0, 30) : m.name.slice(0, 30) }));
}

// ---- Any agent that speaks the Agent Client Protocol (ACP): Gemini CLI, Cursor, Copilot, Goose,
// Qwen Code, Kimi, Kiro, Codex (through codex-acp)… The runner starts the agent, sends it your
// prompt, and puts the whole session on Deskling: its replies, thinking and steps, permission
// prompts to answer on the screen, and your messages from the screen.
const ACP_KNOWN = [
  { id: "gemini", name: "Gemini CLI", bin: "gemini", args: ["--acp"], source: "gemini" },
  { id: "cursor", name: "Cursor", bin: "cursor-agent", args: ["acp"], source: "cursor" },
  { id: "codex", name: "Codex", bin: "codex-acp", args: [], source: "codex" },
  { id: "copilot", name: "Copilot", bin: "copilot", args: ["--acp", "--stdio"], source: "custom" },
  { id: "goose", name: "Goose", bin: "goose", args: ["acp"], source: "custom" },
  { id: "qwen", name: "Qwen Code", bin: "qwen", args: ["--acp"], source: "custom" },
  { id: "kimi", name: "Kimi", bin: "kimi", args: ["acp"], source: "custom" },
  { id: "kiro", name: "Kiro", bin: "kiro-cli-chat", args: ["acp"], source: "custom" },
];

/** The ACP agents to offer: the known ones that are installed, plus DESKLING_ACP
 *  ("Name=command args; Other=command"), which also replaces a known one with the same name. */
function acpAgents() {
  const list = ACP_KNOWN.filter((a) => onPath(a.bin));
  for (const item of get("DESKLING_ACP").split(";").map((x) => x.trim()).filter(Boolean)) {
    const i = item.indexOf("=");
    if (i < 1) continue;
    const name = item.slice(0, i).trim();
    const [bin, ...args] = item.slice(i + 1).trim().split(/\s+/);
    const id = name.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "").slice(0, 16) || "acp";
    const known = ACP_KNOWN.find((a) => a.id === id);
    const at = list.findIndex((a) => a.id === id);
    const def = { id, name: name.slice(0, 24), bin, args, source: known?.source ?? "custom" };
    if (at >= 0) list[at] = def;
    else list.push(def);
  }
  return list;
}

const acpSessions = new Map(); // relay agent id -> AcpSession
const ACP_IDLE_MS = (Number(get("DESKLING_ACP_IDLE_HOURS", "4")) || 4) * 3600_000;

class AcpSession {
  constructor(def, cwd) {
    this.def = def;
    this.cwd = cwd;
    this.id = `${def.id}-${randomBytes(5).toString("hex")}`;
    this.name = basename(cwd);
    this.nextId = 1;
    this.calls = new Map();
    this.busy = false;
    this.queue = [];
    this.seg = 0; // a new reply line after each step
    this.text = "";
    this.thought = "";
    this.tools = new Map();
    this.used = Date.now();
    this.pending = { fields: {}, lines: new Map() };
    this.timer = null;
  }

  send(msg) {
    this.proc.stdin.write(JSON.stringify({ jsonrpc: "2.0", ...msg }) + "\n");
  }
  call(method, params) {
    const id = this.nextId++;
    this.send({ id, method, params });
    return new Promise((resolve, reject) => this.calls.set(id, { resolve, reject }));
  }

  async start() {
    // DESKLING_BRIDGED: the agent's own Deskling plugin or hooks stay quiet (this shows the session).
    this.proc = runCmd(this.def.bin, this.def.args, { cwd: this.cwd, env: { ...process.env, DESKLING_BRIDGED: "1" }, stdio: ["pipe", "pipe", "pipe"], windowsHide: true, detached: platform() !== "win32" });
    let buf = "";
    let err = "";
    this.proc.stdout.on("data", (d) => {
      buf += d;
      let i;
      while ((i = buf.indexOf("\n")) >= 0) {
        const line = buf.slice(0, i).trim();
        buf = buf.slice(i + 1);
        if (!line.startsWith("{")) continue;
        try {
          this.onMessage(JSON.parse(line));
        } catch (e) {
          log(`${this.def.name}: ${e.message}`);
        }
      }
    });
    this.proc.stderr.on("data", (d) => (err = (err + d).slice(-2000)));
    const died = new Promise((_, reject) =>
      this.proc.on("exit", (code) => {
        for (const c of this.calls.values()) c.reject(new Error(err.trim().split("\n").pop() || `${this.def.name} exited (${code})`));
        this.calls.clear();
        reject(new Error(err.trim().split("\n").pop() || `${this.def.name} exited (${code})`));
        this.close(false);
      }),
    );
    died.catch(() => {});
    this.proc.on("error", () => {});
    const init = await Promise.race([
      this.call("initialize", { protocolVersion: 1, clientCapabilities: { fs: { readTextFile: false, writeTextFile: false }, terminal: false }, clientInfo: { name: "deskling-runner", version: VERSION } }),
      died,
    ]);
    const created = await Promise.race([this.call("session/new", { cwd: this.cwd, mcpServers: [] }), died]).catch((e) => {
      if (init?.authMethods?.length) throw new Error(`${this.def.name} needs you to sign in on this computer first (${e.message})`);
      throw e;
    });
    this.sessionId = created.sessionId;
    acpSessions.set(this.id, this);
    const model = created.models?.currentModelId;
    this.update({ state: "idle", task: "Ready for a prompt", ...(model ? { model } : {}) });
    return this;
  }

  // ---- what the agent tells us
  onMessage(m) {
    if (m.id !== undefined && !m.method) {
      const c = this.calls.get(m.id);
      this.calls.delete(m.id);
      if (m.error) c?.reject(new Error(m.error.message || "error"));
      else c?.resolve(m.result);
      return;
    }
    if (m.method === "session/update") return this.onUpdate(m.params?.update ?? {});
    if (m.method === "session/request_permission") return this.onPermission(m.id, m.params);
    // Anything else (files, terminals) we said we can't do.
    if (m.id !== undefined) this.send({ id: m.id, error: { code: -32601, message: "Not supported by Deskling" } });
  }

  onUpdate(u) {
    const kind = u.sessionUpdate;
    const text = u.content?.type === "text" ? u.content.text : "";
    if (kind === "agent_message_chunk" && text) {
      this.text += text;
      this.line({ key: `m${this.seg}`, from: "agent", text: this.text });
    } else if (kind === "agent_thought_chunk" && text) {
      this.thought += text;
      const t = this.thought.split(/\n+/).map((l) => l.trim()).filter(Boolean).slice(0, 3).join("\n");
      if (t) this.line({ key: `t${this.seg}`, from: "thought", text: t });
    } else if (kind === "tool_call" || kind === "tool_call_update") {
      const t = this.tools.get(u.toolCallId) ?? { title: "", status: "pending" };
      if (u.title) t.title = u.title;
      if (u.status) t.status = u.status;
      this.tools.set(u.toolCallId, t);
      if (kind === "tool_call") this.breakLine();
      const title = (t.title || "Working").replace(/\s+/g, " ").slice(0, 90);
      this.line({ key: `c-${u.toolCallId}`, from: "tool", text: t.status === "failed" ? `${title} (failed)` : title }, t.status === "in_progress" || t.status === "pending" ? { task: title } : {});
    } else if (kind === "plan" && Array.isArray(u.entries)) {
      const doing = u.entries.find((e) => e.status === "in_progress");
      if (doing?.content) this.update({ task: String(doing.content).slice(0, 90) });
    } else if (kind === "usage_update" && typeof u.used === "number") {
      this.update({ context: u.used });
    } else if (kind === "current_model_update" && u.modelId) {
      this.update({ model: u.modelId });
    }
  }

  breakLine() {
    if (this.text || this.thought) this.seg++;
    this.text = "";
    this.thought = "";
  }

  async onPermission(rpcId, p) {
    const options = p?.options ?? [];
    const pick = (...kinds) => options.find((o) => kinds.includes(o.kind))?.optionId;
    const tc = p?.toolCall ?? {};
    const raw = tc.rawInput ?? {};
    const summary = String(raw.command ?? raw.cmd ?? raw.path ?? raw.file_path ?? raw.url ?? tc.title ?? "a tool").slice(0, 200);
    const key = String(tc.toolCallId ?? rpcId);
    // There's no other place to answer: keep asking on the screen (up to 30 minutes), then decline.
    const until = Date.now() + 30 * 60_000;
    let decision = null;
    while (!decision && Date.now() < until && acpSessions.has(this.id)) {
      await this.flush();
      const r = await relay("/agent/ask", { method: "POST", body: JSON.stringify({ id: this.id, key, kind: "permission", tool: tc.kind ? tc.kind[0].toUpperCase() + tc.kind.slice(1) : "Tool", summary, detail: tc.title ?? "", wait: 110 }) }).catch(() => ({}));
      if (r.decision === "allow" || r.decision === "deny") decision = r.decision;
      else if (!r.timeout) await new Promise((res) => setTimeout(res, 5000));
    }
    const optionId = decision === "allow" ? pick("allow_once", "allow_always") : pick("reject_once", "reject_always");
    this.send({ id: rpcId, result: { outcome: optionId ? { outcome: "selected", optionId } : { outcome: "cancelled" } } });
  }

  // ---- what we send the relay (batched: replies stream in many small pieces)
  line(l, fields = {}) {
    this.pending.lines.set(l.key, l);
    Object.assign(this.pending.fields, fields);
    this.schedule();
  }
  update(fields) {
    Object.assign(this.pending.fields, fields);
    this.schedule();
  }
  schedule() {
    if (!this.timer) this.timer = setTimeout(() => this.flush(), 300);
  }
  async flush() {
    clearTimeout(this.timer);
    this.timer = null;
    const { fields, lines } = this.pending;
    this.pending = { fields: {}, lines: new Map() };
    const body = { id: this.id, source: this.def.source, name: this.name, host: NAME, os: OS, ...fields, lines: [...lines.values()] };
    await relay("/agent/update", { method: "POST", body: JSON.stringify(body) }).catch((e) => log("relay:", e.message));
  }

  // ---- prompts: yours from the screen, one turn at a time
  prompt(text) {
    this.used = Date.now();
    if (this.busy) return void this.queue.push(text);
    this.busy = true;
    this.breakLine();
    this.seg++;
    this.line({ key: `u${this.seg}`, from: "you", text }, { state: "working", task: "Working" });
    this.call("session/prompt", { sessionId: this.sessionId, prompt: [{ type: "text", text }] })
      .then((r) => ({ state: "done", task: r?.stopReason === "cancelled" ? "Stopped" : r?.stopReason === "refusal" ? "Declined" : "Finished" }))
      .catch((e) => ({ state: "done", task: `Stopped: ${e.message}`.slice(0, 90) }))
      .then((f) => {
        this.busy = false;
        this.used = Date.now();
        this.breakLine();
        this.update(f);
        const next = this.queue.shift();
        if (next) this.prompt(next);
      });
  }

  close(kill = true) {
    if (!acpSessions.has(this.id)) return;
    acpSessions.delete(this.id);
    if (kill && this.proc?.pid) {
      try {
        if (platform() === "win32") spawnSync("taskkill", ["/pid", String(this.proc.pid), "/t", "/f"], { stdio: "ignore", windowsHide: true });
        else process.kill(-this.proc.pid, "SIGTERM");
      } catch {
        this.proc.kill();
      }
    }
    this.flush().then(() => relay("/agent/update", { method: "POST", body: JSON.stringify({ id: this.id, end: true }) }).catch(() => {}));
  }
}

async function startAcp(job, def) {
  log(`starting ${def.name} in ${job.project}: ${job.prompt.slice(0, 80)}`);
  try {
    const s = await new AcpSession(def, job.project).start();
    s.prompt(job.prompt);
    acpLoop();
    return { ok: true, mode: "acp", session: s.id };
  } catch (e) {
    return { ok: false, error: e.message };
  }
}

/** Messages typed on the screen for the ACP sessions, and closing the ones idle for hours. */
let acpPolling = false;
async function acpLoop() {
  if (acpPolling) return;
  acpPolling = true;
  while (acpSessions.size) {
    for (const s of acpSessions.values()) if (!s.busy && Date.now() - s.used > ACP_IDLE_MS) s.close();
    if (!acpSessions.size) break;
    try {
      const ids = [...acpSessions.keys()].join(",");
      const { messages = [] } = await relay(`/agent/poll?ids=${encodeURIComponent(ids)}&wait=25`);
      for (const m of messages) acpSessions.get(m.id)?.prompt(m.text);
    } catch (e) {
      log("relay:", e.message);
      await new Promise((r) => setTimeout(r, 5000));
    }
  }
  acpPolling = false;
}

async function engines() {
  const list = [];
  if (has(CLAUDE)) list.push({ id: "claude", name: "Claude Code", models: [] });
  if (has(OPENCODE) && (await ensureOpenCode())) {
    await installPlugin();
    let models = [];
    try {
      models = await openCodeModels();
    } catch (e) {
      log("couldn't list OpenCode's models:", e.message);
    }
    list.push({ id: "opencode", name: "OpenCode", models });
  }
  for (const a of acpAgents()) if (!list.some((e) => e.id === a.id)) list.push({ id: a.id, name: a.name, models: [] });
  return list;
}

async function startOpenCode(job) {
  log(`starting OpenCode in ${job.project}${job.model ? ` with ${job.model}` : ""}: ${job.prompt.slice(0, 80)}`);
  if (!(await ensureOpenCode())) return { ok: false, error: "OpenCode didn't start" };
  try {
    const q = `?directory=${encodeURIComponent(job.project)}`;
    const session = await ocFetch(`/session${q}`, { method: "POST", body: "{}" });
    const body = { parts: [{ type: "text", text: job.prompt }] };
    // Sessions from the screen use the Deskling agent (it keeps you posted there) unless you pick another.
    const agent = get("DESKLING_OPENCODE_AGENT", "deskling");
    if (agent !== "default" && (agent !== "deskling" || existsSync(join(process.env.XDG_CONFIG_HOME || join(HOME, ".config"), "opencode", "agents", "deskling.md")))) body.agent = agent;
    if (job.model?.includes("/")) {
      const i = job.model.indexOf("/");
      body.model = { providerID: job.model.slice(0, i), modelID: job.model.slice(i + 1) };
    }
    await ocFetch(`/session/${session.id}/prompt_async${q}`, { method: "POST", body: JSON.stringify(body) });
    return { ok: true, mode: "opencode", session: session.id };
  } catch (e) {
    return { ok: false, error: e.message };
  }
}

/* Headless fallback when background sessions aren't available: `claude -p` in its own process.
   It still reports to Deskling, and with Keep listening on (sessions you start from Deskling
   listen straight away) it carries on with each message you send from the screen. */
function startHeadless(job) {
  const logFile = join(HOME, ".deskling", "sessions.log");
  mkdirSync(join(HOME, ".deskling"), { recursive: true });
  const fd = openSync(logFile, "a");
  const child = runCmd(CLAUDE, ["-p", job.prompt], { cwd: job.project, detached: true, stdio: ["ignore", fd, fd], windowsHide: true });
  child.on("error", () => {});
  child.unref();
  return { ok: true, mode: "headless" };
}

function start(job) {
  if (job.engine === "opencode") return startOpenCode(job);
  const acp = job.engine && job.engine !== "claude" ? acpAgents().find((a) => a.id === job.engine) : null;
  if (acp) return startAcp(job, acp);
  return startBg(job).then((r) => (r.ok || r.missing ? r : (log(`background sessions unavailable (${r.error}); running headless`), startHeadless(job))));
}

function startBg(job) {
  return new Promise((done) => {
    log(`starting a session in ${job.project}: ${job.prompt.slice(0, 80)}`);
    // (On Windows cmd.exe runs it, so a missing command doesn't show up as an error code.)
    if (platform() === "win32" && !onPath(CLAUDE)) return done({ ok: false, missing: true, error: `"${CLAUDE}" isn't on this computer's PATH` });
    let err = "";
    // `claude --bg` hands the session to Claude Code's background supervisor and returns.
    const child = runCmd(CLAUDE, ["--bg", job.prompt], { cwd: job.project, stdio: ["ignore", "ignore", "pipe"], windowsHide: true });
    child.stderr.on("data", (d) => (err += d));
    child.on("error", (e) => done({ ok: false, missing: e.code === "ENOENT", error: e.code === "ENOENT" ? `"${CLAUDE}" isn't on this computer's PATH` : e.message }));
    child.on("exit", (code) => done(code === 0 ? { ok: true } : { ok: false, error: err.trim().split("\n").pop() || `claude exited with ${code}` }));
  });
}

// Take the OpenCode server down with the runner (its whole process group: `opencode` from npm is a
// small launcher that starts the real one).
function stopAll() {
  for (const s of acpSessions.values()) s.close();
  stopOpenCode();
}
function stopOpenCode() {
  const p = oc.proc;
  oc.proc = null;
  oc.ok = false;
  if (!p?.pid) return;
  try {
    if (platform() === "win32") spawnSync("taskkill", ["/pid", String(p.pid), "/t", "/f"], { stdio: "ignore", windowsHide: true });
    else process.kill(-p.pid, "SIGTERM");
  } catch {
    p.kill();
  }
}
for (const sig of ["SIGINT", "SIGTERM", "SIGHUP"])
  process.on(sig, () => {
    stopAll();
    setTimeout(() => process.exit(0), 300); // let the screen hear the sessions ended
  });
process.on("exit", stopOpenCode);

async function run() {
  if (!URL_BASE || !TOKEN) {
    console.error("Set DESKLING_URL and DESKLING_TOKEN in ~/.deskling/config first.");
    process.exit(1);
  }
  log(`Deskling runner ${VERSION} as "${NAME}" on ${URL_BASE}`);
  let list = projects();
  log(`offering ${list.length} project folder${list.length === 1 ? "" : "s"}${list.length ? ": " + list.map((p) => p.name).join(", ") : ""}`);
  let engs = await engines();
  let lastEngines = Date.now();
  log(`can start: ${engs.map((e) => e.name + (e.models.length ? ` (${e.models.map((m) => m.id).join(", ")})` : "")).join(", ") || "nothing (install Claude Code, OpenCode or another agent)"}`);
  const register = async () => {
    list = projects();
    // What's installed and which models are set up changes rarely: check every 10 minutes.
    if (Date.now() - lastEngines > 600_000) {
      engs = await engines();
      lastEngines = Date.now();
    }
    await relay("/runner/register", { method: "POST", body: JSON.stringify({ machine: NAME, os: OS, projects: list, engines: engs, version: VERSION }) });
  };
  let lastReg = 0;
  for (;;) {
    try {
      if (Date.now() - lastReg > 60_000) {
        await register();
        lastReg = Date.now();
      }
      const { job } = await relay(`/runner/poll?machine=${encodeURIComponent(NAME)}`);
      if (job) {
        const r = await start(job);
        if (!r.ok) log("couldn't start it:", r.error);
        await relay("/runner/done", { method: "POST", body: JSON.stringify({ id: job.id, machine: NAME, ...r }) }).catch(() => {});
      }
    } catch (e) {
      log("relay:", e.message);
      await new Promise((r) => setTimeout(r, 10_000));
    }
  }
}

// ---- start at login
const self = fileURLToPath(import.meta.url);
const plist = join(HOME, "Library", "LaunchAgents", "com.deskling.runner.plist");
const startup = join(process.env.APPDATA ?? "", "Microsoft", "Windows", "Start Menu", "Programs", "Startup", "deskling-runner.vbs");
const unit = join(HOME, ".config", "systemd", "user", "deskling-runner.service");

async function install() {
  if (has(OPENCODE)) await installPlugin();
  const node = process.execPath;
  const logFile = join(HOME, ".deskling", "runner.log");
  mkdirSync(join(HOME, ".deskling"), { recursive: true });
  if (platform() === "darwin") {
    mkdirSync(join(HOME, "Library", "LaunchAgents"), { recursive: true });
    // launchd starts with a bare PATH: keep the one claude was found on.
    writeFileSync(
      plist,
      `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>com.deskling.runner</string>
  <key>ProgramArguments</key><array><string>${node}</string><string>${self}</string></array>
  <key>EnvironmentVariables</key><dict><key>PATH</key><string>${process.env.PATH}</string></dict>
  <key>RunAtLoad</key><true/><key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>${logFile}</string><key>StandardErrorPath</key><string>${logFile}</string>
</dict></plist>
`,
    );
    spawn("launchctl", ["unload", plist], { stdio: "ignore" }).on("exit", () => spawn("launchctl", ["load", plist], { stdio: "inherit" }));
    console.log(`Installed: it starts when you log in (log: ${logFile}).`);
  } else if (platform() === "win32") {
    writeFileSync(startup, `CreateObject("WScript.Shell").Run """${node}"" ""${self}""", 0, False\r\n`);
    spawn("wscript", [startup], { detached: true, stdio: "ignore" }).unref();
    console.log("Installed: it starts when you log in, hidden.");
  } else {
    mkdirSync(join(HOME, ".config", "systemd", "user"), { recursive: true });
    writeFileSync(unit, `[Unit]\nDescription=Deskling runner\n\n[Service]\nExecStart=${node} ${self}\nRestart=always\nEnvironment=PATH=${process.env.PATH}\n\n[Install]\nWantedBy=default.target\n`);
    spawn("systemctl", ["--user", "enable", "--now", "deskling-runner"], { stdio: "inherit" });
    console.log("Installed as a systemd user service.");
  }
}

function uninstall() {
  if (platform() === "darwin" && existsSync(plist)) {
    spawn("launchctl", ["unload", plist], { stdio: "inherit" }).on("exit", () => rmSync(plist, { force: true }));
  } else if (platform() === "win32") {
    rmSync(startup, { force: true });
    console.log("Removed from startup. It stops at your next log-out (or end node.exe in Task Manager).");
  } else {
    spawn("systemctl", ["--user", "disable", "--now", "deskling-runner"], { stdio: "inherit" }).on("exit", () => rmSync(unit, { force: true }));
  }
}

const arg = process.argv[2];
if (arg === "--install") await install();
else if (arg === "--uninstall") uninstall();
else if (arg === "--list") for (const p of projects()) console.log(`${p.name}\t${p.path}`);
else run();
