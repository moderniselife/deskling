import { DurableObject } from "cloudflare:workers";
import type { Agent, AgentState, Cause, Decision, FeedItem, FeedLevel, Pending, Reminder, State } from "./types";

const FEED_MAX = 40;
const AGENT_STALE_MS = 12 * 60 * 60 * 1000; // forget sessions idle for 12 h
const REMINDER_KEEP_DONE_MS = 24 * 60 * 60 * 1000;
const DEFAULT_WAIT_S = 30;
const MAX_WAIT_S = 110;

function emptyState(): State {
  return { agents: {}, feed: [], reminders: [], pending: [], focus: false, pingWhenDone: false };
}

function shortId(): string {
  return crypto.randomUUID().replace(/-/g, "").slice(0, 10);
}

function oneLine(s: unknown, max = 90): string {
  if (typeof s !== "string") return "";
  const flat = s.replace(/\s+/g, " ").trim();
  return flat.length > max ? flat.slice(0, max - 1) + "…" : flat;
}

function baseName(path: unknown): string {
  if (typeof path !== "string" || !path) return "claude";
  const parts = path.replace(/[\\/]+$/, "").split(/[\\/]/);
  return parts[parts.length - 1] || "claude";
}

function toolSummary(tool: string, input: Record<string, unknown> | undefined): { summary: string; detail: string } {
  const i = input ?? {};
  const str = (k: string) => (typeof i[k] === "string" ? (i[k] as string) : "");
  if (tool === "Bash" || tool === "PowerShell") return { summary: oneLine(str("command"), 160), detail: oneLine(str("description"), 120) };
  if (["Edit", "Write", "MultiEdit", "Read", "NotebookEdit"].includes(tool))
    return { summary: oneLine(str("file_path") || str("notebook_path"), 160), detail: tool === "Write" ? "Create or overwrite a file" : "Edit a file" };
  if (tool === "WebFetch") return { summary: oneLine(str("url"), 160), detail: oneLine(str("prompt"), 120) };
  if (tool === "WebSearch") return { summary: oneLine(str("query"), 160), detail: "Search the web" };
  let json = "";
  try {
    json = JSON.stringify(i);
  } catch {
    json = "";
  }
  return { summary: oneLine(json, 160), detail: "" };
}

function parseDue(at: unknown): number | null {
  if (typeof at === "number" && Number.isFinite(at)) return at < 1e12 ? at * 1000 : at;
  if (typeof at === "string" && at) {
    const t = Date.parse(at);
    if (!Number.isNaN(t)) return t;
  }
  return null;
}

function json(data: unknown, status = 200): Response {
  return new Response(JSON.stringify(data), { status, headers: { "content-type": "application/json" } });
}

export class Hub extends DurableObject<Record<string, unknown>> {
  private state: State = emptyState();
  private waiters = new Map<string, (d: Decision) => void>();

  constructor(ctx: DurableObjectState, env: Record<string, unknown>) {
    super(ctx, env);
    ctx.blockConcurrencyWhile(async () => {
      const saved = await ctx.storage.get<State>("state");
      if (saved) this.state = { ...emptyState(), ...saved, pending: [] };
    });
  }

  // ---------------------------------------------------------------- routing

  async fetch(req: Request): Promise<Response> {
    const url = new URL(req.url);
    const path = url.pathname;

    if (path === "/ws") {
      if (req.headers.get("upgrade")?.toLowerCase() !== "websocket") return new Response("expected websocket", { status: 426 });
      const pair = new WebSocketPair();
      const [client, server] = Object.values(pair);
      this.ctx.acceptWebSocket(server);
      server.send(this.snapshot({ kind: "hello" }));
      return new Response(null, { status: 101, webSocket: client });
    }

    if (req.method === "POST" && path === "/hook") {
      let body: Record<string, unknown>;
      try {
        body = (await req.json()) as Record<string, unknown>;
      } catch {
        return json({ error: "bad json" }, 400);
      }
      const host = oneLine(req.headers.get("x-deskling-host") ?? "", 40) || "computer";
      const os = oneLine(req.headers.get("x-deskling-os") ?? "", 20) || "unknown";
      const wait = Math.min(MAX_WAIT_S, Math.max(0, Number(url.searchParams.get("wait") ?? DEFAULT_WAIT_S) || DEFAULT_WAIT_S));
      return this.onHook(body, host, os, wait);
    }

    if (req.method === "POST" && path === "/api/event") {
      let body: Record<string, unknown>;
      try {
        body = (await req.json()) as Record<string, unknown>;
      } catch {
        return json({ error: "bad json" }, 400);
      }
      return this.onApiEvent(body);
    }

    if (req.method === "POST" && path === "/api/client") {
      // Same messages the screen sends over the WebSocket, for testing with curl.
      const body = await req.json().catch(() => null);
      if (!body) return json({ error: "bad json" }, 400);
      await this.onClientMessage(body as Record<string, unknown>);
      return json({ ok: true });
    }

    if (req.method === "GET" && path === "/api/state") {
      this.prune();
      return json(this.state);
    }

    return json({ error: "not found" }, 404);
  }

  // ------------------------------------------------------ Claude Code hooks

  private async onHook(body: Record<string, unknown>, host: string, os: string, wait: number): Promise<Response> {
    const event = String(body.hook_event_name ?? "");
    const sid = String(body.session_id ?? "") || `${host}-unknown`;
    const name = baseName(body.cwd);
    const now = Date.now();

    if (event === "SessionEnd") {
      delete this.state.agents[sid];
      this.state.pending = this.state.pending.filter((p) => p.agentId !== sid);
      await this.commit({ kind: "agent", id: sid });
      return new Response("");
    }

    const agent = this.upsertAgent(sid, name, host, os);

    switch (event) {
      case "SessionStart":
        this.setAgent(agent, "idle", "Ready for a prompt");
        await this.commit({ kind: "agent", id: sid });
        break;

      case "UserPromptSubmit":
        this.setAgent(agent, "working", oneLine(body.prompt) || "Working");
        await this.commit({ kind: "agent", id: sid });
        break;

      case "Notification": {
        const type = String(body.notification_type ?? "");
        const message = oneLine(body.message, 140);
        if (type === "permission_prompt") {
          // If a PermissionRequest is already open for this session the screen
          // is showing it; otherwise flag that the terminal is asking.
          if (!this.state.pending.some((p) => p.agentId === sid)) {
            this.setAgent(agent, "needs_you", message || "Waiting for permission");
            this.addFeed("alert", "Permission needed", `${name} · ${host}`);
          }
          await this.commit({ kind: "permission", id: sid });
        } else if (type === "idle_prompt") {
          this.setAgent(agent, "waiting", "Waiting for your reply");
          this.addFeed("info", "Claude is waiting for you", `${name} · ${host}`);
          await this.commit({ kind: "feed" });
        } else if (message) {
          this.addFeed("info", oneLine(body.title, 60) || "Claude Code", message);
          await this.commit({ kind: "feed" });
        }
        break;
      }

      case "Stop": {
        const summary = oneLine(body.last_assistant_message, 120);
        this.setAgent(agent, "done", summary || "Finished");
        this.addFeed("good", "Claude finished", `${name}${summary ? " · " + summary : ""}`);
        const ping = this.state.pingWhenDone;
        this.state.pingWhenDone = false;
        await this.commit({ kind: ping ? "ping" : "done", id: sid });
        break;
      }

      case "PermissionRequest":
        return this.onPermission(agent, body, wait);

      default:
        // Unknown or unhandled events still count as activity.
        agent.updated = now;
        await this.save();
    }
    return new Response("");
  }

  private async onPermission(agent: Agent, body: Record<string, unknown>, wait: number): Promise<Response> {
    const tool = String(body.tool_name ?? "Tool");
    const { summary, detail } = toolSummary(tool, body.tool_input as Record<string, unknown> | undefined);
    const now = Date.now();
    const p: Pending = {
      id: shortId(),
      agentId: agent.id,
      name: agent.name,
      host: agent.host,
      os: agent.os,
      tool,
      summary,
      detail,
      ts: now,
      expires: now + wait * 1000,
    };
    this.state.pending.push(p);
    const previous = { state: agent.state, task: agent.task };
    this.setAgent(agent, "needs_you", `Wants to use ${tool}`);
    this.addFeed("alert", "Permission needed", `${agent.name} wants to use ${tool}`);

    // Register the waiter before committing: prune() drops pending requests nobody waits on.
    const answered = new Promise<Decision | "timeout">((resolve) => {
      const timer = setTimeout(() => {
        this.waiters.delete(p.id);
        resolve("timeout");
      }, wait * 1000);
      this.waiters.set(p.id, (d) => {
        clearTimeout(timer);
        this.waiters.delete(p.id);
        resolve(d);
      });
    });
    await this.commit({ kind: "permission", id: p.id });
    const decision = await answered;

    this.state.pending = this.state.pending.filter((x) => x.id !== p.id);
    if (decision === "allow" || decision === "deny") {
      this.setAgent(agent, "working", previous.state === "working" ? previous.task : "Working");
      this.addFeed(decision === "allow" ? "good" : "info", decision === "allow" ? "Approved from Deskling" : "Denied from Deskling", `${agent.name} · ${tool}`);
    }
    // "computer" and "timeout" leave the agent flagged: the terminal prompt takes over.
    await this.commit({ kind: "permission_closed", id: p.id });

    if (decision === "allow" || decision === "deny") {
      return json({ hookSpecificOutput: { hookEventName: "PermissionRequest", decision: { behavior: decision } } });
    }
    return new Response(""); // no decision: Claude Code shows its normal prompt
  }

  // ------------------------------------------------ generic event API

  private async onApiEvent(body: Record<string, unknown>): Promise<Response> {
    const kind = String(body.kind ?? "");
    if (kind === "notify") {
      const level = (["info", "good", "alert"].includes(String(body.level)) ? body.level : "info") as FeedLevel;
      const item = this.addFeed(level, oneLine(body.title, 60) || "Notification", oneLine(body.body, 140));
      await this.commit({ kind: "feed", id: item.id });
      return json({ ok: true, id: item.id });
    }
    if (kind === "reminder") {
      const title = oneLine(body.title, 80);
      if (!title) return json({ error: "title required" }, 400);
      const r: Reminder = {
        id: shortId(),
        title,
        due: parseDue(body.at),
        from: oneLine(body.from, 30) || "API",
        done: false,
        doneAt: null,
        notified: false,
      };
      this.state.reminders.push(r);
      await this.commit({ kind: "reminder", id: r.id });
      return json({ ok: true, id: r.id });
    }
    if (kind === "status") {
      const id = oneLine(body.id, 60);
      if (!id) return json({ error: "id required" }, 400);
      const state = String(body.state ?? "working") as AgentState;
      if (!["idle", "working", "waiting", "needs_you", "done"].includes(state)) return json({ error: "bad state" }, 400);
      if (body.remove === true) {
        delete this.state.agents[id];
      } else {
        const agent = this.upsertAgent(id, oneLine(body.name, 40) || id, oneLine(body.host, 40) || "cloud", oneLine(body.os, 20) || "cloud");
        this.setAgent(agent, state, oneLine(body.task) || agent.task);
        if (state === "done") this.addFeed("good", `${agent.name} finished`, oneLine(body.task));
      }
      await this.commit({ kind: "agent", id });
      return json({ ok: true });
    }
    if (kind === "clear_reminder") {
      this.state.reminders = this.state.reminders.filter((r) => r.id !== body.id);
      await this.commit({ kind: "reminder" });
      return json({ ok: true });
    }
    return json({ error: "unknown kind (notify | reminder | status | clear_reminder)" }, 400);
  }

  // ---------------------------------------------------- screen messages

  async webSocketMessage(ws: WebSocket, message: string | ArrayBuffer): Promise<void> {
    if (typeof message !== "string") return;
    let msg: Record<string, unknown>;
    try {
      msg = JSON.parse(message);
    } catch {
      return;
    }
    if (msg.type === "hello") {
      ws.send(this.snapshot({ kind: "hello" }));
      return;
    }
    await this.onClientMessage(msg);
  }

  async webSocketClose(ws: WebSocket, code: number): Promise<void> {
    try {
      ws.close(code === 1005 ? 1000 : code, "bye");
    } catch {
      /* already closed */
    }
  }

  private async onClientMessage(msg: Record<string, unknown>): Promise<void> {
    if (msg.type === "decision") {
      const id = String(msg.id ?? "");
      const d = String(msg.decision ?? "");
      if (!["allow", "deny", "computer"].includes(d)) return;
      const waiter = this.waiters.get(id);
      if (waiter) waiter(d as Decision);
      else {
        // Nobody is waiting any more (timed out or relay restarted): just clear it.
        this.state.pending = this.state.pending.filter((p) => p.id !== id);
        await this.commit({ kind: "permission_closed", id });
      }
      return;
    }
    if (msg.type === "reminder_done") {
      const r = this.state.reminders.find((x) => x.id === msg.id);
      if (!r) return;
      r.done = msg.done !== false;
      r.doneAt = r.done ? Date.now() : null;
      await this.commit({ kind: "reminder_done", id: r.id });
      return;
    }
    if (msg.type === "action") {
      const name = String(msg.name ?? "");
      if (name === "snooze_all") {
        const until = Date.now() + 15 * 60 * 1000;
        for (const r of this.state.reminders) {
          if (!r.done && r.due !== null && r.due < until) {
            r.due = until;
            r.notified = false;
          }
        }
        this.addFeed("reminder", "Reminders snoozed", "Back in 15 minutes");
        await this.commit({ kind: "reminder" });
      } else if (name === "focus") {
        this.state.focus = typeof msg.on === "boolean" ? msg.on : !this.state.focus;
        await this.commit({ kind: "focus" });
      } else if (name === "ping_when_done") {
        this.state.pingWhenDone = true;
        this.addFeed("info", "Okay!", "I'll ping you when Claude finishes");
        await this.commit({ kind: "feed" });
      }
    }
  }

  // ------------------------------------------------------------- alarms

  async alarm(): Promise<void> {
    const now = Date.now();
    let fired: Reminder | null = null;
    for (const r of this.state.reminders) {
      if (!r.done && !r.notified && r.due !== null && r.due <= now) {
        r.notified = true;
        this.addFeed("reminder", "Reminder", r.title);
        fired = r;
      }
    }
    await this.commit(fired ? { kind: "reminder_due", id: fired.id } : { kind: "reminder" });
  }

  private async scheduleAlarm(): Promise<void> {
    const next = this.state.reminders
      .filter((r) => !r.done && !r.notified && r.due !== null)
      .reduce<number | null>((min, r) => (min === null || (r.due as number) < min ? (r.due as number) : min), null);
    if (next === null) await this.ctx.storage.deleteAlarm();
    else await this.ctx.storage.setAlarm(Math.max(next, Date.now() + 100));
  }

  // ------------------------------------------------------------ helpers

  private upsertAgent(id: string, name: string, host: string, os: string): Agent {
    const now = Date.now();
    let a = this.state.agents[id];
    if (!a) {
      a = { id, name, host, os, state: "idle", task: "", since: now, updated: now };
      this.state.agents[id] = a;
    }
    a.name = name || a.name;
    a.host = host || a.host;
    a.os = os || a.os;
    a.updated = now;
    return a;
  }

  private setAgent(a: Agent, state: AgentState, task: string): void {
    if (a.state !== state) a.since = Date.now();
    a.state = state;
    a.task = task;
  }

  private addFeed(level: FeedLevel, title: string, body: string): FeedItem {
    const item: FeedItem = { id: shortId(), level, title, body, ts: Date.now() };
    this.state.feed.unshift(item);
    if (this.state.feed.length > FEED_MAX) this.state.feed.length = FEED_MAX;
    return item;
  }

  private prune(): void {
    const now = Date.now();
    for (const [id, a] of Object.entries(this.state.agents)) {
      if (now - a.updated > AGENT_STALE_MS) delete this.state.agents[id];
    }
    this.state.reminders = this.state.reminders.filter((r) => !r.done || (r.doneAt ?? 0) > now - REMINDER_KEEP_DONE_MS);
    this.state.pending = this.state.pending.filter((p) => this.waiters.has(p.id));
  }

  private snapshot(cause: Cause): string {
    const s = this.state;
    const agents = Object.values(s.agents).sort((a, b) => b.updated - a.updated);
    const reminders = [...s.reminders].sort((a, b) => Number(a.done) - Number(b.done) || (a.due ?? Infinity) - (b.due ?? Infinity));
    return JSON.stringify({
      type: "snapshot",
      now: Date.now(),
      cause,
      agents,
      feed: s.feed.slice(0, 12),
      reminders: reminders.slice(0, 12),
      pending: s.pending,
      focus: s.focus,
      pingWhenDone: s.pingWhenDone,
    });
  }

  private async save(): Promise<void> {
    await this.ctx.storage.put("state", this.state);
  }

  /** Persist, re-arm reminders and push a fresh snapshot to every screen. */
  private async commit(cause: Cause): Promise<void> {
    this.prune();
    await this.save();
    await this.scheduleAlarm();
    const msg = this.snapshot(cause);
    for (const ws of this.ctx.getWebSockets()) {
      try {
        ws.send(msg);
      } catch {
        /* socket went away */
      }
    }
  }
}
