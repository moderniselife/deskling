#!/usr/bin/env node
// Deskling runner: lets your Deskling start new Claude Code and OpenCode sessions on this computer.
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
//   DESKLING_OPENCODE_AGENT        the OpenCode agent for sessions from the screen (default: deskling;
//                                  "default" for OpenCode's own default)

import { spawn, spawnSync } from "node:child_process";
import { randomBytes } from "node:crypto";
import { existsSync, mkdirSync, openSync, readdirSync, readFileSync, realpathSync, rmSync, statSync, writeFileSync } from "node:fs";
import { homedir, hostname, platform } from "node:os";
import { basename, dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const VERSION = "0.2.0";
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

// ---- which agents this computer can start
function has(cmd) {
  if (!cmd || cmd === "off") return false;
  try {
    const r = spawnSync(cmd, ["--version"], { shell: platform() === "win32", timeout: 15_000, stdio: "ignore", windowsHide: true });
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
  oc.proc = spawn(OPENCODE, ["serve", "--port", String(OC_PORT), "--hostname", "127.0.0.1"], {
    cwd: HOME,
    shell: platform() === "win32",
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
  const child = spawn(CLAUDE, ["-p", job.prompt], { cwd: job.project, shell: platform() === "win32", detached: true, stdio: ["ignore", fd, fd], windowsHide: true });
  child.on("error", () => {});
  child.unref();
  return { ok: true, mode: "headless" };
}

function start(job) {
  if (job.engine === "opencode") return startOpenCode(job);
  return startBg(job).then((r) => (r.ok || r.missing ? r : (log(`background sessions unavailable (${r.error}); running headless`), startHeadless(job))));
}

function startBg(job) {
  return new Promise((done) => {
    log(`starting a session in ${job.project}: ${job.prompt.slice(0, 80)}`);
    let err = "";
    // `claude --bg` hands the session to Claude Code's background supervisor and returns.
    const child = spawn(CLAUDE, ["--bg", job.prompt], { cwd: job.project, shell: platform() === "win32", stdio: ["ignore", "ignore", "pipe"], windowsHide: true });
    child.stderr.on("data", (d) => (err += d));
    child.on("error", (e) => done({ ok: false, missing: e.code === "ENOENT", error: e.code === "ENOENT" ? `"${CLAUDE}" isn't on this computer's PATH` : e.message }));
    child.on("exit", (code) => done(code === 0 ? { ok: true } : { ok: false, error: err.trim().split("\n").pop() || `claude exited with ${code}` }));
  });
}

// Take the OpenCode server down with the runner (its whole process group: `opencode` from npm is a
// small launcher that starts the real one).
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
    stopOpenCode();
    process.exit(0);
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
  log(`can start: ${engs.map((e) => e.name + (e.models.length ? ` (${e.models.map((m) => m.id).join(", ")})` : "")).join(", ") || "nothing (install Claude Code or OpenCode)"}`);
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
