// Mojo Desk, Linear and Jira behind one shape: who am I, my tickets, move one, log time.

import type { Credentials, Source, Task, TaskStatus } from "./types";

export interface Provider {
  whoami(): Promise<string>;
  /** Your open tickets, plus the ones finished in the last week (so finishing them elsewhere counts). */
  list(): Promise<Task[]>;
  setStatus(t: Task, to: TaskStatus): Promise<void>;
  /** Log focus time where the service keeps time (Jira). */
  logTime?(t: Task, minutes: number, startedAt: number): Promise<void>;
}

const TIMEOUT = 15000;
const REVIEW = /review|test|qa|verif|check/i;
const WEEK = 7 * 24 * 3600 * 1000;

async function call(name: string, url: string, init: RequestInit): Promise<unknown> {
  let r: Response;
  try {
    r = await fetch(url, { ...init, signal: AbortSignal.timeout(TIMEOUT) });
  } catch (e) {
    throw new Error(`Couldn't reach ${name} (${e instanceof Error && e.name === "TimeoutError" ? "no answer in 15 s" : "network error"}).`);
  }
  if (r.status === 401 || r.status === 403) throw new Error(`${name} didn't accept the key (HTTP ${r.status}). Check it and connect again.`);
  if (r.status === 429) throw new Error(`${name} asked us to slow down. Trying again later.`);
  if (!r.ok) throw new Error(`${name} answered HTTP ${r.status}.`);
  if (r.status === 204) return null;
  const text = await r.text();
  return text ? JSON.parse(text) : null;
}

const str = (v: unknown, max = 200): string => (typeof v === "string" ? v : v == null ? "" : String(v)).slice(0, max);
const day = (v: unknown): string | null => (typeof v === "string" && /^\d{4}-\d{2}-\d{2}/.test(v) ? v.slice(0, 10) : null);
const trim = (u: string) => u.replace(/\/+$/, "");
/** Links shown to you: https only (a service could send anything). */
const safeUrl = (u: unknown): string => {
  const s = str(u, 300);
  try {
    return new URL(s).protocol === "https:" ? s : "";
  } catch {
    return "";
  }
};
/** Where keys are sent: https, or plain http only to this machine (tests, a local server). */
export function keyUrlOk(u: string): boolean {
  try {
    const x = new URL(u);
    return x.protocol === "https:" || (x.protocol === "http:" && ["localhost", "127.0.0.1", "[::1]"].includes(x.hostname));
  } catch {
    return false;
  }
}

// ------------------------------------------------------------------ Mojo Desk

type MojoIssue = Record<string, unknown>;

export function mojo(c: NonNullable<Credentials["mojo"]>): Provider {
  const base = trim(c.url || "https://desk.mojolayers.com");
  const headers = { authorization: `Bearer ${c.token}`, "content-type": "application/json", "user-agent": "deskling-relay/1" };
  const prio: Record<string, Task["priority"]> = { urgent: 4, high: 3, medium: 2, low: 1 };
  const status: Record<string, TaskStatus> = { backlog: "todo", todo: "todo", progress: "progress", review: "review", done: "done" };
  const toTask = (i: MojoIssue): Task => {
    const est = ["estDevMin", "estTestMin", "estReviewMin", "estOtherMin"].reduce((t, k) => t + (typeof i[k] === "number" ? (i[k] as number) : 0), 0);
    const key = str(i.key, 24);
    return {
      id: "mojo:" + key,
      src: "mojo",
      key,
      title: str(i.title),
      status: status[str(i.status)] ?? "todo",
      priority: prio[str(i.priority)] ?? 0,
      estMin: est || null,
      due: day(i.dueOn),
      url: `${base}/issues/${encodeURIComponent(key)}`,
      project: str(i.project, 24).toUpperCase(),
      // Mojo Desk doesn't say when a ticket was finished (updatedAt moves with every comment), so
      // a finish counts when the relay saw it open first.
    };
  };
  return {
    async whoami() {
      const me = (await call("Mojo Desk", `${base}/api/v1/me`, { headers })) as Record<string, unknown>;
      return str(me?.name || me?.email, 80);
    },
    async list() {
      const today = new Date().toISOString().slice(0, 10);
      const q = (await call("Mojo Desk", `${base}/api/v1/me/queue?date=${today}`, { headers })) as Record<string, MojoIssue[]>;
      const all = [...(q?.progress ?? []), ...(q?.todo ?? []), ...(q?.waiting ?? []), ...(q?.done ?? [])];
      return all.filter((i) => i && i.key && !["duplicate", "wontdo", "merged"].includes(str(i.resolution))).map(toTask);
    },
    async setStatus(t, to) {
      await call("Mojo Desk", `${base}/api/v1/issues/${encodeURIComponent(t.key)}`, { method: "PATCH", headers, body: JSON.stringify({ status: to }) });
    },
  };
}

// --------------------------------------------------------------------- Linear

export function linear(c: NonNullable<Credentials["linear"]>): Provider {
  const url = c.url || "https://api.linear.app/graphql";
  // Personal API keys go in as they are, no "Bearer".
  const headers = { authorization: c.token, "content-type": "application/json" };
  const gql = async (query: string, variables: Record<string, unknown> = {}) => {
    const r = (await call("Linear", url, { method: "POST", headers, body: JSON.stringify({ query, variables }) })) as { data?: Record<string, unknown>; errors?: { message: string }[] };
    if (r?.errors?.length) throw new Error(`Linear: ${str(r.errors[0].message, 160)}`);
    return r?.data ?? {};
  };
  // Linear: 0 none, 1 urgent, 2 high, 3 medium, 4 low.
  const prio = (p: unknown): Task["priority"] => (([0, 4, 3, 2, 1] as const)[typeof p === "number" ? p : 0] ?? 0);
  return {
    async whoami() {
      const d = (await gql("query { viewer { name email } }")) as { viewer?: { name?: string; email?: string } };
      return str(d.viewer?.name || d.viewer?.email, 80);
    },
    async list() {
      const since = new Date(Date.now() - WEEK).toISOString();
      const d = (await gql(
        `query Mine($since: DateTimeOrDuration) {
          viewer { assignedIssues(first: 60, orderBy: updatedAt, filter: { or: [
            { state: { type: { nin: ["completed", "canceled"] } } },
            { completedAt: { gt: $since } } ] }) {
            nodes { id identifier title priority estimate dueDate url completedAt state { type name } team { key } } } } }`,
        { since },
      )) as { viewer?: { assignedIssues?: { nodes?: Record<string, unknown>[] } } };
      const out: Task[] = [];
      for (const n of d.viewer?.assignedIssues?.nodes ?? []) {
        const st = (n.state ?? {}) as { type?: string; name?: string };
        if (st.type === "canceled") continue;
        const status: TaskStatus =
          st.type === "completed" ? "done" : st.type === "started" ? (REVIEW.test(st.name ?? "") ? "review" : "progress") : "todo";
        out.push({
          id: "linear:" + str(n.id, 64),
          src: "linear",
          key: str(n.identifier, 24),
          title: str(n.title),
          status,
          priority: prio(n.priority),
          estMin: typeof n.estimate === "number" && n.estimate > 0 ? n.estimate * 60 : null, // a point is an hour
          due: day(n.dueDate),
          url: safeUrl(n.url),
          project: str((n.team as { key?: string })?.key, 24),
          ...(status === "done" ? { doneAt: Date.parse(str(n.completedAt)) || Date.now() } : {}),
        });
      }
      return out;
    },
    async setStatus(t, to) {
      const id = t.id.slice("linear:".length);
      const d = (await gql(`query States($id: String!) { issue(id: $id) { team { states { nodes { id name type position } } } } }`, { id })) as {
        issue?: { team?: { states?: { nodes?: { id: string; name: string; type: string; position: number }[] } } };
      };
      const states = [...(d.issue?.team?.states?.nodes ?? [])].sort((a, b) => a.position - b.position);
      const started = states.filter((s) => s.type === "started");
      const pick =
        to === "done"
          ? states.find((s) => s.type === "completed")
          : to === "review"
            ? (started.find((s) => REVIEW.test(s.name)) ?? started[started.length - 1])
            : to === "progress"
              ? (started.find((s) => !REVIEW.test(s.name)) ?? started[0])
              : (states.find((s) => s.type === "unstarted") ?? states.find((s) => s.type === "backlog"));
      if (!pick) throw new Error(`Linear: this team has no state for "${to}".`);
      await gql(`mutation Move($id: String!, $state: String!) { issueUpdate(id: $id, input: { stateId: $state }) { success } }`, { id, state: pick.id });
    },
  };
}

// ----------------------------------------------------------------------- Jira

export function jira(c: NonNullable<Credentials["jira"]>): Provider {
  const site = trim(/^https?:\/\//.test(c.site) ? c.site : `https://${c.site}`);
  const headers = { authorization: "Basic " + btoa(`${c.email}:${c.token}`), accept: "application/json", "content-type": "application/json" };
  const prio = (name: string): Task["priority"] =>
    /highest|blocker|critical|urgent/i.test(name) ? 4 : /high/i.test(name) ? 3 : /medium|normal/i.test(name) ? 2 : /low/i.test(name) ? 1 : 0;
  const search = async (jql: string, max: number) => {
    const q = new URLSearchParams({ jql, maxResults: String(max), fields: "summary,status,priority,duedate,timeoriginalestimate,project,statuscategorychangedate" });
    const r = (await call("Jira", `${site}/rest/api/3/search/jql?${q}`, { headers })) as { issues?: Record<string, unknown>[] };
    return r?.issues ?? [];
  };
  const toTask = (i: Record<string, unknown>): Task => {
    const f = (i.fields ?? {}) as Record<string, unknown>;
    const st = (f.status ?? {}) as { name?: string; statusCategory?: { key?: string } };
    const cat = st.statusCategory?.key;
    const status: TaskStatus = cat === "done" ? "done" : cat === "indeterminate" ? (REVIEW.test(st.name ?? "") ? "review" : "progress") : "todo";
    const key = str(i.key, 24);
    return {
      id: "jira:" + key,
      src: "jira",
      key,
      title: str(f.summary),
      status,
      priority: prio(str((f.priority as { name?: string })?.name)),
      estMin: typeof f.timeoriginalestimate === "number" ? Math.round(f.timeoriginalestimate / 60) : null,
      due: day(f.duedate),
      url: `${site}/browse/${encodeURIComponent(key)}`,
      project: str((f.project as { key?: string })?.key, 24),
      ...(status === "done" ? { doneAt: Date.parse(str(f.statuscategorychangedate)) || Date.now() } : {}),
    };
  };
  return {
    async whoami() {
      const me = (await call("Jira", `${site}/rest/api/3/myself`, { headers })) as Record<string, unknown>;
      return str(me?.displayName || me?.emailAddress, 80);
    },
    async list() {
      const open = await search("assignee = currentUser() AND statusCategory != Done ORDER BY priority DESC, updated DESC", 50);
      // By when it moved to Done (not "resolved": workflows without a resolution never set that).
      const done = await search("assignee = currentUser() AND statusCategory = Done AND statusCategoryChangedDate >= -7d ORDER BY statusCategoryChangedDate DESC", 30);
      return [...open, ...done].map(toTask);
    },
    async setStatus(t, to) {
      const r = (await call("Jira", `${site}/rest/api/3/issue/${encodeURIComponent(t.key)}/transitions`, { headers })) as {
        transitions?: { id: string; name: string; to?: { name?: string; statusCategory?: { key?: string } } }[];
      };
      const ts = r?.transitions ?? [];
      const cat = (x: (typeof ts)[number]) => x.to?.statusCategory?.key;
      const named = (x: (typeof ts)[number]) => `${x.name} ${x.to?.name ?? ""}`;
      const pick =
        to === "done"
          ? ts.find((x) => cat(x) === "done")
          : to === "review"
            ? (ts.find((x) => cat(x) === "indeterminate" && REVIEW.test(named(x))) ?? ts.find((x) => cat(x) === "indeterminate"))
            : to === "progress"
              ? (ts.find((x) => cat(x) === "indeterminate" && !REVIEW.test(named(x))) ?? ts.find((x) => cat(x) === "indeterminate"))
              : ts.find((x) => cat(x) === "new");
      if (!pick) throw new Error(`Jira: ${t.key} can't move to "${to}" from where it is.`);
      await call("Jira", `${site}/rest/api/3/issue/${encodeURIComponent(t.key)}/transitions`, { method: "POST", headers, body: JSON.stringify({ transition: { id: pick.id } }) });
    },
    async logTime(t, minutes, startedAt) {
      // Jira wants "2026-10-27T10:00:00.000+0000".
      const started = new Date(startedAt).toISOString().replace("Z", "+0000");
      await call("Jira", `${site}/rest/api/3/issue/${encodeURIComponent(t.key)}/worklog`, {
        method: "POST",
        headers,
        body: JSON.stringify({ timeSpentSeconds: Math.max(60, Math.round(minutes) * 60), started, comment: { type: "doc", version: 1, content: [{ type: "paragraph", content: [{ type: "text", text: "Focus block on Deskling" }] }] } }),
      });
    },
  };
}

export function provider(kind: Source, creds: Credentials): Provider | null {
  if (kind === "mojo" && creds.mojo) return mojo(creds.mojo);
  if (kind === "linear" && creds.linear) return linear(creds.linear);
  if (kind === "jira" && creds.jira) return jira(creds.jira);
  return null;
}

export const SOURCE_NAME: Record<Source, string> = { mojo: "Mojo Desk", linear: "Linear", jira: "Jira" };
