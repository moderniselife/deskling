#!/usr/bin/env node
// Deskling runner: lets your Deskling start new Claude Code sessions on this computer.
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
// Settings: ~/.deskling/config (or the environment):
//   DESKLING_URL, DESKLING_TOKEN   your relay
//   DESKLING_NAME                  this computer's name on Deskling (default: its hostname)
//   DESKLING_PROJECTS              folders to offer, separated by commas or semicolons. Default:
//                                  the git repos one level inside ~/code, ~/Projects, ~/dev,
//                                  ~/src, ~/Developer, ~/repos and ~/GitHub.
//   DESKLING_CLAUDE                the claude command (default: claude)

import { spawn } from "node:child_process";
import { existsSync, mkdirSync, openSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { homedir, hostname, platform } from "node:os";
import { basename, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const VERSION = "0.1.0";
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
  const seen = new Set();
  return dirs
    .map((d) => resolve(d))
    .filter((d) => existsSync(d) && !seen.has(d) && seen.add(d))
    .sort((a, b) => {
      try {
        return statSync(b).mtimeMs - statSync(a).mtimeMs; // recently touched first
      } catch {
        return 0;
      }
    })
    .slice(0, 40)
    .map((path) => ({ name: basename(path), path }));
}

async function relay(path, init = {}) {
  const res = await fetch(URL_BASE + path, {
    ...init,
    headers: { authorization: `Bearer ${TOKEN}`, "content-type": "application/json", ...(init.headers ?? {}) },
  });
  if (!res.ok) throw new Error(`${path}: ${res.status} ${await res.text().catch(() => "")}`.trim());
  return res.json();
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

async function run() {
  if (!URL_BASE || !TOKEN) {
    console.error("Set DESKLING_URL and DESKLING_TOKEN in ~/.deskling/config first.");
    process.exit(1);
  }
  log(`Deskling runner ${VERSION} as "${NAME}" on ${URL_BASE}`);
  let list = projects();
  log(`offering ${list.length} project folder${list.length === 1 ? "" : "s"}${list.length ? ": " + list.map((p) => p.name).join(", ") : ""}`);
  const register = async () => {
    list = projects();
    await relay("/runner/register", { method: "POST", body: JSON.stringify({ machine: NAME, os: OS, projects: list, version: VERSION }) });
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

function install() {
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
if (arg === "--install") install();
else if (arg === "--uninstall") uninstall();
else if (arg === "--list") for (const p of projects()) console.log(`${p.name}\t${p.path}`);
else run();
