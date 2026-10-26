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

// Questions (AskUserQuestion via PreToolUse).
const question = hook(
  {
    hook_event_name: "PreToolUse",
    tool_name: "AskUserQuestion",
    tool_input: { questions: [{ question: "Which database?", header: "DB", multiSelect: false, options: [{ label: "Postgres", description: "" }, { label: "SQLite", description: "" }] }] },
  },
  "?wait=10",
);
s = await waitFor((m) => m.pending.some((p) => p.kind === "question"));
const q = s.pending.find((p) => p.kind === "question");
check("question shows with its options", q.questions[0].options.length === 2 && q.questions[0].question === "Which database?");
ws.send(JSON.stringify({ type: "answer", id: q.id, answers: { "Which database?": "SQLite" } }));
const qOut = await (await question).json();
check("answer reaches Claude", qOut?.hookSpecificOutput?.permissionDecision === "deny" && qOut.hookSpecificOutput.permissionDecisionReason.includes("SQLite"));

// Messages from the screen: queued, then delivered when Claude stops.
await hook({ hook_event_name: "UserPromptSubmit", prompt: "Write the docs" });
ws.send(JSON.stringify({ type: "message", agentId: "s-smoke", text: "also add a FAQ section" }));
s = await waitFor((m) => m.agents.find((a) => a.id === "s-smoke")?.chat?.some((c) => c.status === "queued"));
check("message queues on the screen", true);
const stopRes = await (await hook({ hook_event_name: "Stop", last_assistant_message: "Docs written." })).json();
check("stop delivers queued message and keeps Claude going", stopRes.decision === "block" && stopRes.reason.includes("FAQ"));
s = await waitFor((m) => m.agents.find((a) => a.id === "s-smoke")?.chat?.some((c) => c.status === "delivered"));
check("message marked delivered", true);
ws.send(JSON.stringify({ type: "message", agentId: "s-smoke", text: "and run the tests" }));
await waitFor((m) => m.agents.find((a) => a.id === "s-smoke")?.chat?.some((c) => c.status === "queued" && c.text === "and run the tests"));
const upRes = await (await hook({ hook_event_name: "UserPromptSubmit", prompt: "ship it" })).json();
check("queued message rides along with your next prompt", upRes?.hookSpecificOutput?.additionalContext?.includes("run the tests"));

// Channel: a live channel takes messages straight away instead of the Stop hook.
const reg = await (await fetch(`${URL_BASE}/channel/register`, { method: "POST", headers: auth, body: JSON.stringify({ host: "test-mac", cwd: "/Users/joe/code/web-app", os: "mac" }) })).json();
check("channel links to the session on the same machine + folder", reg.agentId === "s-smoke", JSON.stringify(reg));
const poll = fetch(`${URL_BASE}/channel/poll?cid=${reg.cid}&wait=10`, { headers: auth }).then((r) => r.json());
await new Promise((r) => setTimeout(r, 300));
ws.send(JSON.stringify({ type: "message", agentId: "s-smoke", text: "hello over the channel" }));
const polled = await poll;
check("channel poll receives the message", polled.messages?.[0] === "hello over the channel", JSON.stringify(polled));
await fetch(`${URL_BASE}/channel/reply`, { method: "POST", headers: auth, body: JSON.stringify({ cid: reg.cid, text: "On it!" }) });
s = await waitFor((m) => m.agents.find((a) => a.id === "s-smoke")?.chat?.some((c) => c.from === "agent" && c.text === "On it!"));
check("Claude's channel reply shows in the chat", true);

// Cursor.
const cursorHook = (body, q = "") =>
  fetch(`${URL_BASE}/hook${q}`, { method: "POST", headers: { ...auth, "x-deskling-host": "win-pc", "x-deskling-os": "windows", "x-deskling-source": "cursor" }, body: JSON.stringify({ conversation_id: "c1", workspace_roots: ["C:\\code\\styles"], ...body }) });
const cs = await (await cursorHook({ hook_event_name: "beforeSubmitPrompt", prompt: "fix the navbar" })).json();
s = await waitFor((m) => m.agents.some((a) => a.id === "cursor-c1" && a.source === "cursor" && a.name === "styles"));
check("cursor chat shows up as an agent", cs.continue === true);
const cShell = cursorHook({ hook_event_name: "beforeShellExecution", command: "npm test", cwd: "C:\\code\\styles" }, "?approve=1&wait=10");
s = await waitFor((m) => m.pending.some((p) => p.source === "cursor"));
ws.send(JSON.stringify({ type: "decision", id: s.pending.find((p) => p.source === "cursor").id, decision: "allow" }));
const cOut = await (await cShell).json();
check("cursor shell approval", cOut.permission === "allow");
await cursorHook({ hook_event_name: "stop", status: "completed" });
s = await waitFor((m) => m.agents.find((a) => a.id === "cursor-c1")?.state === "done");
check("cursor stop marks done", true);

// Custom agents via the API.
await fetch(`${URL_BASE}/api/event`, { method: "POST", headers: auth, body: JSON.stringify({ kind: "status", id: "nightly", name: "Nightly digest", state: "working", task: "Reading inboxes", source: "custom" }) });
s = await waitFor((m) => m.agents.some((a) => a.id === "nightly"));
check("custom agent status", s.agents.find((a) => a.id === "nightly").task === "Reading inboxes");

// The transcript: words as they're written, thoughts, steps, pictures, model and context size.
const png = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";
const lines = [
  { type: "assistant", uuid: "u1", message: { model: "claude-opus-5-5", usage: { input_tokens: 10, cache_read_input_tokens: 41000, output_tokens: 5 }, content: [{ type: "thinking", thinking: "Checking the login form.\nLooking for the reset link." }, { type: "text", text: "Found it: **reset** link is missing." }, { type: "tool_use", name: "Edit", input: { file_path: "/a/Login.tsx" } }] } },
  { type: "user", uuid: "u2", message: { content: [{ type: "tool_result", content: [{ type: "image", source: { type: "base64", media_type: "image/png", data: png } }] }] } },
  { type: "assistant", uuid: "u3", isSidechain: true, message: { content: [{ type: "text", text: "helper chatter" }] } },
];
const tr = await fetch(`${URL_BASE}/transcript?sid=s-smoke`, { method: "POST", headers: { ...auth, "content-type": "application/x-ndjson" }, body: lines.map((l) => JSON.stringify(l)).join("\n") + "\n" });
check("transcript accepted", tr.ok);
s = await waitFor((m) => m.agents.find((a) => a.id === "s-smoke")?.chat?.some((c) => c.image));
const ag = s.agents.find((a) => a.id === "s-smoke");
check("Claude's words arrive from the transcript", ag.chat.some((c) => c.from === "agent" && c.text === "Found it: reset link is missing."));
check("thoughts arrive", ag.chat.filter((c) => c.from === "thought").length === 2);
check("steps arrive", ag.chat.some((c) => c.from === "tool" && c.text === "Editing Login.tsx"));
check("helpers' own transcript lines are skipped", !ag.chat.some((c) => c.text === "helper chatter"));
check("model and context size", ag.model === "claude-opus-5-5" && ag.context === 41010);
const imgId = ag.chat.find((c) => c.image).image;
const img = await fetch(`${URL_BASE}/img/${imgId}`, { headers: auth });
check("picture can be fetched", img.ok && img.headers.get("content-type") === "image/png" && (await img.arrayBuffer()).byteLength === 70);
await fetch(`${URL_BASE}/transcript?sid=s-smoke`, { method: "POST", headers: auth, body: JSON.stringify(lines[0]) + "\n" });
await new Promise((r) => setTimeout(r, 400));
s = snapshots[snapshots.length - 1];
check("the same line twice is only shown once", s.agents.find((a) => a.id === "s-smoke").chat.filter((c) => c.from === "thought").length === 2);

// Replying to a quote, and a message you sent while Claude was working.
await fetch(`${URL_BASE}/quote?sid=s-smoke&name=ab12-excerpt_from_previous_claude_message.txt`, { method: "POST", headers: { ...auth, "content-type": "text/plain" }, body: "You can read along and approve things." });
await hook({ hook_event_name: "UserPromptSubmit", prompt: '<system-reminder id="x">tz</system-reminder>\n@"/mnt/a/ab12-excerpt_from_previous_claude_message.txt" do this too' });
s = await waitFor((m) => m.agents.find((a) => a.id === "s-smoke")?.chat?.some((c) => c.text === "do this too"));
check("a reply shows what it quotes", s.agents.find((a) => a.id === "s-smoke").chat.find((c) => c.text === "do this too").quote === "You can read along and approve things.");
await fetch(`${URL_BASE}/transcript?sid=s-smoke`, { method: "POST", headers: auth, body: JSON.stringify({ type: "queue-operation", operation: "remove", reason: "absorbed_mid_turn", content: "<system-reminder>t</system-reminder>\nalso check the docs" }) + "\n" });
s = await waitFor((m) => m.agents.find((a) => a.id === "s-smoke")?.chat?.some((c) => c.text === "also check the docs" && c.from === "you"));
check("messages you send mid-turn show up", true);

// Channel permission relay: a terminal dialog answered on the screen.
const perm = fetch(`${URL_BASE}/channel/permission`, { method: "POST", headers: auth, body: JSON.stringify({ cid: reg.cid, request_id: "abcde", tool_name: "Write", description: "Create a file", input_preview: "notes.md", wait: 10 }) }).then((r) => r.json());
s = await waitFor((m) => m.pending.some((p) => p.tool === "Write" && p.summary === "notes.md"));
ws.send(JSON.stringify({ type: "decision", id: s.pending.find((p) => p.tool === "Write").id, decision: "allow" }));
check("a channel's permission dialog is answered on the screen", (await perm).behavior === "allow");

// Starting sessions: a runner on a computer picks up the job; a bad routine is refused.
await fetch(`${URL_BASE}/runner/register`, { method: "POST", headers: auth, body: JSON.stringify({ machine: "test-mac", os: "mac", projects: [{ name: "web-app", path: "/Users/joe/code/web-app" }] }) });
s = await waitFor((m) => m.targets?.some((t) => t.id === "test-mac" && t.projects.includes("web-app")));
check("a computer with the runner is offered for new sessions", true);
const jobP = fetch(`${URL_BASE}/runner/poll?machine=test-mac`, { headers: auth }).then((r) => r.json());
await new Promise((r) => setTimeout(r, 300));
ws.send(JSON.stringify({ type: "start", target: "test-mac", project: "web-app", prompt: "add a dark mode" }));
const { job } = await jobP;
check("the runner gets the new session to start", job?.project === "/Users/joe/code/web-app" && job.prompt === "add a dark mode");
await hook({ hook_event_name: "SessionStart", session_id: "s-new" });
s = await waitFor((m) => m.agents.some((a) => a.id === "s-new"));
check("the session it starts listens for you straight away", s.agents.find((a) => a.id === "s-new").listening === true);
await hook({ hook_event_name: "SessionEnd", session_id: "s-new" });
const badRoutine = await fetch(`${URL_BASE}/api/routine`, { method: "POST", headers: auth, body: JSON.stringify({ url: "https://example.com/fire", token: "x" }) });
check("a routine URL that isn't Anthropic's is refused", badRoutine.status === 400);

// Keep listening: a finished session waits for the screen; typing on the computer lets it go.
ws.send(JSON.stringify({ type: "listen", agentId: "s-smoke", minutes: 5 }));
s = await waitFor((m) => m.agents.find((a) => a.id === "s-smoke")?.listening);
let held = hook({ hook_event_name: "Stop", last_assistant_message: "Done." }, "?hold=1&round=0");
await new Promise((r) => setTimeout(r, 800));
ws.send(JSON.stringify({ type: "message", agentId: "s-smoke", text: "now add a test" }));
const lout = await (await held).json();
check("a screen message wakes a listening session", lout.decision === "block" && lout.reason.includes("now add a test"));
held = hook({ hook_event_name: "Stop", last_assistant_message: "Test added." }, "?hold=1&round=0");
await new Promise((r) => setTimeout(r, 800));
await fetch(`${URL_BASE}/listen/release`, { method: "POST", headers: auth, body: JSON.stringify({ session_id: "s-smoke" }) });
const rel = await held;
check("typing on the computer releases it", rel.status === 204 || (await rel.text()) === "");
ws.send(JSON.stringify({ type: "listen", agentId: "s-smoke", minutes: 0 }));

// Bridges (OpenCode and others): report a session, stream a reply, ask, and collect screen messages.
const agentPost = (path, body) => fetch(`${URL_BASE}${path}`, { method: "POST", headers: auth, body: JSON.stringify(body) });
const base = { id: "oc-smoke", source: "opencode", name: "api", host: "test-mac", os: "mac" };
await agentPost("/agent/update", { ...base, state: "working", task: "Working", model: "ollama/qwen3", lines: [{ key: "u1", from: "you", text: "fix the tests" }] });
s = await waitFor((m) => m.agents.some((a) => a.id === "oc-smoke"));
let oc = s.agents.find((a) => a.id === "oc-smoke");
check("a bridged session shows up", oc.source === "opencode" && oc.state === "working" && oc.model === "ollama/qwen3");
await agentPost("/agent/update", { ...base, lines: [{ key: "p1", from: "agent", text: "Looking at" }] });
await agentPost("/agent/update", { ...base, lines: [{ key: "p1", from: "agent", text: "Looking at the failing test now." }, { key: "c1", from: "tool", text: "Reading app.test.ts" }] });
s = await waitFor((m) => m.agents.find((a) => a.id === "oc-smoke")?.chat.some((c) => c.text === "Looking at the failing test now."));
oc = s.agents.find((a) => a.id === "oc-smoke");
check("streamed text grows in place", oc.chat.filter((c) => c.from === "agent").length === 1 && oc.chat.some((c) => c.from === "tool"));
check("a bridge that isn't polling can't be messaged", oc.canMessage === false);
const bridgeAsk = agentPost("/agent/ask", { id: "oc-smoke", kind: "permission", tool: "Bash", summary: "rm -rf dist", wait: 10 });
s = await waitFor((m) => m.pending.some((p) => p.agentId === "oc-smoke"));
ws.send(JSON.stringify({ type: "decision", id: s.pending.find((p) => p.agentId === "oc-smoke").id, decision: "deny" }));
check("a bridge's permission is answered on the screen", (await (await bridgeAsk).json()).decision === "deny");
const bridgeQ = agentPost("/agent/ask", { id: "oc-smoke", kind: "question", tool: "Question", summary: "Which?", questions: [{ question: "Which?", header: "", multiSelect: false, options: [{ label: "A" }, { label: "B" }] }], wait: 10 });
s = await waitFor((m) => m.pending.some((p) => p.agentId === "oc-smoke" && p.kind === "question"));
check("a bridge's question says who asks", s.feed.some((f) => f.title === "OpenCode has a question"));
ws.send(JSON.stringify({ type: "answer", id: s.pending.find((p) => p.agentId === "oc-smoke").id, answers: { "Which?": "B" } }));
check("a bridge's question gets the answer", (await (await bridgeQ).json()).answers?.["Which?"] === "B");
const bridgePoll = fetch(`${URL_BASE}/agent/poll?ids=oc-smoke&wait=10`, { headers: auth }).then((r) => r.json());

await new Promise((r) => setTimeout(r, 500));
ws.send(JSON.stringify({ type: "message", agentId: "oc-smoke", text: "also update the docs" }));
const polledMsgs = await bridgePoll;
check("a polling bridge receives screen messages", polledMsgs.messages?.[0]?.text === "also update the docs", JSON.stringify(polledMsgs));
await agentPost("/agent/update", { ...base, lines: [{ key: "u2", from: "you", text: "also update the docs" }] });
await agentPost("/agent/update", { ...base, state: "done", task: "Finished" });
s = await waitFor((m) => m.agents.find((a) => a.id === "oc-smoke")?.state === "done");
oc = s.agents.find((a) => a.id === "oc-smoke");
check("your screen message isn't shown twice when the agent echoes it", oc.chat.filter((c) => c.text === "also update the docs").length === 1);
check("a bridged session finishing notifies", s.feed.some((f) => f.title === "OpenCode finished"));
await agentPost("/agent/update", { id: "oc-smoke", end: true });
s = await waitFor((m) => !m.agents.some((a) => a.id === "oc-smoke"));
check("a bridge can end its session", true);

await hook({ hook_event_name: "SessionEnd", reason: "other" });
s = await waitFor((m) => !m.agents.some((a) => a.id === "s-smoke"));
check("session end removes the agent", true);

ws.close();
console.log(failures ? `\n${failures} failed` : "\nall passed");
process.exit(failures ? 1 : 0);
