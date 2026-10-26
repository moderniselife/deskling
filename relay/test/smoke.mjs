// End-to-end smoke test against a running relay (default: `npm run dev`).
//   DESKLING_URL=http://127.0.0.1:8787 DESKLING_TOKEN=dev-token node test/smoke.mjs
// Node 22+ (uses the built-in fetch and WebSocket).
const URL_BASE = process.env.DESKLING_URL ?? "http://127.0.0.1:8787";
const TOKEN = process.env.DESKLING_TOKEN ?? "dev-token";
const auth = { authorization: `Bearer ${TOKEN}`, "content-type": "application/json" };
let failures = 0;

function check(name, cond, extra = "") {
  console.log(`${cond ? "PASS" : "FAIL"}  ${name}${extra ? "  " + extra : ""}`);
  if (!cond) failures++;
}

const hook = (body, query = "") =>
  fetch(`${URL_BASE}/hook${query}`, {
    method: "POST",
    headers: { ...auth, "x-deskling-host": "test-mac", "x-deskling-os": "mac" },
    body: JSON.stringify({ session_id: "s-smoke", cwd: "/Users/joe/code/web-app", ...body }),
  });

const unauth = await fetch(`${URL_BASE}/api/state`);
check("rejects requests without a token", unauth.status === 401);

const wsUrl = `${URL_BASE.replace(/^http/, "ws")}/ws?token=${encodeURIComponent(TOKEN)}`;
const ws = new WebSocket(wsUrl);
const snapshots = [];
let onSnap = null;
ws.onmessage = (e) => {
  const m = JSON.parse(e.data);
  if (m.type === "snapshot") {
    snapshots.push(m);
    onSnap?.(m);
  }
};
await new Promise((res, rej) => {
  ws.onopen = res;
  ws.onerror = rej;
});
const waitFor = (pred, ms = 3000) =>
  new Promise((res, rej) => {
    const hit = snapshots.find(pred);
    if (hit) return res(hit);
    const t = setTimeout(() => rej(new Error("timeout waiting for snapshot")), ms);
    onSnap = (m) => {
      if (pred(m)) {
        clearTimeout(t);
        onSnap = null;
        res(m);
      }
    };
  });

await waitFor((m) => m.cause.kind === "hello");
check("screen gets a hello snapshot", true);

await hook({ hook_event_name: "SessionStart", source: "startup" });
await hook({ hook_event_name: "UserPromptSubmit", prompt: "Refactor the login flow\nplease" });
let s = await waitFor((m) => m.agents.some((a) => a.id === "s-smoke" && a.state === "working"));
const agent = s.agents.find((a) => a.id === "s-smoke");
check("prompt marks the agent working", agent.task === "Refactor the login flow please" && agent.name === "web-app", JSON.stringify(agent.task));

// Permission: the hook blocks until the screen answers.
const permission = hook(
  { hook_event_name: "PermissionRequest", tool_name: "Bash", tool_input: { command: "npm run deploy -- --prod", description: "Deploy" } },
  "?wait=10",
);
s = await waitFor((m) => m.pending.length > 0);
check("permission shows on the screen", s.pending[0].summary === "npm run deploy -- --prod");
ws.send(JSON.stringify({ type: "decision", id: s.pending[0].id, decision: "allow" }));
const res = await permission;
const out = await res.json();
check("allow reaches the hook", out?.hookSpecificOutput?.decision?.behavior === "allow", JSON.stringify(out));

// Timeout falls through to the terminal prompt.
const t0 = Date.now();
const slow = await hook({ hook_event_name: "PermissionRequest", tool_name: "Write", tool_input: { file_path: "/tmp/x" } }, "?wait=1");
check("unanswered permission falls through after the wait", (await slow.text()) === "" && Date.now() - t0 >= 900);

await hook({ hook_event_name: "Stop", last_assistant_message: "Login refactor done; tests pass." });
s = await waitFor((m) => m.cause.kind === "done");
check("stop marks it done and adds to the feed", s.feed[0].title === "Claude finished");

// Reminders + alarms.
const due = Date.now() + 1500;
const r = await (await fetch(`${URL_BASE}/api/event`, { method: "POST", headers: auth, body: JSON.stringify({ kind: "reminder", title: "Stretch", at: new Date(due).toISOString(), from: "test" }) })).json();
check("reminder created", r.ok === true);
s = await waitFor((m) => m.cause.kind === "reminder_due", 6000);
check("reminder alarm fires", s.feed[0].body === "Stretch");
ws.send(JSON.stringify({ type: "reminder_done", id: r.id }));
s = await waitFor((m) => m.cause.kind === "reminder_done");
check("ticking a reminder off sticks", s.reminders.find((x) => x.id === r.id)?.done === true);

const n = await fetch(`${URL_BASE}/api/event`, { method: "POST", headers: auth, body: JSON.stringify({ kind: "notify", title: "Daily digest is ready", body: "Scheduled task" }) });
check("notify works", n.ok);

await hook({ hook_event_name: "SessionEnd", reason: "other" });
s = await waitFor((m) => !m.agents.some((a) => a.id === "s-smoke"));
check("session end removes the agent", true);

ws.close();
console.log(failures ? `\n${failures} failed` : "\nall passed");
process.exit(failures ? 1 : 0);
