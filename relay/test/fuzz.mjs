// Throws odd and broken requests at a running relay, then checks it still answers and its
// snapshot is still sound. Pairs with the simulator's chaos test on the screen side.
//   DESKLING_URL=http://127.0.0.1:8787 DESKLING_TOKEN=dev-token node test/fuzz.mjs [rounds]
const URL_BASE = process.env.DESKLING_URL ?? "http://127.0.0.1:8787";
const TOKEN = process.env.DESKLING_TOKEN ?? "dev-token";
const ROUNDS = Number(process.argv[2] ?? 300);
const auth = { authorization: `Bearer ${TOKEN}` };
let failures = 0;
const check = (name, ok, extra = "") => {
  console.log(`${ok ? "PASS" : "FAIL"}  ${name}${ok || !extra ? "" : "  " + extra}`);
  if (!ok) failures++;
};

let seed = 4242;
const rnd = (n) => ((seed = (seed * 1103515245 + 12345) >>> 0) >>> 8) % n;
const pick = (a) => a[rnd(a.length)];
const junk = () =>
  pick([
    null,
    undefined,
    0,
    -1,
    1e309,
    true,
    "",
    "x".repeat(rnd(5000)),
    "é漢😀\u0000‮<script>",
    [],
    [1, 2, 3],
    {},
    { a: { b: { c: [] } } },
    "../../etc/passwd",
    "__proto__",
  ]);
const ids = ["s1", "s2", "oc-1", "oc-2", "__proto__", "constructor", "", "x".repeat(200)];
const sources = ["claude-code", "opencode", "cursor", "custom", "nope", junk()];

function body() {
  switch (rnd(9)) {
    case 0:
      return ["/hook", { hook_event_name: pick(["SessionStart", "UserPromptSubmit", "PreToolUse", "PermissionRequest", "Stop", "SessionEnd", "SubagentStart", "Notification", junk()]), session_id: pick(ids), cwd: junk(), prompt: junk(), tool_name: junk(), tool_input: junk(), last_assistant_message: junk() }, pick(["?wait=1", "?live=1", "?wait=1&hold=1&round=0"])];
    case 1:
      return ["/agent/update", { id: pick(ids), source: pick(sources), name: junk(), host: junk(), state: pick(["working", "done", "needs_you", junk()]), task: junk(), model: junk(), context: junk(), helpers: pick([junk(), [{ id: junk(), name: junk(), done: junk() }]]), lines: pick([junk(), Array.from({ length: rnd(50) }, () => ({ key: pick([junk(), "k" + rnd(5)]), from: pick(["you", "agent", "tool", "thought", junk()]), text: junk(), image: pick([undefined, "data:image/png;base64,!!!", junk()]) }))]), end: rnd(10) === 0 }];
    case 2:
      return ["/agent/ask", { id: pick(ids), kind: pick(["permission", "question", junk()]), tool: junk(), summary: junk(), questions: pick([junk(), [{ question: junk(), options: junk() }]]), wait: 0 }];
    case 3:
      return ["/api/event", { kind: pick(["notify", "reminder", "status", junk()]), title: junk(), body: junk(), at: junk(), id: pick(ids), state: junk(), level: junk() }];
    case 4:
      return ["/runner/register", { machine: junk(), projects: pick([junk(), [{ name: junk(), path: junk() }]]), engines: pick([junk(), [{ id: junk(), name: junk(), models: junk() }]]) }];
    case 5:
      return ["/transcript?sid=" + encodeURIComponent(pick(ids)), "raw"];
    case 6:
      return ["/quote?sid=" + encodeURIComponent(pick(ids)) + "&name=" + encodeURIComponent(String(junk())), "raw"];
    case 7:
      return ["/agent/resolve", { id: pick(ids), key: junk() }];
    default:
      return ["/channel/register", { host: junk(), cwd: junk(), name: junk() }];
  }
}

let serverErrors = 0;
const slow = [];
// In batches, like many agents at once (requests that wait for an answer time out after a few s).
async function one() {
  const [path, b, q = ""] = body();
  const raw = b === "raw" ? pick(["{not json", "", "\n\n{}\n", JSON.stringify({ type: "assistant", uuid: "u" + rnd(9), message: { content: junk() } }) + "\n"]) : rnd(15) === 0 ? "{broken" : JSON.stringify(b);
  const t0 = Date.now();
  const res = await fetch(URL_BASE + path + q, { method: "POST", headers: { ...auth, "content-type": "application/json" }, body: raw, signal: AbortSignal.timeout(20000) }).catch((e) => (e.name === "TimeoutError" ? "slow" : null));
  if (res === "slow") {
    slow.push(path + q + " " + raw.slice(0, 120));
    return;
  }
  if (!res || res.status >= 500) {
    serverErrors++;
    if (serverErrors <= 3) console.log("500 from", path, raw.slice(0, 200));
  }
}
for (let i = 0; i < ROUNDS; i += 25) await Promise.all(Array.from({ length: Math.min(25, ROUNDS - i) }, one));
check(`${ROUNDS} odd requests without a server error`, serverErrors === 0, `${serverErrors} errors`);
check("none of them hang", !slow.length, slow.slice(0, 3).join(" | "));

const state = await fetch(URL_BASE + "/api/state", { headers: auth });
check("the relay still answers", state.ok);
const ws = new WebSocket(`${URL_BASE.replace(/^http/, "ws")}/ws?token=${encodeURIComponent(TOKEN)}`);
const snap = await new Promise((res) => {
  ws.onmessage = (e) => res(JSON.parse(e.data));
  setTimeout(() => res(null), 5000);
});
ws.close();
const sound =
  snap &&
  Array.isArray(snap.agents) &&
  snap.agents.every((a) => typeof a.id === "string" && typeof a.name === "string" && Array.isArray(a.chat) && a.chat.every((c) => typeof c.text === "string")) &&
  Array.isArray(snap.pending) &&
  Array.isArray(snap.targets);
check("the screen's snapshot is still well formed", sound);
// Tidy up what the fuzzing left behind.
for (const id of ids) await fetch(URL_BASE + "/agent/update", { method: "POST", headers: { ...auth, "content-type": "application/json" }, body: JSON.stringify({ id, end: true }) }).catch(() => {});
console.log(failures ? `\n${failures} failed` : "\nall passed");
process.exit(failures ? 1 : 0);
