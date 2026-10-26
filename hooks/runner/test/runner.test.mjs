// Tests for deskling-runner.mjs: project discovery and starting a session for a job from the relay.
//   node hooks/runner/test/runner.test.mjs
import { spawn, spawnSync } from "node:child_process";
import { chmodSync, existsSync, mkdirSync, mkdtempSync, readFileSync, symlinkSync, writeFileSync } from "node:fs";
import http from "node:http";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const RUNNER = join(dirname(fileURLToPath(import.meta.url)), "..", "deskling-runner.mjs");
let failures = 0;
const check = (name, ok, extra = "") => {
  console.log(`${ok ? "PASS" : "FAIL"}  ${name}${ok || !extra ? "" : "  " + extra}`);
  if (!ok) failures++;
};

const home = mkdtempSync(join(tmpdir(), "dk-runner-"));
for (const d of ["code/web-app", "code/deskling", "GitHub/deskling", "code/notes"]) mkdirSync(join(home, d), { recursive: true });
for (const d of ["code/web-app", "code/deskling", "GitHub/deskling"]) mkdirSync(join(home, d, ".git"));
symlinkSync(join(home, "code/web-app"), join(home, "GitHub/web-app")); // the same folder twice

// --list
const list = spawnSync(process.execPath, [RUNNER, "--list"], { env: { ...process.env, HOME: home, DESKLING_PROJECTS: "" }, encoding: "utf8" });
const names = list.stdout.trim().split("\n").map((l) => l.split("\t")[0]).sort();
check("finds git projects in the usual folders, once each", JSON.stringify(names) === JSON.stringify(["deskling (GitHub)", "deskling (code)", "web-app"]), names.join(", "));

// A job from the relay starts `claude --bg "<prompt>"` in that folder.
const fake = join(home, "fake-claude");
const log = join(home, "fake-claude.log");
writeFileSync(fake, `#!/bin/sh\necho "$(pwd)|$*" >> "${log}"\n`);
chmodSync(fake, 0o755);
let registered = null;
let jobSent = false;
let done = null;
const server = http.createServer((req, res) => {
  let body = "";
  req.on("data", (d) => (body += d));
  req.on("end", () => {
    const send = (o) => {
      res.writeHead(200, { "content-type": "application/json" });
      res.end(JSON.stringify(o));
    };
    if (req.headers.authorization !== "Bearer t0k") return res.writeHead(401).end();
    if (req.url === "/runner/register") {
      registered = JSON.parse(body);
      return send({ ok: true });
    }
    if (req.url.startsWith("/runner/poll")) {
      if (!jobSent && registered) {
        jobSent = true;
        return send({ job: { id: "j1", machine: registered.machine, project: join(home, "code/web-app"), prompt: "add a dark mode" } });
      }
      return setTimeout(() => send({ job: null }), 300);
    }
    if (req.url === "/runner/done") {
      done = JSON.parse(body);
      return send({ ok: true });
    }
    res.writeHead(404).end();
  });
});
await new Promise((r) => server.listen(0, "127.0.0.1", r));
const env = { ...process.env, HOME: home, DESKLING_URL: `http://127.0.0.1:${server.address().port}`, DESKLING_TOKEN: "t0k", DESKLING_NAME: "test-mac.local", DESKLING_CLAUDE: fake, DESKLING_OPENCODE: "off", DESKLING_PROJECTS: "" };
const p = spawn(process.execPath, [RUNNER], { env, stdio: "ignore" });
for (let i = 0; i < 50 && !done; i++) await new Promise((r) => setTimeout(r, 100));
p.kill();
check("registers this computer by its short name", registered?.machine === "test-mac" && registered.projects.length === 3);
check("starts the session in that folder, in the background", existsSync(log) && readFileSync(log, "utf8").split("\n").filter((l) => l && !l.endsWith("|--version")).join("\n") === `${join(home, "code/web-app")}|--bg add a dark mode`);
check("tells the relay it started", done?.ok === true && done.id === "j1");
check("offers Claude Code when it's installed", registered?.engines?.length === 1 && registered.engines[0].id === "claude", JSON.stringify(registered?.engines));

server.close();
console.log(failures ? `\n${failures} failed` : "\nall passed");
process.exit(failures ? 1 : 0);
