import { DurableObject } from "cloudflare:workers";
import { makeZip, type ZipEntry } from "./zip";
import type {
  Agent,
  AgentState,
  Answers,
  Cause,
  ChatLine,
  Decision,
  FeedItem,
  FeedLevel,
  Pending,
  Question,
  Reminder,
  Source,
  State,
} from "./types";

const FEED_MAX = 40;
const CHAT_MAX = 20;
const AGENT_STALE_MS = 12 * 60 * 60 * 1000; // forget sessions idle for 12 h
const REMINDER_KEEP_DONE_MS = 24 * 60 * 60 * 1000;
const DEFAULT_WAIT_S = 30;
const MAX_WAIT_S = 110;
const CHANNEL_LIVE_MS = 45 * 1000;
const SOURCES: Source[] = ["claude-code", "cursor", "codex", "gemini", "custom"];

type Resolution = { decision: Decision } | { answers: Answers; note: string } | { timeout: true };

interface Channel {
  id: string;
  host: string;
  name: string;
  os: string;
  agentId: string | null;
  lastPoll: number;
}

function emptyState(): State {
  return { agents: {}, feed: [], reminders: [], pending: [], chats: {}, focus: false, pingWhenDone: false };
}

function shortId(): string {
  return crypto.randomUUID().replace(/-/g, "").slice(0, 10);
}

function oneLine(s: unknown, max = 90): string {
  if (typeof s !== "string") return "";
  const flat = s.replace(/\s+/g, " ").trim();
  return flat.length > max ? flat.slice(0, max - 1) + "…" : flat;
}

function clip(s: unknown, max: number): string {
  if (typeof s !== "string") return "";
  const t = s.trim();
  return t.length > max ? t.slice(0, max - 1) + "…" : t;
}

// Where the plugin's files are published (this repo's hooks/ folder).
const PLUGIN_SOURCE = "https://raw.githubusercontent.com/moderniselife/deskling/main/hooks/";

function baseName(path: unknown): string {
  if (typeof path !== "string" || !path) return "";
  const parts = path.replace(/[\\/]+$/, "").split(/[\\/]/);
  return parts[parts.length - 1] || "";
}

function toolSummary(tool: string, input: Record<string, unknown> | undefined): { summary: string; detail: string } {
  const i = input ?? {};
  const str = (k: string) => (typeof i[k] === "string" ? (i[k] as string) : "");
  if (tool === "Bash" || tool === "PowerShell" || tool === "Shell") return { summary: oneLine(str("command"), 160), detail: oneLine(str("description"), 120) };
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

function parseQuestions(input: unknown): Question[] {
  const raw = (input as { questions?: unknown })?.questions;
  if (!Array.isArray(raw)) return [];
  return raw.slice(0, 4).map((q) => {
    const o = (q ?? {}) as Record<string, unknown>;
    const opts = Array.isArray(o.options) ? o.options : [];
    return {
      question: clip(o.question, 200),
      header: oneLine(o.header, 24),
      multiSelect: o.multiSelect === true,
      options: opts.slice(0, 6).map((x) => {
        const ox = (x ?? {}) as Record<string, unknown>;
        return { label: oneLine(ox.label, 60), description: oneLine(ox.description, 120) };
      }),
    };
  });
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

const empty = () => new Response("");

export class Hub extends DurableObject<Record<string, unknown>> {
  private state: State = emptyState();
  private waiters = new Map<string, (r: Resolution) => void>();
  private channels = new Map<string, Channel>();
  private channelPolls = new Map<string, () => void>();

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
    const readJson = async () => {
      try {
        return (await req.json()) as Record<string, unknown>;
      } catch {
        return null;
      }
    };

    if (path === "/ws") {
      if (req.headers.get("upgrade")?.toLowerCase() !== "websocket") return new Response("expected websocket", { status: 426 });
      const pair = new WebSocketPair();
      const [client, server] = Object.values(pair);
      this.ctx.acceptWebSocket(server);
      server.send(this.snapshot({ kind: "hello" }));
      return new Response(null, { status: 101, webSocket: client });
    }

    if (req.method === "POST" && path === "/hook") {
      const raw = await req.text();
      let body: Record<string, unknown> | null = null;
      try {
        body = JSON.parse(raw) as Record<string, unknown>;
      } catch {
        body = null;
      }
      if (!body) return json({ error: "bad json" }, 400);
      // The same event twice within a few seconds means two copies of the plugin are installed
      // (say the marketplace one and your personal one): only act on the first.
      if (this.seenRecently(raw)) return empty();
      const host = oneLine(req.headers.get("x-deskling-host") ?? "", 40) || "computer";
      const os = oneLine(req.headers.get("x-deskling-os") ?? "", 20) || "unknown";
      const src = (req.headers.get("x-deskling-source") ?? "claude-code").toLowerCase() as Source;
      const wait = Math.min(MAX_WAIT_S, Math.max(0, Number(url.searchParams.get("wait") ?? DEFAULT_WAIT_S) || DEFAULT_WAIT_S));
      if (src === "cursor") return this.onCursorHook(body, host, os, wait, url.searchParams.get("approve") === "1");
      const label = oneLine(req.headers.get("x-deskling-label") ?? "", 30);
      return this.onHook(body, host, os, wait, label);
    }

    if (req.method === "POST" && path === "/api/event") {
      const body = await readJson();
      if (!body) return json({ error: "bad json" }, 400);
      return this.onApiEvent(body);
    }

    if (req.method === "POST" && path === "/api/client") {
      // Same messages the screen sends over the WebSocket, for testing with curl.
      const body = await readJson();
      if (!body) return json({ error: "bad json" }, 400);
      await this.onClientMessage(body);
      return json({ ok: true });
    }

    if (req.method === "GET" && path === "/api/health") {
      this.prune();
      return json({ ok: true, screens: this.ctx.getWebSockets().length, agents: Object.keys(this.state.agents).length, pending: this.state.pending.length });
    }

    if (req.method === "GET" && path === "/plugin.zip") return this.personalPlugin(url);

    if (req.method === "GET" && path === "/api/state") {
      this.prune();
      return json(this.state);
    }

    if (req.method === "POST" && path === "/channel/register") return this.onChannelRegister((await readJson()) ?? {});
    if (req.method === "GET" && path === "/channel/poll") return this.onChannelPoll(url);
    if (req.method === "POST" && path === "/channel/reply") return this.onChannelReply((await readJson()) ?? {});

    return json({ error: "not found" }, 404);
  }

  // ------------------------------------------------------ Claude Code hooks

  // ------------------------------------------------------ personal plugin

  /**
   * The Deskling plugin with this relay's address and token built in, for the Claude app:
   * uploaded there once, it syncs to Cowork (on your computer and in the cloud) and to Claude
   * Code, none of which can be configured by hand the way a local install can.
   */
  private async personalPlugin(url: URL): Promise<Response> {
    const files: { path: string; executable?: boolean }[] = [
      { path: ".claude-plugin/plugin.json" },
      { path: ".claude-plugin/icon.png" },
      { path: "hooks/hooks.json" },
      { path: "scripts/deskling-hook.sh", executable: true },
      { path: "scripts/deskling-hook.ps1" },
      { path: "scripts/deskling", executable: true },
      { path: "channel/deskling-channel.mjs" },
      { path: "README.md" },
    ];
    const entries: ZipEntry[] = [];
    for (const f of files) {
      const r = await fetch(PLUGIN_SOURCE + f.path, { cf: { cacheTtl: 300 } } as RequestInit);
      if (!r.ok) {
        if (f.path.endsWith("plugin.json") || f.path.endsWith("hooks.json") || f.path.endsWith(".sh")) {
          return json({ error: `couldn't fetch ${f.path} from GitHub (${r.status})` }, 502);
        }
        continue;
      }
      let data: Uint8Array = new Uint8Array(await r.arrayBuffer());
      if (f.path === ".claude-plugin/plugin.json") {
        const manifest = JSON.parse(new TextDecoder().decode(data)) as Record<string, unknown>;
        manifest.name = "deskling-personal";
        manifest.displayName = "Deskling (yours)";
        manifest.description = `Sends Claude activity to your Deskling screen through ${url.host}. Keep it private: it holds your relay token.`;
        data = new TextEncoder().encode(JSON.stringify(manifest, null, 2) + "\n");
      }
      entries.push({ path: f.path, data, executable: f.executable });
    }
    const conf =
      `# Made by ${url.origin}/setup. This copy of the plugin holds your relay token, so keep it private.\n` +
      `# ~/.deskling/config (or the plugin options) on a computer still win over these.\n` +
      `DESKLING_URL=${url.origin}\n` +
      `DESKLING_TOKEN=${String(this.env.DESKLING_TOKEN ?? "")}\n`;
    entries.push({ path: "deskling.conf", data: new TextEncoder().encode(conf) });
    return new Response(makeZip(entries), {
      headers: {
        "content-type": "application/zip",
        "content-disposition": 'attachment; filename="deskling-personal.zip"',
        "cache-control": "no-store",
      },
    });
  }

  private recent = new Map<string, number>();

  /** True if this exact hook body arrived in the last few seconds. */
  private seenRecently(raw: string): boolean {
    const now = Date.now();
    for (const [k, t] of this.recent) if (now - t > 5000) this.recent.delete(k);
    let h = 2166136261;
    for (let i = 0; i < raw.length; i++) h = Math.imul(h ^ raw.charCodeAt(i), 16777619);
    const key = `${raw.length}:${h >>> 0}`;
    if (this.recent.has(key)) return true;
    this.recent.set(key, now);
    return false;
  }

  private async onHook(body: Record<string, unknown>, host: string, os: string, wait: number, label = ""): Promise<Response> {
    const event = String(body.hook_event_name ?? "");
    const sid = String(body.session_id ?? "") || `${host}-unknown`;
    // Cowork sessions without a project folder run in a home directory: name them after the app.
    const dir = baseName(body.cwd);
    const name = label && /^(claude|root|user|home|)$/i.test(dir) ? label : dir || label || "claude";

    if (event === "SessionEnd") {
      this.removeAgent(sid);
      await this.commit({ kind: "agent", id: sid });
      return empty();
    }

    const agent = this.upsertAgent(sid, "claude-code", name, host, os);

    switch (event) {
      case "SessionStart":
        this.setAgent(agent, "idle", "Ready for a prompt");
        await this.commit({ kind: "agent", id: sid });
        return empty();

      case "UserPromptSubmit": {
        agent.lastPrompt = clip(body.prompt, 400);
        this.setAgent(agent, "working", oneLine(body.prompt) || "Working");
        // Messages queued on the screen ride along with the prompt you typed.
        const queued = this.channelLive(sid) ? [] : this.takeQueued(sid);
        await this.commit({ kind: "agent", id: sid });
        if (queued.length) {
          return json({
            hookSpecificOutput: {
              hookEventName: "UserPromptSubmit",
              additionalContext: `The user also sent these messages from their Deskling desk screen:\n${queued.map((m) => `- ${m}`).join("\n")}`,
            },
          });
        }
        return empty();
      }

      case "Notification": {
        const type = String(body.notification_type ?? "");
        const message = oneLine(body.message, 140);
        if (type === "permission_prompt") {
          // If a request is already open on the screen, it's showing; otherwise flag the terminal prompt.
          if (!this.state.pending.some((p) => p.agentId === sid)) {
            this.setAgent(agent, "needs_you", message || "Waiting for permission");
            this.addFeed("alert", "Permission needed", `${name} · ${host}`, sid);
          }
          await this.commit({ kind: "permission", id: sid });
        } else if (type === "idle_prompt") {
          this.setAgent(agent, "waiting", "Waiting for your reply");
          this.addFeed("info", "Claude is waiting for you", `${name} · ${host}`, sid);
          await this.commit({ kind: "feed" });
        } else if (type === "elicitation_dialog" || type === "agent_needs_input") {
          this.setAgent(agent, "needs_you", message || "Needs your input");
          this.addFeed("alert", "Claude needs your input", `${name} · ${host}`, sid);
          await this.commit({ kind: "permission", id: sid });
        } else if (message) {
          this.addFeed("info", oneLine(body.title, 60) || "Claude Code", message, sid);
          await this.commit({ kind: "feed" });
        }
        return empty();
      }

      case "Stop": {
        const full = clip(body.last_assistant_message, 600);
        const summary = oneLine(full, 120);
        agent.lastMessage = full;
        if (full) this.pushChat(sid, "agent", full, "sent");
        // Anything queued on the screen keeps Claude going instead of stopping.
        const queued = this.channelLive(sid) ? [] : this.takeQueued(sid);
        if (queued.length) {
          this.setAgent(agent, "working", oneLine(queued[0]));
          await this.commit({ kind: "chat", id: sid });
          return json({
            decision: "block",
            reason: `The user sent ${queued.length > 1 ? "these messages" : "this message"} from their Deskling desk screen. Treat it like a new prompt from them:\n${queued.map((m) => `- ${m}`).join("\n")}`,
          });
        }
        this.setAgent(agent, "done", summary || "Finished");
        this.addFeed("good", "Claude finished", `${name}${summary ? " · " + summary : ""}`, sid);
        const ping = this.state.pingWhenDone;
        this.state.pingWhenDone = false;
        await this.commit({ kind: ping ? "ping" : "done", id: sid });
        return empty();
      }

      case "PreToolUse":
        if (String(body.tool_name) === "AskUserQuestion") return this.onQuestion(agent, body, wait);
        agent.updated = Date.now();
        await this.save();
        return empty();

      case "PermissionRequest":
        if (String(body.tool_name) === "AskUserQuestion") return empty(); // handled as a question
        return this.onPermission(agent, body, wait);

      default:
        agent.updated = Date.now();
        await this.save();
        return empty();
    }
  }

  private async onPermission(agent: Agent, body: Record<string, unknown>, wait: number): Promise<Response> {
    const tool = String(body.tool_name ?? "Tool");
    const { summary, detail } = toolSummary(tool, body.tool_input as Record<string, unknown> | undefined);
    const r = await this.ask(agent, "permission", tool, summary, detail, [], wait);
    if ("decision" in r && (r.decision === "allow" || r.decision === "deny")) {
      return json({ hookSpecificOutput: { hookEventName: "PermissionRequest", decision: { behavior: r.decision } } });
    }
    return empty(); // no decision: Claude Code shows its normal prompt
  }

  private async onQuestion(agent: Agent, body: Record<string, unknown>, wait: number): Promise<Response> {
    const questions = parseQuestions(body.tool_input);
    if (!questions.length) return empty();
    const r = await this.ask(agent, "question", "AskUserQuestion", questions[0].question, questions[0].header, questions, wait);
    if ("answers" in r) {
      const lines = questions.map((q) => {
        const a = r.answers[q.question];
        const text = Array.isArray(a) ? a.join(", ") : a;
        return `- ${q.question} → ${text || "(no answer)"}`;
      });
      if (r.note) lines.push(`- Note from the user: ${r.note}`);
      // Experimental: there's no documented way for a shell hook to fill in the
      // answers, so the hook stops the dialog and hands Claude the answers instead.
      return json({
        hookSpecificOutput: {
          hookEventName: "PreToolUse",
          permissionDecision: "deny",
          permissionDecisionReason: `No need to ask: the user already answered on their Deskling desk screen.\n${lines.join("\n")}\nContinue using these answers.`,
        },
      });
    }
    return empty(); // timed out or "on my computer": the normal dialog appears
  }

  /** Put a request on the screen and wait for an answer, a decision, or the timeout. */
  private async ask(agent: Agent, kind: Pending["kind"], tool: string, summary: string, detail: string, questions: Question[], wait: number): Promise<Resolution> {
    const now = Date.now();
    const p: Pending = {
      id: shortId(),
      kind,
      agentId: agent.id,
      source: agent.source,
      name: agent.name,
      host: agent.host,
      os: agent.os,
      tool,
      summary,
      detail,
      questions,
      ts: now,
      expires: now + wait * 1000,
    };
    this.state.pending.push(p);
    const previous = { state: agent.state, task: agent.task };
    this.setAgent(agent, "needs_you", kind === "question" ? "Has a question for you" : `Wants to use ${tool}`);
    this.addFeed("alert", kind === "question" ? "Claude has a question" : "Permission needed", kind === "question" ? `${agent.name} · ${oneLine(summary, 60)}` : `${agent.name} wants to use ${tool}`, agent.id);

    // Register the waiter before committing: prune() drops requests nobody waits on.
    const answered = new Promise<Resolution>((resolve) => {
      const timer = setTimeout(() => {
        this.waiters.delete(p.id);
        resolve({ timeout: true });
      }, wait * 1000);
      this.waiters.set(p.id, (r) => {
        clearTimeout(timer);
        this.waiters.delete(p.id);
        resolve(r);
      });
    });
    await this.commit({ kind: kind === "question" ? "question" : "permission", id: p.id });
    const r = await answered;

    this.state.pending = this.state.pending.filter((x) => x.id !== p.id);
    const resumed = previous.state === "working" ? previous.task : "Working";
    if ("answers" in r) {
      this.setAgent(agent, "working", resumed);
      this.addFeed("good", "Answered from Deskling", agent.name, agent.id);
    } else if ("decision" in r && (r.decision === "allow" || r.decision === "deny")) {
      this.setAgent(agent, "working", resumed);
      this.addFeed(r.decision === "allow" ? "good" : "info", r.decision === "allow" ? "Approved from Deskling" : "Denied from Deskling", `${agent.name} · ${tool}`, agent.id);
    }
    // "computer" and timeouts leave the agent flagged: the prompt on the computer takes over.
    await this.commit({ kind: "permission_closed", id: p.id });
    return r;
  }

  // ------------------------------------------------------------ Cursor hooks

  private async onCursorHook(body: Record<string, unknown>, host: string, os: string, wait: number, approve: boolean): Promise<Response> {
    const event = String(body.hook_event_name ?? "");
    const sid = `cursor-${String(body.conversation_id ?? "") || host}`;
    const roots = Array.isArray(body.workspace_roots) ? body.workspace_roots : [];
    const name = baseName(roots[0]) || "cursor";
    const agent = this.upsertAgent(sid, "cursor", name, host, os);

    if (event === "beforeSubmitPrompt") {
      agent.lastPrompt = clip(body.prompt, 400);
      this.setAgent(agent, "working", oneLine(body.prompt) || "Working");
      await this.commit({ kind: "agent", id: sid });
      return json({ continue: true });
    }
    if (event === "stop") {
      const status = String(body.status ?? "completed");
      this.setAgent(agent, "done", status === "completed" ? "Finished" : status === "aborted" ? "Stopped" : "Hit an error");
      this.addFeed(status === "error" ? "alert" : "good", status === "error" ? "Cursor hit an error" : "Cursor finished", name, sid);
      await this.commit({ kind: "done", id: sid });
      return json({});
    }
    if (event === "afterFileEdit") {
      if (agent.state !== "working") this.setAgent(agent, "working", "Editing files");
      agent.task = `Editing ${baseName(body.file_path) || "files"}`;
      await this.commit({ kind: "agent", id: sid });
      return json({});
    }
    if (event === "beforeShellExecution" || event === "beforeMCPExecution") {
      const isShell = event === "beforeShellExecution";
      const tool = isShell ? "Shell" : `MCP ${oneLine(body.tool_name ?? body.server, 40)}`;
      if (!approve) return json({ continue: true, permission: "ask" });
      const summary = isShell ? oneLine(body.command, 160) : oneLine(String(body.tool_input ?? ""), 160);
      const r = await this.ask(agent, "permission", tool, summary, isShell ? oneLine(body.cwd, 80) : "", [], wait);
      const decided = "decision" in r && (r.decision === "allow" || r.decision === "deny");
      return json({
        continue: true,
        permission: decided ? (r as { decision: Decision }).decision : "ask",
        userMessage: decided ? `${(r as { decision: Decision }).decision === "allow" ? "Approved" : "Denied"} on Deskling` : undefined,
      });
    }
    agent.updated = Date.now();
    await this.save();
    return json({});
  }

  // --------------------------------------------- channel (two-way chat) plumbing

  private async onChannelRegister(body: Record<string, unknown>): Promise<Response> {
    const id = oneLine(body.cid, 40) || shortId();
    const ch: Channel = {
      id,
      host: oneLine(body.host, 40) || "computer",
      name: baseName(body.cwd) || "claude",
      os: oneLine(body.os, 20) || "unknown",
      agentId: null,
      lastPoll: Date.now(),
    };
    this.channels.set(id, ch);
    this.linkChannel(ch);
    return json({ cid: id, agentId: ch.agentId });
  }

  private async onChannelPoll(url: URL): Promise<Response> {
    const ch = this.channels.get(url.searchParams.get("cid") ?? "");
    if (!ch) return json({ error: "unknown channel, register again" }, 409);
    ch.lastPoll = Date.now();
    if (!ch.agentId) this.linkChannel(ch);
    const wait = Math.min(50, Math.max(1, Number(url.searchParams.get("wait") ?? 25) || 25));

    let messages = ch.agentId ? this.takeQueued(ch.agentId) : [];
    if (!messages.length) {
      await new Promise<void>((resolve) => {
        const t = setTimeout(resolve, wait * 1000);
        this.channelPolls.set(ch.id, () => {
          clearTimeout(t);
          resolve();
        });
      });
      this.channelPolls.delete(ch.id);
      ch.lastPoll = Date.now();
      if (!ch.agentId) this.linkChannel(ch);
      messages = ch.agentId ? this.takeQueued(ch.agentId) : [];
    }
    if (messages.length) await this.commit({ kind: "chat", id: ch.agentId ?? undefined });
    return json({ agentId: ch.agentId, messages });
  }

  private async onChannelReply(body: Record<string, unknown>): Promise<Response> {
    const ch = this.channels.get(String(body.cid ?? ""));
    if (!ch) return json({ error: "unknown channel, register again" }, 409);
    if (!ch.agentId) this.linkChannel(ch);
    const text = clip(body.text, 600);
    if (!text) return json({ error: "text required" }, 400);
    if (ch.agentId) {
      this.pushChat(ch.agentId, "agent", text, "sent");
      const a = this.state.agents[ch.agentId];
      if (a) a.lastMessage = text;
    } else {
      this.addFeed("info", ch.name, text, null);
    }
    await this.commit({ kind: "chat", id: ch.agentId ?? undefined });
    return json({ ok: true });
  }

  /** Match a channel to the Claude Code session on the same machine and folder. */
  private linkChannel(ch: Channel): void {
    const taken = new Set([...this.channels.values()].filter((c) => c !== ch && c.agentId).map((c) => c.agentId));
    const match = Object.values(this.state.agents)
      .filter((a) => a.source === "claude-code" && a.host === ch.host && a.name === ch.name && !taken.has(a.id))
      .sort((a, b) => b.since - a.since)[0];
    if (match) ch.agentId = match.id;
  }

  private channelLive(agentId: string): boolean {
    const now = Date.now();
    for (const ch of this.channels.values()) if (ch.agentId === agentId && now - ch.lastPoll < CHANNEL_LIVE_MS) return true;
    return false;
  }

  // ------------------------------------------------ generic event API

  private async onApiEvent(body: Record<string, unknown>): Promise<Response> {
    const kind = String(body.kind ?? "");
    if (kind === "notify") {
      const level = (["info", "good", "alert"].includes(String(body.level)) ? body.level : "info") as FeedLevel;
      const item = this.addFeed(level, oneLine(body.title, 60) || "Notification", oneLine(body.body, 140), null);
      await this.commit({ kind: "feed", id: item.id });
      return json({ ok: true, id: item.id });
    }
    if (kind === "reminder") {
      const title = oneLine(body.title, 80);
      if (!title) return json({ error: "title required" }, 400);
      const inMin = Number(body.in_minutes);
      const r: Reminder = {
        id: shortId(),
        title,
        due: Number.isFinite(inMin) && inMin > 0 ? Date.now() + inMin * 60_000 : parseDue(body.at),
        from: oneLine(body.from, 30) || "API",
        done: false,
        doneAt: null,
        notified: false,
      };
      this.state.reminders.push(r);
      this.addFeed("reminder", "Reminder set", title, null);
      await this.commit({ kind: "reminder", id: r.id });
      return json({ ok: true, id: r.id, due: r.due });
    }
    if (kind === "status") {
      const id = oneLine(body.id, 60);
      if (!id) return json({ error: "id required" }, 400);
      if (body.remove === true) {
        this.removeAgent(id);
      } else {
        const state = String(body.state ?? "working") as AgentState;
        if (!["idle", "working", "waiting", "needs_you", "done"].includes(state)) return json({ error: "bad state" }, 400);
        const src = SOURCES.includes(body.source as Source) ? (body.source as Source) : "custom";
        const agent = this.upsertAgent(id, src, oneLine(body.name, 40) || id, oneLine(body.host, 40) || "cloud", oneLine(body.os, 20) || "cloud");
        this.setAgent(agent, state, oneLine(body.task) || agent.task);
        if (body.message) agent.lastMessage = clip(body.message, 600);
        if (state === "done") this.addFeed("good", `${agent.name} finished`, oneLine(body.task), id);
        if (state === "needs_you") this.addFeed("alert", `${agent.name} needs you`, oneLine(body.task), id);
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
    const type = msg.type;
    if (type === "decision" || type === "answer") {
      const id = String(msg.id ?? "");
      let r: Resolution | null = null;
      if (type === "decision" && ["allow", "deny", "computer"].includes(String(msg.decision))) r = { decision: msg.decision as Decision };
      if (type === "answer" && msg.answers && typeof msg.answers === "object") r = { answers: msg.answers as Answers, note: clip(msg.note, 300) };
      if (!r) return;
      const waiter = this.waiters.get(id);
      if (waiter) waiter(r);
      else {
        // Nobody is waiting any more (timed out or the relay restarted): just clear it.
        this.state.pending = this.state.pending.filter((p) => p.id !== id);
        await this.commit({ kind: "permission_closed", id });
      }
      return;
    }
    if (type === "message") {
      const agentId = String(msg.agentId ?? "");
      const text = clip(msg.text, 600);
      const agent = this.state.agents[agentId];
      if (!agent || !text) return;
      if (agent.source !== "claude-code") {
        this.addFeed("info", "Can't message that one", `${agent.name} doesn't accept messages from Deskling`, agentId);
        await this.commit({ kind: "feed" });
        return;
      }
      this.pushChat(agentId, "you", text, "queued");
      await this.commit({ kind: "chat", id: agentId });
      for (const ch of this.channels.values()) if (ch.agentId === agentId) this.channelPolls.get(ch.id)?.();
      return;
    }
    if (type === "unqueue") {
      const chat = this.state.chats[String(msg.agentId ?? "")];
      if (!chat) return;
      this.state.chats[String(msg.agentId)] = chat.filter((c) => !(c.id === msg.id && c.status === "queued"));
      await this.commit({ kind: "chat", id: String(msg.agentId) });
      return;
    }
    if (type === "dismiss") {
      const a = this.state.agents[String(msg.agentId ?? "")];
      if (a && msg.remove === true) this.removeAgent(a.id);
      else if (a && a.state === "done") this.setAgent(a, "idle", "Ready for a prompt");
      await this.commit({ kind: "agent" });
      return;
    }
    if (type === "feed_dismiss") {
      const id = String(msg.id ?? "");
      const before = this.state.feed.length;
      this.state.feed = this.state.feed.filter((f) => f.id !== id);
      if (this.state.feed.length !== before) await this.commit({ kind: "feed_removed", id });
      return;
    }
    if (type === "feed_clear") {
      if (this.state.feed.length) {
        this.state.feed = [];
        await this.commit({ kind: "feed_removed" });
      }
      return;
    }
    if (type === "reminder_done") {
      const r = this.state.reminders.find((x) => x.id === msg.id);
      if (!r) return;
      r.done = msg.done !== false;
      r.doneAt = r.done ? Date.now() : null;
      await this.commit({ kind: "reminder_done", id: r.id });
      return;
    }
    if (type === "action") {
      const name = String(msg.name ?? "");
      if (name === "snooze_all") {
        const until = Date.now() + 15 * 60 * 1000;
        for (const r of this.state.reminders) {
          if (!r.done && r.due !== null && r.due < until) {
            r.due = until;
            r.notified = false;
          }
        }
        this.addFeed("reminder", "Reminders snoozed", "Back in 15 minutes", null);
        await this.commit({ kind: "reminder" });
      } else if (name === "focus") {
        this.state.focus = typeof msg.on === "boolean" ? msg.on : !this.state.focus;
        await this.commit({ kind: "focus" });
      } else if (name === "ping_when_done") {
        this.state.pingWhenDone = true;
        this.addFeed("info", "Okay!", "I'll ping you when Claude finishes", null);
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
        this.addFeed("reminder", "Reminder", r.title, null);
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

  private upsertAgent(id: string, source: Source, name: string, host: string, os: string): Agent {
    const now = Date.now();
    let a = this.state.agents[id];
    if (!a) {
      a = { id, source, name, host, os, state: "idle", task: "", lastPrompt: "", lastMessage: "", since: now, updated: now };
      this.state.agents[id] = a;
    }
    a.source = source;
    a.name = name || a.name;
    a.host = host || a.host;
    a.os = os || a.os;
    a.updated = now;
    for (const ch of this.channels.values()) if (!ch.agentId) this.linkChannel(ch);
    return a;
  }

  private removeAgent(id: string): void {
    delete this.state.agents[id];
    delete this.state.chats[id];
    this.state.pending = this.state.pending.filter((p) => p.agentId !== id);
    for (const ch of this.channels.values()) if (ch.agentId === id) ch.agentId = null;
  }

  private setAgent(a: Agent, state: AgentState, task: string): void {
    if (a.state !== state) a.since = Date.now();
    a.state = state;
    a.task = task;
  }

  private addFeed(level: FeedLevel, title: string, body: string, agentId: string | null): FeedItem {
    const item: FeedItem = { id: shortId(), level, title, body, agentId, ts: Date.now() };
    this.state.feed.unshift(item);
    if (this.state.feed.length > FEED_MAX) this.state.feed.length = FEED_MAX;
    return item;
  }

  private pushChat(agentId: string, from: ChatLine["from"], text: string, status: ChatLine["status"]): void {
    const chat = (this.state.chats[agentId] ??= []);
    chat.push({ id: shortId(), from, text, status, ts: Date.now() });
    if (chat.length > CHAT_MAX) chat.splice(0, chat.length - CHAT_MAX);
  }

  /** Mark this agent's queued screen messages delivered and return their text. */
  private takeQueued(agentId: string): string[] {
    const chat = this.state.chats[agentId] ?? [];
    const out: string[] = [];
    for (const line of chat) {
      if (line.status === "queued") {
        line.status = "delivered";
        out.push(line.text);
      }
    }
    return out;
  }

  private prune(): void {
    const now = Date.now();
    for (const [id, a] of Object.entries(this.state.agents)) {
      if (now - a.updated > AGENT_STALE_MS) this.removeAgent(id);
    }
    this.state.reminders = this.state.reminders.filter((r) => !r.done || (r.doneAt ?? 0) > now - REMINDER_KEEP_DONE_MS);
    this.state.pending = this.state.pending.filter((p) => this.waiters.has(p.id));
    for (const [id, ch] of this.channels) if (now - ch.lastPoll > 10 * 60 * 1000) this.channels.delete(id);
  }

  private snapshot(cause: Cause): string {
    const s = this.state;
    const rank: Record<AgentState, number> = { needs_you: 0, waiting: 1, working: 2, done: 3, idle: 4 };
    const agents = Object.values(s.agents)
      .sort((a, b) => rank[a.state] - rank[b.state] || b.updated - a.updated)
      .slice(0, 12)
      .map((a) => ({
        ...a,
        lastMessage: clip(a.lastMessage, 280),
        lastPrompt: clip(a.lastPrompt, 160),
        channel: this.channelLive(a.id),
        canMessage: a.source === "claude-code",
        chat: (s.chats[a.id] ?? []).slice(-6).map((c) => ({ ...c, text: clip(c.text, 280) })),
      }));
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
