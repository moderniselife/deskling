// Tests for scripts/deskling-hook.sh against a fake relay. Node 18+, no dependencies.
//   node hooks/test/hook.test.mjs
import { spawn } from "node:child_process";
import { appendFileSync, mkdtempSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import http from "node:http";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const HOOK = join(dirname(fileURLToPath(import.meta.url)), "..", "scripts", "deskling-hook.sh");
let failures = 0;
const check = (name, ok, extra = "") => {
  console.log(`${ok ? "PASS" : "FAIL"}  ${name}${ok || !extra ? "" : "  " + extra}`);
  if (!ok) failures++;
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ---- fake relay: records requests; replies come from a per-test handler
let requests = [];
let reply = () => ({ status: 200, body: "" });
const server = http.createServer((req, res) => {
  let body = "";
  req.on("data", (d) => (body += d));
  req.on("end", async () => {
    const r = { method: req.method, url: req.url, headers: req.headers, body };
    requests.push(r);
    const out = await reply(r);
    if (out === "hang") return; // never answer
    res.writeHead(out.status ?? 200, { "content-type": "application/json" });
    res.end(out.body ?? "");
  });
});
await new Promise((r) => server.listen(0, "127.0.0.1", r));
const URL_BASE = `http://127.0.0.1:${server.address().port}`;

function home() {
  const h = mkdtempSync(join(tmpdir(), "dk-hook-"));
  mkdirSync(join(h, ".deskling"));
  writeFileSync(join(h, ".deskling", "config"), `DESKLING_URL=${URL_BASE}\nDESKLING_TOKEN=test-token\nDESKLING_NAME=test-mac\n`);
  return h;
}

function run(args, input, env = {}, timeoutMs = 15000) {
  return new Promise((resolve) => {
    const p = spawn("bash", [HOOK, ...args], { env: { PATH: process.env.PATH, ...env } });
    let out = "";
    p.stdout.on("data", (d) => (out += d));
    const t = setTimeout(() => p.kill("SIGKILL"), timeoutMs);
    p.on("exit", (code) => {
      clearTimeout(t);
      resolve({ code, out });
    });
    p.stdin.end(input);
  });
}

const line = (o) => JSON.stringify(o) + "\n";

// 1. Not configured: silent, no requests.
{
  requests = [];
  const h = mkdtempSync(join(tmpdir(), "dk-hook-"));
  const r = await run([], '{"session_id":"s1"}', { HOME: h });
  check("without a relay configured it exits quietly", r.code === 0 && r.out === "" && requests.length === 0);
}

// 2. Transcript streaming: whole lines only, tool output filtered out (pictures kept), offset remembered.
{
  const h = home();
  const tp = join(h, "t.jsonl");
  writeFileSync(
    tp,
    line({ type: "assistant", uuid: "a1", message: { content: [{ type: "text", text: "hello" }] } }) +
      line({ type: "user", uuid: "u1", message: { content: [{ type: "tool_result", content: "SECRET FILE CONTENTS" }] } }) +
      line({ type: "user", uuid: "u2", message: { content: [{ type: "tool_result", content: [{ type: "image", source: { type: "base64", media_type: "image/png", data: "AAAA" } }] }] } }) +
      line({ type: "attachment", uuid: "x1", attachment: { type: "file", content: "ATTACHED" } }) +
      '{"type":"assistant","uuid":"a2","message":{"content":[{"type":"text","text":"half',
  );
  requests = [];
  reply = () => ({ status: 200, body: "{}" });
  const body = JSON.stringify({ session_id: "s2", transcript_path: tp, hook_event_name: "PostToolUse" });
  await run([], body, { HOME: h });
  const up = requests.filter((r) => r.url.startsWith("/transcript"));
  check("the transcript is uploaded", up.length === 1 && up[0].url === "/transcript?sid=s2");
  const sent = up[0]?.body ?? "";
  check("Claude's words are sent", sent.includes('"text":"hello"'));
  check("tool output never leaves the computer", !sent.includes("SECRET FILE CONTENTS") && !sent.includes("ATTACHED"));
  check("pictures are kept", sent.includes('"type":"image"'));
  check("a half-written line waits", !sent.includes('"a2"'));
  const off = Number(readFileSync(join(h, ".deskling", "state", "s2.off"), "utf8"));
  const whole = readFileSync(tp, "utf8").lastIndexOf("\n") + 1;
  check("the offset stops at the last whole line", off === whole, `${off} vs ${whole}`);
  appendFileSync(tp, '"}]}}\n' + line({ type: "assistant", uuid: "a3", message: { content: [{ type: "text", text: "more" }] } }));
  requests = [];
  await run([], body, { HOME: h });
  const sent2 = requests.find((r) => r.url.startsWith("/transcript"))?.body ?? "";
  check("next time only the new lines go", sent2.includes('"a2"') && sent2.includes('"a3"') && !sent2.includes('"a1"'));
}

// 3. Headers: cloud sessions, Cowork label, project folder.
{
  const h = home();
  requests = [];
  await run([], '{"session_id":"s3","hook_event_name":"SessionStart"}', { HOME: h, CLAUDE_CODE_REMOTE: "true", CLAUDE_CODE_ENTRYPOINT: "remote_cowork", CLAUDE_PROJECT_DIR: "/home/me/web-app" });
  const r = requests.find((x) => x.url.startsWith("/hook"));
  check("cloud sessions say so", r?.headers["x-deskling-os"] === "cloud" && r.headers["x-deskling-label"] === "Cowork");
  check("the project folder names the session", r?.headers["x-deskling-project"] === "web-app");
}

// 4. Quotes go first, with their file name.
{
  const h = home();
  const q = join(h, "ab12-excerpt_from_previous_claude_message.txt");
  writeFileSync(q, "You can read along and approve things.");
  requests = [];
  reply = () => ({ status: 200, body: "{}" });
  await run(["sync"], JSON.stringify({ session_id: "s4", hook_event_name: "UserPromptSubmit", prompt: `@"${q}" do this` }), { HOME: h });
  const qi = requests.findIndex((r) => r.url.startsWith("/quote"));
  const hi = requests.findIndex((r) => r.url.startsWith("/hook"));
  check("a quoted excerpt is sent before the prompt", qi >= 0 && hi > qi && requests[qi].url.includes("name=ab12-excerpt_from_previous_claude_message.txt") && requests[qi].body.includes("read along"));
}

// 5. Screen approvals off: nothing is sent and the terminal prompt shows.
{
  const h = home();
  appendFileSync(join(h, ".deskling", "config"), "DESKLING_WAIT=0\n");
  requests = [];
  const r = await run(["wait"], '{"session_id":"s5","hook_event_name":"PermissionRequest"}', { HOME: h });
  check("with screen approvals off, wait mode asks nothing", r.out === "" && !requests.some((x) => x.url.startsWith("/hook")));
}

// 6. Keep listening: the hook asks again until a message comes.
{
  const h = home();
  requests = [];
  let n = 0;
  reply = (r) => (r.url.startsWith("/hook") ? (++n < 3 ? { body: '{"again":true}' } : { body: '{"decision":"block","reason":"from the screen"}' }) : { body: "{}" });
  const r = await run(["stop"], '{"session_id":"s6","hook_event_name":"Stop"}', { HOME: h });
  const rounds = requests.filter((x) => x.url.startsWith("/hook")).map((x) => x.url);
  check("listening asks again each round", rounds.join(",") === "/hook?hold=1&round=0,/hook?hold=1&round=1,/hook?hold=1&round=2");
  check("a screen message becomes the decision", r.out.includes('"decision":"block"'));
}

// 7. Typing in the app while listening lets go straight away.
{
  const h = home();
  const tp = join(h, "t.jsonl");
  writeFileSync(tp, line({ type: "assistant", uuid: "b1", message: { content: [{ type: "text", text: "done" }] } }));
  requests = [];
  reply = (r) => (r.url.startsWith("/hook") ? "hang" : { body: "{}" });
  const started = Date.now();
  const p = run(["stop"], JSON.stringify({ session_id: "s7", transcript_path: tp, hook_event_name: "Stop" }), { HOME: h });
  await sleep(1500);
  appendFileSync(tp, line({ type: "queue-operation", operation: "enqueue", sessionId: "s7" }));
  const r = await p;
  const took = Date.now() - started;
  check("typing in the app releases a listening session", r.out === "" && took < 6000, `${took} ms`);
  check("the relay is told to let go", requests.some((x) => x.url === "/listen/release" && x.body.includes('"s7"')));
}

server.close();
console.log(failures ? `\n${failures} failed` : "\nall passed");
process.exit(failures ? 1 : 0);
