// Quests end to end: a running relay connects to fake Mojo Desk, Linear and Jira servers, shows
// their tickets as quests, moves them, plans the day and pays XP.
//   DESKLING_URL=http://127.0.0.1:8787 DESKLING_TOKEN=dev-token node test/integrations.test.mjs
import { createServer } from "node:http";

const URL_BASE = process.env.DESKLING_URL ?? "http://127.0.0.1:8787";
const TOKEN = process.env.DESKLING_TOKEN ?? "dev-token";
const auth = { authorization: `Bearer ${TOKEN}`, "content-type": "application/json" };
let failures = 0;
const check = (name, ok, extra = "") => {
  console.log(`${ok ? "PASS" : "FAIL"}  ${name}${extra && !ok ? "  " + extra : ""}`);
  if (!ok) failures++;
};

// ---- the fake services
const seen = [];
const mojoIssues = [
  { key: "DKL-30", title: "Integrations", status: "progress", priority: "medium", estDevMin: 120, estTestMin: 30, project: "dkl", dueOn: null },
  { key: "DKL-31", title: "Feed overflow", status: "todo", priority: "urgent", estDevMin: 60, project: "dkl" },
  { key: "DKL-9", title: "Old one", status: "done", priority: "low", project: "dkl", updatedAt: Date.now() - 3600e3 },
];
const linearIssues = [
  { id: "lin-uuid-1", identifier: "ENG-12", title: "Linear thing", priority: 2, estimate: 2, dueDate: null, url: "https://linear.app/x/issue/ENG-12", completedAt: null, state: { type: "unstarted", name: "Todo" }, team: { key: "ENG" } },
];
const linearStates = [
  { id: "s-todo", name: "Todo", type: "unstarted", position: 1 },
  { id: "s-prog", name: "In Progress", type: "started", position: 2 },
  { id: "s-rev", name: "In Review", type: "started", position: 3 },
  { id: "s-done", name: "Done", type: "completed", position: 4 },
];
const jiraIssues = [{ key: "ABC-7", fields: { summary: "Jira bug", status: { name: "To Do", statusCategory: { key: "new" } }, priority: { name: "High" }, duedate: "2020-01-01", timeoriginalestimate: 3600, project: { key: "ABC" } } }];

const body = (req) => new Promise((res) => { let b = ""; req.on("data", (c) => (b += c)); req.on("end", () => res(b)); });
const server = createServer(async (req, res) => {
  const b = await body(req);
  seen.push({ method: req.method, url: req.url, auth: req.headers.authorization, body: b });
  const send = (code, data) => { res.writeHead(code, { "content-type": "application/json" }); res.end(data === undefined ? "" : JSON.stringify(data)); };
  const u = new URL(req.url, "http://x");
  // Mojo Desk
  if (u.pathname === "/mojo/api/v1/me") return req.headers.authorization === "Bearer mojo-key" ? send(200, { name: "Joe Shenton" }) : send(401, { error: "no" });
  if (u.pathname === "/mojo/api/v1/me/queue") return send(200, { progress: mojoIssues.filter((i) => i.status === "progress"), todo: mojoIssues.filter((i) => i.status === "todo"), waiting: mojoIssues.filter((i) => i.status === "review"), done: mojoIssues.filter((i) => i.status === "done") });
  const m = /^\/mojo\/api\/v1\/issues\/([^/]+)$/.exec(u.pathname);
  if (m && req.method === "PATCH") {
    const i = mojoIssues.find((x) => x.key === decodeURIComponent(m[1]));
    Object.assign(i, JSON.parse(b), { updatedAt: Date.now() });
    return send(200, i);
  }
  // Linear (GraphQL)
  if (u.pathname === "/linear/graphql") {
    if (req.headers.authorization !== "lin_api_key") return send(200, { errors: [{ message: "Authentication required" }] });
    const { query, variables } = JSON.parse(b);
    if (query.includes("viewer { name email }")) return send(200, { data: { viewer: { name: "Joe", email: "joe@x" } } });
    if (query.includes("assignedIssues")) return send(200, { data: { viewer: { assignedIssues: { nodes: linearIssues } } } });
    if (query.includes("states")) return send(200, { data: { issue: { team: { states: { nodes: linearStates } } } } });
    if (query.includes("issueUpdate")) {
      const st = linearStates.find((s) => s.id === variables.state);
      Object.assign(linearIssues.find((i) => i.id === variables.id).state, { type: st.type, name: st.name });
      return send(200, { data: { issueUpdate: { success: true } } });
    }
  }
  // Jira
  if (u.pathname === "/rest/api/3/myself") return req.headers.authorization === "Basic " + Buffer.from("joe@x:jira-key").toString("base64") ? send(200, { displayName: "Joe S" }) : send(401);
  if (u.pathname === "/rest/api/3/search/jql") return send(200, { issues: u.searchParams.get("jql").includes("!= Done") ? jiraIssues.filter((i) => i.fields.status.statusCategory.key !== "done") : jiraIssues.filter((i) => i.fields.status.statusCategory.key === "done") });
  if (u.pathname === "/rest/api/3/issue/ABC-7/transitions") {
    if (req.method === "GET") return send(200, { transitions: [{ id: "11", name: "Start", to: { name: "In Progress", statusCategory: { key: "indeterminate" } } }, { id: "31", name: "Done", to: { name: "Done", statusCategory: { key: "done" } } }] });
    const t = JSON.parse(b).transition.id;
    jiraIssues[0].fields.status = t === "31" ? { name: "Done", statusCategory: { key: "done" } } : { name: "In Progress", statusCategory: { key: "indeterminate" } };
    return send(204);
  }
  send(404, { error: "nope" });
});
await new Promise((r) => server.listen(0, "127.0.0.1", r));
const FAKE = `http://127.0.0.1:${server.address().port}`;

// ---- a screen
const ws = new WebSocket(`${URL_BASE.replace(/^http/, "ws")}/ws?token=${encodeURIComponent(TOKEN)}`);
let last = null;
const waiters = [];
ws.onmessage = (e) => {
  const m = JSON.parse(e.data);
  if (m.type !== "snapshot") return;
  last = m;
  for (const w of [...waiters]) if (w.pred(m)) { waiters.splice(waiters.indexOf(w), 1); w.res(m); }
};
await new Promise((r) => (ws.onopen = r));
ws.send(JSON.stringify({ type: "hello" }));
const waitFor = (pred, ms = 8000) => new Promise((res, rej) => {
  if (last && pred(last)) return res(last);
  const w = { pred, res };
  waiters.push(w);
  setTimeout(() => { if (waiters.includes(w)) { waiters.splice(waiters.indexOf(w), 1); rej(new Error("timeout")); } }, ms);
});
const post = (path, b, method = "POST") => fetch(URL_BASE + path, { method, headers: auth, body: b ? JSON.stringify(b) : undefined }).then(async (r) => ({ status: r.status, json: await r.json() }));

// The score so far (other tests may have played on this relay).
const before = await fetch(URL_BASE + "/api/state", { headers: auth }).then((x) => x.json());
const xp0 = before.quests?.game?.xp ?? 0;
const seq0 = before.quests?.game?.seq ?? 0;

// ---- connect
let r = await post("/api/integrations", { kind: "mojo", url: FAKE + "/mojo", token: "wrong" });
check("a wrong key is refused with a reason", r.status === 400 && /didn't accept the key/.test(r.json.error), JSON.stringify(r.json));
r = await post("/api/integrations", { kind: "mojo", url: FAKE + "/mojo", token: "mojo-key", tz: "Australia/Sydney" });
check("Mojo Desk connects and says whose it is", r.status === 200 && r.json.account === "Joe Shenton", JSON.stringify(r.json));
r = await post("/api/integrations", { kind: "linear", url: FAKE + "/linear/graphql", token: "lin_api_key" });
check("Linear connects", r.status === 200 && r.json.account === "Joe");
r = await post("/api/integrations", { kind: "jira", site: FAKE, email: "joe@x", token: "jira-key" });
check("Jira connects", r.status === 200 && r.json.account === "Joe S");
const list = await fetch(URL_BASE + "/api/integrations", { headers: auth }).then((x) => x.json());
check("keys never come back out", !JSON.stringify(list).includes("mojo-key") && !JSON.stringify(list).includes("jira-key") && list.connections.length === 3);
const state = await fetch(URL_BASE + "/api/state", { headers: auth }).then((x) => x.text());
check("nor through /api/state", !state.includes("mojo-key") && !state.includes("lin_api_key") && !state.includes("jira-key"));

let s = await waitFor((m) => m.quests?.connected?.length === 3 && m.quests.tasks.length >= 4);
let Q = s.quests;
const byKey = (k) => Q.tasks.find((t) => t.key === k);
check("tickets from all three show as quests", ["DKL-30", "DKL-31", "ENG-12", "ABC-7"].every((k) => byKey(k)), Q.tasks.map((t) => t.key).join(","));
check("Linear points become hours; Jira seconds become minutes", byKey("ENG-12").est === 120 && byKey("ABC-7").est === 60);
check("urgent and overdue are bosses", byKey("DKL-31").boss && byKey("ABC-7").boss && byKey("ABC-7").overdue);
check("today is planned for you", Q.planTotal >= 4 && Q.tasks[0].planned);
check("what was already done pays nothing", Q.game.xp === xp0);

// ---- play
ws.send(JSON.stringify({ type: "quest_status", id: "mojo:DKL-31", to: "done" }));
s = await waitFor((m) => m.quests.game.events.some((e) => e.seq > seq0 && e.label === "Defeated DKL-31"));
check("finishing a quest on the screen moves it in Mojo Desk", seen.some((x) => x.method === "PATCH" && x.url.endsWith("/DKL-31") && JSON.parse(x.body).status === "done"));
check("and pays XP, as a boss", s.quests.game.xp > xp0 && s.quests.game.events.some((e) => e.kind === "boss" && e.label === "Defeated DKL-31"), JSON.stringify(s.quests.game.events));
const xp1 = s.quests.game.xp;
ws.send(JSON.stringify({ type: "quest_status", id: "linear:lin-uuid-1", to: "review" }));
await waitFor((m) => m.quests.tasks.find((t) => t.key === "ENG-12")?.status === "review");
check("Linear moves to its review state", linearIssues[0].state.name === "In Review");
ws.send(JSON.stringify({ type: "quest_status", id: "jira:ABC-7", to: "done" }));
s = await waitFor((m) => m.quests.game.events.some((e) => e.seq > seq0 && e.label === "Defeated ABC-7"));
check("Jira moves through its Done transition", seen.some((x) => x.method === "POST" && x.url.endsWith("/ABC-7/transitions") && JSON.parse(x.body).transition.id === "31"));
check("two in a row is a combo", s.quests.game.combo >= 2);

ws.send(JSON.stringify({ type: "quest_focus", id: "mojo:DKL-30", minutes: 25 }));
s = await waitFor((m) => m.quests.focus?.id === "mojo:DKL-30");
check("a focus block starts with its end time", s.quests.focus.endsAt - s.quests.focus.startedAt === 25 * 60000);
ws.send(JSON.stringify({ type: "quest_focus", stop: true }));
s = await waitFor((m) => m.quests.focus === null);
check("and can stop early", true);

ws.send(JSON.stringify({ type: "quest_snooze", id: "mojo:DKL-30" }));
s = await waitFor((m) => m.quests.tasks.find((t) => t.key === "DKL-30")?.snoozed);
check("snoozing takes a quest off today", !s.quests.tasks.find((t) => t.key === "DKL-30").planned);
ws.send(JSON.stringify({ type: "quest_plan", id: "mojo:DKL-30", on: true }));
s = await waitFor((m) => m.quests.tasks.find((t) => t.key === "DKL-30")?.planned);
check("and planning puts it back", !s.quests.tasks.find((t) => t.key === "DKL-30").snoozed);

// Done elsewhere: finish DKL-30 in "Mojo Desk" and sync.
Object.assign(mojoIssues[0], { status: "done", updatedAt: Date.now() });
const xp2 = s.quests.game.xp;
r = await post("/api/quests/sync");
check("finishing it in Mojo Desk itself pays out on the next sync", r.json.game.xp > xp2, `${xp2} -> ${r.json.game.xp}`);

// A service that breaks says so, and the others keep working.
r = await post("/api/integrations", { kind: "linear", url: FAKE + "/linear/graphql", token: "lin_api_key" });
linearIssues.length = 0;
r = await post("/api/integrations?kind=jira", null, "DELETE");
s = await waitFor((m) => m.quests.connected.length === 2);
check("disconnecting Jira removes its quests", !s.quests.tasks.some((t) => t.src === "jira"));

for (const k of ["mojo", "linear"]) await post(`/api/integrations?kind=${k}`, null, "DELETE");
ws.close();
server.close();
console.log(failures ? `\n${failures} failed` : "\nall passed");
process.exit(failures ? 1 : 0);
