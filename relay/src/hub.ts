import { DurableObject } from "cloudflare:workers";
import { makeZip, type ZipEntry } from "./zip";
import type {
  Helper,
  Machine,
  Engine,
  StartJob,
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
const CHAT_MAX = 80; // messages and tool steps kept per session
const CHAT_TEXT_MAX = 1200; // the screen keeps this much of each chat line
const SNAPSHOT_CHAT = 30; // chat lines per session sent to the screen
const TAIL_CHAT = 2; // for sessions that aren't open on the screen
const STEPS_SHOWN = 3; // tool steps shown in a row before they collapse into a count
const HELPER_MAX = 4; // helpers per session the screen shows
const HELPER_KEEP_MS = 10 * 60 * 1000; // finished helpers stay listed this long
const QUIET_MS = 2 * 60 * 60 * 1000; // "working" with no news for this long is shown as idle
const AGENT_STALE_MS = 12 * 60 * 60 * 1000; // forget sessions idle for 12 h
const REMINDER_KEEP_DONE_MS = 24 * 60 * 60 * 1000;
const DEFAULT_WAIT_S = 30;
const MAX_WAIT_S = 110;
const HOLD_S = 85; // one round of a listening Stop hook (the hook asks again until listening ends)
const LISTEN_MAX_MIN = 120;
const IMAGES_KEPT = 8; // pictures kept per session
const MACHINE_GONE_MS = 3 * 60 * 1000; // a runner that hasn't checked in for this long is offline
const RUNNER_POLL_S = 50;
const IMAGE_MAX_BYTES = 1_800_000;
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

/** What the screen calls each kind of agent in its notifications. */
function agentLabel(source: Source): string {
  return ({ "claude-code": "Claude", cowork: "Claude", opencode: "OpenCode", codex: "Codex", gemini: "Gemini", cursor: "Cursor" } as Record<string, string>)[source] ?? "Your agent";
}

function shortId(): string {
  return crypto.randomUUID().replace(/-/g, "").slice(0, 10);
}

function oneLine(s: unknown, max = 90): string {
  if (typeof s !== "string") return "";
  const flat = s.replace(/\s+/g, " ").trim();
  return flat.length > max ? flat.slice(0, max - 1) + "…" : flat;
}

/**
 * What a person would want to read from a prompt or a reply: Claude Code wraps some prompts in
 * tags meant for the model (<system-reminder>, <command-name>…), and replies use markdown that the
 * screen can't render. Typographic marks become their plain forms so the screen's fonts have them.
 */
function readable(s: unknown): string {
  if (typeof s !== "string") return "";
  return s
    .replace(/<(system-reminder|agent-message|task-notification|local-command-stdout|local-command-stderr|command-message|command-args)(\s[^>]*)?>[\s\S]*?<\/\1>/g, "")
    .replace(/\[Image #\d+\]/g, "[photo]")
    .replace(/@"[^"]*excerpt_from_previous_claude_message\.txt"\s*/g, "") // shown as a quote instead
    .replace(/@"[^"]*[\\/]([^"\\/]+)"/g, "[$1]") // @"path/to/file" -> [file]
    .replace(/<command-name>\s*([\s\S]*?)\s*<\/command-name>/g, "$1")
    .replace(/<\/?[a-z][a-z0-9-]*>/gi, "")
    .replace(/\*\*(.+?)\*\*/g, "$1")
    .replace(/__(.+?)__/g, "$1")
    .replace(/`{3}[a-z]*\n?/gi, "")
    .replace(/`([^`]+)`/g, "$1")
    .replace(/^#{1,6}\s+/gm, "")
    .replace(/^\s*[*•]\s+/gm, "- ")
    .replace(/\[([^\]]+)\]\((https?:[^)]+)\)/g, "$1 ($2)")
    .replace(/[\u2018\u2019]/g, "'")
    .replace(/[\u201C\u201D]/g, '"')
    .replace(/[\u2013\u2014]/g, "-")
    .replace(/\u2026/g, "...")
    .replace(/\u2192/g, "->")
    .replace(/\u00A0/g, " ")
    .replace(/\n{3,}/g, "\n\n")
    .trim();
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

/**
 * The conversation as the screen shows it: messages in full, and runs of tool steps cut to
 * the last few with a count of the rest (like the app's collapsed step list).
 */
function chatView(lines: ChatLine[]): ChatLine[] {
  const out: ChatLine[] = [];
  let run: ChatLine[] = [];
  const flush = () => {
    if (run.length > STEPS_SHOWN) {
      const hidden = run.length - STEPS_SHOWN;
      out.push({ ...run[0], from: "tool", id: run[0].id + "m", text: `${hidden} earlier step${hidden > 1 ? "s" : ""}` });
    }
    out.push(...run.slice(-STEPS_SHOWN));
    run = [];
  };
  for (const l of lines) {
    if (l.from === "tool" || l.from === "thought") run.push(l);
    else {
      flush();
      out.push({ ...l, text: clip(l.text, CHAT_TEXT_MAX) });
    }
  }
  flush();
  return out.slice(-SNAPSHOT_CHAT);
}

/** Its thinking as short lines for the screen: the first few sentences or lines, clipped. */
function thoughtLines(t: string): string[] {
  const lines = readable(t)
    .split(/\n+/)
    .map((l) => l.replace(/^[-.\s]+$/, "").trim())
    .filter((l) => l && l !== "…" && l !== "...");
  return lines.slice(0, 3).map((l) => oneLine(l, 140));
}

/** A short "what it's doing" line for the screen, from a tool call. */
function activity(tool: string, input: Record<string, unknown> | undefined): string {
  const i = input ?? {};
  const str = (k: string) => (typeof i[k] === "string" ? (i[k] as string) : "");
  const file = (k: string) => baseName(str(k));
  switch (tool) {
    case "Bash":
    case "PowerShell":
      return oneLine(str("description") || "Running " + str("command"), 90);
    case "Read":
      return oneLine("Reading " + file("file_path"), 90);
    case "Edit":
    case "MultiEdit":
    case "NotebookEdit":
      return oneLine("Editing " + (file("file_path") || file("notebook_path")), 90);
    case "Write":
      return oneLine("Writing " + file("file_path"), 90);
    case "Grep":
    case "Glob":
      return oneLine("Searching for " + (str("pattern") || "files"), 90);
    case "WebSearch":
      return oneLine("Searching the web for " + str("query"), 90);
    case "WebFetch":
      return oneLine("Reading " + str("url").replace(/^https?:\/\//, ""), 90);
    case "Task":
    case "Agent":
      return oneLine(str("description") || "Working with a helper", 90);
    case "TodoWrite":
    case "TaskCreate":
    case "TaskUpdate":
      return "Updating its to-do list";
    case "SendUserMessage":
      return "Writing to you";
    default:
      return oneLine("Using " + tool.replace(/^mcp__[^_]+__/, "").replace(/_/g, " "), 90);
  }
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

/** The chat open on a screen (null: a screen from before views, which gets every chat). */
function viewOf(ws: WebSocket): string | null {
  try {
    const a = ws.deserializeAttachment() as { view?: string } | null;
    return typeof a?.view === "string" ? a.view : null;
  } catch {
    return null;
  }
}

function json(data: unknown, status = 200): Response {
  return new Response(JSON.stringify(data), { status, headers: { "content-type": "application/json" } });
}

const empty = () => new Response("");

export class Hub extends DurableObject<Record<string, unknown>> {
  private state: State = emptyState();
  private waiters = new Map<string, (r: Resolution) => void>();
  /** Sessions whose Stop hook is holding, waiting for a message from the screen. */
  private listeners = new Map<string, (why: "message" | "cancel") => void>();
  /** Transcript lines already handled, per session (uploads can overlap after a retry). */
  private seenLines = new Map<string, Set<string>>();
  /** Bridges (OpenCode…) waiting for messages from the screen: poll id → wake(agentId). */
  private bridgePolls = new Map<string, (agentId: string) => void>();
  /** Runners waiting for a job, by machine. */
  private runnerPolls = new Map<string, () => void>();
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

    if (req.method === "POST" && path === "/transcript") return this.onTranscript(url.searchParams.get("sid") ?? "", await req.text());
    // ---- bridges: other agents (OpenCode, …) reporting through a small adapter on your computer
    if (req.method === "POST" && path === "/agent/update") return this.onAgentUpdate((await readJson()) ?? {});
    if (req.method === "POST" && path === "/agent/ask") return this.onAgentAsk((await readJson()) ?? {});
    if (req.method === "GET" && path === "/agent/poll") return this.onAgentPoll(url);
    if (req.method === "POST" && path === "/agent/resolve") return this.onAgentResolve((await readJson()) ?? {});

    // ---- starting sessions: the runner on your computers, and a cloud routine
    if (req.method === "POST" && path === "/runner/register") return this.onRunnerRegister((await readJson()) ?? {});
    if (req.method === "GET" && path === "/runner/poll") return this.onRunnerPoll(url.searchParams.get("machine") ?? "");
    if (req.method === "POST" && path === "/runner/done") {
      const b = (await readJson()) ?? {};
      if (b.ok === false) {
        this.addFeed("alert", "Couldn't start the session", `${oneLine(b.machine, 40)} · ${oneLine(b.error, 100)}`, null);
        await this.commit({ kind: "feed" });
      }
      return json({ ok: true });
    }
    if (path === "/api/routine") {
      if (req.method === "POST") {
        const b = (await readJson()) ?? {};
        const u = String(b.url ?? "").trim();
        const t = String(b.token ?? "").trim();
        if (!u && !t) this.state.routine = null;
        else if (!/^https:\/\/api\.anthropic\.com\/v1\/claude_code\/routines\/[\w-]+\/fire$/.test(u) || !t) return json({ error: "That isn't a routine's API trigger URL and token" }, 400);
        else this.state.routine = { url: u, token: t, name: oneLine(b.name, 40) || "Cloud" };
        await this.commit({ kind: "agent" });
      }
      return json({ routine: this.state.routine ? { url: this.state.routine.url, name: this.state.routine.name, token: "…" + this.state.routine.token.slice(-4) } : null });
    }

    if (req.method === "POST" && path === "/quote") {
      const a = this.state.agents[url.searchParams.get("sid") ?? ""];
      const name = baseName(url.searchParams.get("name") ?? "");
      const text = oneLine(readable(await req.text()), 400);
      if (a && name && text) {
        a.quotes = { ...(a.quotes ?? {}), [name]: text };
        const keys = Object.keys(a.quotes);
        for (const k of keys.slice(0, Math.max(0, keys.length - 6))) delete a.quotes[k];
      }
      return json({ ok: true });
    }
    if (req.method === "POST" && path === "/listen/release") {
      const sid = String(((await readJson()) ?? {}).session_id ?? "");
      this.listeners.get(sid)?.("cancel");
      return json({ ok: true });
    }
    if (req.method === "GET" && path.startsWith("/img/")) {
      const id = path.slice(5).replace(/[^a-z0-9]/gi, "");
      const img = await this.ctx.storage.get<{ type: string; data: Uint8Array }>("img:" + id);
      if (!img) return new Response("not found\n", { status: 404 });
      return new Response(img.data, { headers: { "content-type": img.type, "cache-control": "private, max-age=86400" } });
    }

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
      if (this.seenRecently(url.search + raw)) return empty();
      const host = oneLine(req.headers.get("x-deskling-host") ?? "", 40) || "computer";
      const os = oneLine(req.headers.get("x-deskling-os") ?? "", 20) || "unknown";
      const src = (req.headers.get("x-deskling-source") ?? "claude-code").toLowerCase() as Source;
      const wait = Math.min(MAX_WAIT_S, Math.max(0, Number(url.searchParams.get("wait") ?? DEFAULT_WAIT_S) || DEFAULT_WAIT_S));
      if (src === "cursor") return this.onCursorHook(body, host, os, wait, url.searchParams.get("approve") === "1");
      const label = oneLine(req.headers.get("x-deskling-label") ?? "", 30);
      const project = oneLine(req.headers.get("x-deskling-project") ?? "", 48);
      const round = url.searchParams.has("hold") ? Number(url.searchParams.get("round") ?? 0) || 0 : -1;
      return this.onHook(body, host, os, wait, label, url.searchParams.get("live") === "1", project, round);
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
      // The routine's own token never leaves the relay.
      return json({ ...this.state, routine: this.state.routine ? { ...this.state.routine, token: "…" + this.state.routine.token.slice(-4) } : null });
    }

    if (req.method === "POST" && path === "/channel/register") return this.onChannelRegister((await readJson()) ?? {});
    if (req.method === "GET" && path === "/channel/poll") return this.onChannelPoll(url);
    if (req.method === "POST" && path === "/channel/reply") return this.onChannelReply((await readJson()) ?? {});
    if (req.method === "POST" && path === "/channel/permission") return this.onChannelPermission((await readJson()) ?? {});

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

  private async onHook(body: Record<string, unknown>, host: string, os: string, wait: number, label = "", live = false, project = "", round = -1): Promise<Response> {
    const event = String(body.hook_event_name ?? "");
    const sid = String(body.session_id ?? "") || `${host}-unknown`;
    // Cowork sessions without a project folder run in a home directory: name them after the app.
    const dir = project || baseName(body.cwd);
    const name = label && /^(claude|root|user|home|)$/i.test(dir) ? label : dir || label || "claude";

    if (event === "SessionEnd") {
      this.removeAgent(sid);
      await this.commit({ kind: "agent", id: sid });
      return empty();
    }

    const agent = this.upsertAgent(sid, "claude-code", name, host, os);
    if (project) agent.name = name; // the folder the session started in is always right

    switch (event) {
      case "SessionStart":
        this.setAgent(agent, "idle", "Ready for a prompt");
        await this.commit({ kind: "agent", id: sid });
        return empty();

      case "UserPromptSubmit": {
        this.listeners.get(sid)?.("cancel"); // you typed on the computer: a waiting Stop hook lets go
        const raw = typeof body.prompt === "string" ? body.prompt : "";
        const handbacks = this.finishHandbacks(agent, raw);
        const prompt = readable(raw);
        const quote = this.quoteFor(agent, raw);
        if (prompt || quote) {
          agent.lastPrompt = clip(prompt, 400);
          this.pushYou(sid, prompt, quote);
        } else if (handbacks) {
          this.pushChat(sid, "tool", handbacks > 1 ? `${handbacks} helpers reported back` : "A helper reported back", "sent");
        }
        this.setAgent(agent, "working", oneLine(prompt) || (handbacks ? "Reading a helper's report" : "Working"));
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
            // No request reached the screen, so it can only be answered where it's showing.
            const where = os === "cloud" ? "in the Claude app" : `on ${host}`;
            this.setAgent(agent, "needs_you", `${message || "Waiting for permission"} (answer it ${where})`);
            this.addFeed("alert", "Permission needed", `${name} · answer it ${where}`, sid);
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
        // Later rounds of a listening Stop hook: just keep waiting.
        if (round > 0) return this.listen(agent, sid);
        const full = clip(readable(body.last_assistant_message), CHAT_TEXT_MAX);
        const summary = oneLine(full, 120);
        agent.lastMessage = full;
        // Skip it if the same words already arrived mid-turn (a message Claude sent while working).
        const chat = this.state.chats[sid] ?? [];
        if (full && !chat.slice(-12).some((c) => c.from === "agent" && c.text === full)) this.pushChat(sid, "agent", full, "sent");
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
        // Listening (and a hook that can hold): wait here for the next message from the screen.
        if (round === 0 && (agent.listenUntil ?? 0) > Date.now()) return this.listen(agent, sid);
        return empty();
      }

      case "PreToolUse": {
        const tool = String(body.tool_name ?? "");
        if (!live) {
          if (tool === "AskUserQuestion") return this.onQuestion(agent, body, wait);
          agent.updated = Date.now();
          await this.save();
          return empty();
        }
        // Live activity: what the session is doing right now, so the screen keeps moving.
        if (tool === "AskUserQuestion") return empty();
        const input = body.tool_input as Record<string, unknown> | undefined;
        const task = activity(tool, input);
        const sub = typeof body.agent_id === "string" ? body.agent_id : "";
        if (sub) {
          // A helper's step: show it on the helper, not as the session's own work.
          const h = this.helper(agent, sub, oneLine(body.agent_type, 60));
          h.task = task;
          h.updated = Date.now();
          await this.commitLive(sid);
          return empty();
        }
        if (tool === "Agent" || tool === "Task") {
          const desc = oneLine(input?.description ?? input?.subagent_type ?? "", 60);
          (agent.helperNames ??= []).push(desc || "Helper");
          agent.helperNames = agent.helperNames.slice(-HELPER_MAX);
        }
        // Still flagged as waiting for permission but no request is open: it was answered elsewhere.
        const open = this.state.pending.some((p) => p.agentId === sid);
        if (agent.state !== "working" && !open) this.setAgent(agent, "working", task);
        else if (agent.state === "working") agent.task = task;
        // Helpers get their own "Started a helper" line, and messages to you are messages.
        if (!agent.streaming && !["SendUserMessage", "Agent", "Task"].includes(tool)) this.pushChat(sid, "tool", task, "sent");
        await this.commitLive(sid);
        return empty();
      }

      case "SubagentStart": {
        const id = String(body.agent_id ?? "");
        if (id) {
          const name = agent.helperNames?.shift() || oneLine(body.agent_type, 60) || "Helper";
          const h = this.helper(agent, id, name);
          h.done = false;
          h.updated = Date.now();
          this.pushChat(sid, "tool", `Started a helper: ${name}`, "sent");
          await this.commitLive(sid);
        }
        return empty();
      }

      case "SubagentStop": {
        const id = String(body.agent_id ?? "");
        const h = id ? agent.helpers?.find((x) => x.id === id) : undefined;
        if (h && !h.done) {
          h.done = true;
          h.task = "";
          h.updated = Date.now();
          this.pushChat(sid, "tool", `Helper finished: ${h.name}`, "sent");
          await this.commitLive(sid);
        }
        return empty();
      }

      case "PostToolUse": {
        // A message Claude sends you while it works (SendUserMessage in the Claude app and Cowork).
        const input = (body.tool_input ?? {}) as Record<string, unknown>;
        const text = agent.streaming ? "" : clip(readable(input.message ?? input.text ?? ""), CHAT_TEXT_MAX);
        if (agent.state === "needs_you" && !this.state.pending.some((p) => p.agentId === sid)) this.setAgent(agent, "working", "Working");
        if (text) {
          this.pushChat(sid, "agent", text, "sent");
          agent.lastMessage = text;
          await this.commit({ kind: "chat", id: sid });
        } else {
          agent.updated = Date.now();
          await this.save();
        }
        return empty();
      }

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

  // ------------------------------------------------------------ bridges (any agent)

  /**
   * A bridge reports a session: who it is, what it's doing, and new or changed chat lines (a line
   * with a `key` it sent before is updated in place, so streaming text grows rather than repeats).
   */
  private async onAgentUpdate(b: Record<string, unknown>): Promise<Response> {
    const id = oneLine(b.id, 72);
    if (!id) return json({ error: "id required" }, 400);
    if (b.end === true) {
      this.removeAgent(id);
      await this.commit({ kind: "agent", id });
      return json({ ok: true });
    }
    const source = (["opencode", "codex", "gemini", "cursor", "custom"].includes(String(b.source)) ? b.source : "custom") as Source;
    const isNew = !this.state.agents[id];
    const agent = this.upsertAgent(id, source, oneLine(b.name, 48) || "session", oneLine(b.host, 40) || "computer", oneLine(b.os, 20) || "unknown");
    if (b.name) agent.name = oneLine(b.name, 48);
    const states: AgentState[] = ["idle", "working", "waiting", "needs_you", "done"];
    const st = states.includes(b.state as AgentState) ? (b.state as AgentState) : null;
    const open = this.state.pending.some((p) => p.agentId === id);
    const was = agent.state;
    if (st && !(open && st !== "needs_you")) this.setAgent(agent, st, b.task !== undefined ? oneLine(b.task, 90) : agent.task);
    if (st === "done" && was === "working") this.addFeed("good", `${agentLabel(source)} finished`, `${agent.name}${agent.lastMessage ? " · " + oneLine(agent.lastMessage, 60) : ""}`, id);
    else if (b.task !== undefined && !open) agent.task = oneLine(b.task, 90);
    if (typeof b.model === "string") agent.model = oneLine(b.model, 60);
    if (typeof b.context === "number") agent.context = b.context;
    if (Array.isArray(b.helpers)) {
      agent.helpers = (b.helpers as Record<string, unknown>[]).slice(0, HELPER_MAX).map((h) => ({
        id: oneLine(h.id, 40),
        name: oneLine(h.name, 60) || "Helper",
        task: oneLine(h.task, 90),
        done: h.done === true,
        updated: Date.now(),
      }));
    }
    let message = false;
    const chat = (this.state.chats[id] ??= []);
    for (const l of Array.isArray(b.lines) ? (b.lines as Record<string, unknown>[]).slice(0, 40) : []) {
      const from = (["you", "agent", "tool", "thought"].includes(String(l.from)) ? l.from : "agent") as ChatLine["from"];
      // A picture: { key, from, image: "data:image/png;base64,…" }, shown once per key.
      if (typeof l.image === "string") {
        const m = /^data:(image\/(?:png|jpeg|jpg));base64,(.+)$/s.exec(l.image);
        const k = l.key ? oneLine(l.key, 80) : "";
        if (m && !(k && chat.some((c) => c.key === k)) && (await this.addImage(id, from === "you" ? "you" : "agent", { source: { type: "base64", media_type: m[1], data: m[2] } }, k))) message = true;
        continue;
      }
      const text = from === "thought" ? oneLine(readable(l.text), 140) : clip(readable(l.text), CHAT_TEXT_MAX);
      if (!text) continue;
      const key = l.key ? oneLine(l.key, 80) : "";
      const same = key ? chat.find((c) => c.key === key) : undefined;
      if (same) {
        if (same.text !== text) {
          same.text = text;
          if (from === "agent") message = true;
        }
        continue;
      }
      // Your message from the screen comes back from the agent as its prompt: don't show it twice.
      if (from === "you" && chat.slice(-6).some((c) => c.from === "you" && c.text === text)) continue;
      chat.push({ id: shortId(), from, text, status: from === "you" ? "typed" : "sent", ts: Date.now(), ...(key ? { key } : {}) });
      if (from === "agent") {
        agent.lastMessage = text;
        message = true;
      }
      if (from === "you") agent.lastPrompt = clip(text, 400);
    }
    if (chat.length > CHAT_MAX) chat.splice(0, chat.length - CHAT_MAX);
    agent.updated = Date.now();
    if (isNew || message || st === "done" || st === "needs_you") await this.commit({ kind: message ? "chat" : st === "done" ? "done" : "agent", id });
    else await this.commitLive(id);
    return json({ ok: true });
  }

  /** A bridge asks on the screen (a permission or a question) and waits for the answer. */
  private async onAgentAsk(b: Record<string, unknown>): Promise<Response> {
    const agent = this.state.agents[oneLine(b.id, 72)];
    if (!agent) return json({ error: "unknown session" }, 404);
    const kind = b.kind === "question" ? "question" : "permission";
    const wait = Math.min(MAX_WAIT_S, Math.max(5, Number(b.wait ?? MAX_WAIT_S) || MAX_WAIT_S));
    const questions = kind === "question" ? parseQuestions({ questions: b.questions }) : [];
    const r = await this.ask(agent, kind, oneLine(b.tool, 40) || (kind === "question" ? "Question" : "Tool"), clip(b.summary, 200), oneLine(b.detail, 120), questions, wait, oneLine(b.key, 80));
    return json(r);
  }

  /** A bridge's request was answered on the computer: take it off the screen. */
  private async onAgentResolve(b: Record<string, unknown>): Promise<Response> {
    const id = oneLine(b.id, 72);
    const key = oneLine(b.key, 80);
    const p = this.state.pending.find((x) => x.agentId === id && key && x.key === key);
    if (!p) return json({ ok: false });
    this.waiters.get(p.id)?.({ decision: "computer" });
    const agent = this.state.agents[id];
    if (agent?.state === "needs_you") {
      this.setAgent(agent, "working", "Working");
      await this.commit({ kind: "agent", id });
    }
    return json({ ok: true });
  }

  /** A bridge collects the messages you typed on the screen for its sessions (long poll). */
  private async onAgentPoll(url: URL): Promise<Response> {
    const ids = (url.searchParams.get("ids") ?? "").split(",").map((x) => x.trim()).filter(Boolean).slice(0, 50);
    const wait = Math.min(50, Math.max(0, Number(url.searchParams.get("wait") ?? 25) || 0));
    const now = Date.now();
    for (const id of ids) {
      const a = this.state.agents[id];
      if (a) a.polledAt = now;
    }
    const take = () => {
      const out: { id: string; text: string }[] = [];
      for (const id of ids) for (const text of this.takeQueued(id)) out.push({ id, text });
      return out;
    };
    let got = take();
    if (!got.length && wait) {
      const pid = shortId();
      await new Promise<void>((resolve) => {
        const t = setTimeout(() => {
          this.bridgePolls.delete(pid);
          resolve();
        }, wait * 1000);
        this.bridgePolls.set(pid, (agentId) => {
          if (!ids.includes(agentId)) return;
          clearTimeout(t);
          this.bridgePolls.delete(pid);
          resolve();
        });
      });
      got = take();
    }
    if (got.length) await this.commit({ kind: "chat", id: got[0].id });
    return json({ messages: got });
  }

  // ------------------------------------------------------------ starting sessions

  private machinesOnline(): Machine[] {
    const now = Date.now();
    return Object.values(this.state.machines ?? {}).filter((m) => now - m.seen < MACHINE_GONE_MS);
  }

  private async onRunnerRegister(b: Record<string, unknown>): Promise<Response> {
    const id = oneLine(b.machine, 40);
    if (!id) return json({ error: "machine name missing" }, 400);
    const projects = (Array.isArray(b.projects) ? b.projects : [])
      .slice(0, 40)
      .map((p) => ({ name: oneLine((p as Record<string, unknown>).name, 48), path: String((p as Record<string, unknown>).path ?? "").slice(0, 300) }))
      .filter((p) => p.name && p.path)
      // One entry per name: the screen picks projects by name (older runners could list a folder twice).
      .filter((p, i, all) => all.findIndex((q) => q.name === p.name) === i);
    const engines: Engine[] = (Array.isArray(b.engines) ? (b.engines as Record<string, unknown>[]) : [])
      .slice(0, 8)
      .map((e) => ({
        id: oneLine(e.id, 16),
        name: oneLine(e.name, 24),
        models: (Array.isArray(e.models) ? (e.models as Record<string, unknown>[]) : []).slice(0, 8).map((m) => ({ id: oneLine(m.id, 60), name: oneLine(m.name, 30) })).filter((m) => m.id),
      }))
      .filter((e) => /^[a-z0-9-]{1,16}$/.test(e.id) && e.name);
    const known = this.state.machines?.[id];
    const changed =
      !known ||
      JSON.stringify(known.projects) !== JSON.stringify(projects) ||
      JSON.stringify(known.engines ?? []) !== JSON.stringify(engines) ||
      Date.now() - known.seen > MACHINE_GONE_MS;
    (this.state.machines ??= {})[id] = { id, os: oneLine(b.os, 20), projects, seen: Date.now(), ...(engines.length ? { engines } : {}) };
    if (changed) await this.commit({ kind: "agent" });
    else await this.save();
    return json({ ok: true });
  }

  /** A runner waits here (up to RUNNER_POLL_S) for a session to start. */
  private async onRunnerPoll(machine: string): Promise<Response> {
    const m = this.state.machines?.[machine];
    if (m) m.seen = Date.now();
    const take = () => {
      const i = (this.state.jobs ?? []).findIndex((j) => j.machine === machine);
      return i < 0 ? null : (this.state.jobs as StartJob[]).splice(i, 1)[0];
    };
    let job = take();
    if (!job) {
      this.runnerPolls.get(machine)?.();
      await new Promise<void>((resolve) => {
        const t = setTimeout(() => {
          this.runnerPolls.delete(machine);
          resolve();
        }, RUNNER_POLL_S * 1000);
        this.runnerPolls.set(machine, () => {
          clearTimeout(t);
          this.runnerPolls.delete(machine);
          resolve();
        });
      });
      job = take();
    }
    if (job) await this.save();
    return json({ job });
  }

  /** Start a new session from the screen: on a computer with the runner, or in the cloud. */
  private async startSession(target: string, project: string, prompt: string, engine = "", model = ""): Promise<void> {
    if (!prompt) return;
    const now = Date.now();
    if (target === "cloud") {
      const r = this.state.routine;
      if (!r) {
        this.addFeed("alert", "No cloud routine yet", "Add one on your relay's setup page", null);
        await this.commit({ kind: "feed" });
        return;
      }
      let ok = false;
      let detail = "";
      try {
        const res = await fetch(r.url, {
          method: "POST",
          headers: { authorization: `Bearer ${r.token}`, "anthropic-version": "2023-06-01", "anthropic-beta": "experimental-cc-routine-2026-04-01", "content-type": "application/json" },
          body: JSON.stringify({ text: prompt }),
        });
        ok = res.ok;
        detail = ok ? "" : `${res.status} ${oneLine(await res.text(), 120)}`;
      } catch (e) {
        detail = oneLine(String(e), 120);
      }
      if (ok) {
        (this.state.expectNew ??= []).push({ host: "cloud", until: now + 5 * 60 * 1000 });
        this.addFeed("good", "Starting a cloud session", oneLine(prompt, 100), null);
      } else {
        this.addFeed("alert", "Couldn't start a cloud session", detail || "The routine didn't answer", null);
      }
      await this.commit({ kind: "feed" });
      return;
    }
    const m = this.state.machines?.[target];
    if (!m || now - m.seen > MACHINE_GONE_MS) {
      this.addFeed("alert", "That computer isn't online", `Start the Deskling runner on ${target}`, null);
      await this.commit({ kind: "feed" });
      return;
    }
    const path = m.projects.find((p) => p.path === project || p.name === project)?.path ?? m.projects[0]?.path ?? "";
    // Older runners only start Claude Code.
    const eng = m.engines?.find((e) => e.id === engine) ?? m.engines?.[0] ?? { id: "claude", name: "Claude Code", models: [] };
    const mdl = eng.models.find((x) => x.id === model)?.id ?? eng.models[0]?.id ?? "";
    (this.state.jobs ??= []).push({ id: shortId(), machine: m.id, project: path, prompt, ts: now, engine: eng.id, ...(mdl ? { model: mdl } : {}) });
    // A Claude Code session listens for you once it appears; the others always take messages.
    if (eng.id === "claude") (this.state.expectNew ??= []).push({ host: m.id.toLowerCase(), until: now + 5 * 60 * 1000 });
    this.addFeed("good", eng.id === "claude" ? `Starting a session on ${m.id}` : `Starting ${eng.name} on ${m.id}`, oneLine(prompt, 100), null);
    await this.commit({ kind: "feed" });
    this.runnerPolls.get(m.id)?.();
  }

  /** A session you started from the screen has appeared: it listens for you straight away. */
  private adoptNew(agent: Agent): void {
    const now = Date.now();
    const list = (this.state.expectNew ?? []).filter((e) => e.until > now);
    const host = agent.os === "cloud" ? "cloud" : agent.host.toLowerCase();
    const i = list.findIndex((e) => e.host === host);
    if (i >= 0) {
      list.splice(i, 1);
      agent.listenUntil = now + 30 * 60 * 1000;
    }
    this.state.expectNew = list;
  }

  // ------------------------------------------------------------ transcript

  /**
   * New lines of a session's transcript (JSONL, tool output already filtered out by the hook).
   * Claude's words appear as it writes them, with its thoughts, the steps it takes, any
   * pictures, and the model and context size of its latest reply.
   */
  private async onTranscript(sid: string, text: string): Promise<Response> {
    const agent = this.state.agents[sid];
    if (!agent) return json({ ok: true, skipped: "unknown session" });
    const seen = this.seenLines.get(sid) ?? new Set<string>();
    this.seenLines.set(sid, seen);
    let changed = false;
    let message = false;
    for (const line of text.split("\n")) {
      if (!line.startsWith("{")) continue;
      let d: Record<string, unknown>;
      try {
        d = JSON.parse(line) as Record<string, unknown>;
      } catch {
        continue; // a fragment
      }
      const uuid = String(d.uuid ?? "");
      if (uuid) {
        if (seen.has(uuid)) continue;
        seen.add(uuid);
        if (seen.size > 600) seen.delete(seen.values().next().value as string);
      }
      if (d.isSidechain === true || d.isMeta === true) continue; // helpers' own transcripts and bookkeeping
      if (d.type === "queue-operation" && d.operation === "remove" && d.reason === "absorbed_mid_turn" && typeof d.content === "string") {
        // A message you sent while Claude was working (it reads it at its next step).
        const raw = d.content;
        if (/<agent-message\s/.test(raw)) this.finishHandbacks(agent, raw);
        const t = readable(raw);
        const quote = this.quoteFor(agent, raw);
        if (t || quote) {
          this.pushYou(sid, t, quote);
          changed = message = true;
        }
        continue;
      }
      const msg = (d.message ?? {}) as Record<string, unknown>;
      const content = Array.isArray(msg.content) ? (msg.content as Record<string, unknown>[]) : [];
      if (d.type === "assistant") {
        agent.streaming = true;
        if (typeof msg.model === "string" && !msg.model.startsWith("<")) agent.model = msg.model;
        const u = (msg.usage ?? {}) as Record<string, number>;
        const ctx = (u.input_tokens ?? 0) + (u.cache_read_input_tokens ?? 0) + (u.cache_creation_input_tokens ?? 0);
        if (ctx > 0) agent.context = ctx;
        for (const b of content) {
          if (b.type === "thinking" && typeof b.thinking === "string" && b.thinking.trim()) {
            for (const t of thoughtLines(b.thinking)) this.pushChat(sid, "thought", t, "sent");
            changed = true;
          } else if (b.type === "text" && typeof b.text === "string") {
            const t = clip(readable(b.text), CHAT_TEXT_MAX);
            if (t) {
              this.pushChat(sid, "agent", t, "sent");
              agent.lastMessage = t;
              changed = message = true;
            }
          } else if (b.type === "tool_use") {
            const name = String(b.name ?? "");
            const input = (b.input ?? {}) as Record<string, unknown>;
            if (name === "SendUserMessage") {
              const t = clip(readable(input.message ?? ""), CHAT_TEXT_MAX);
              if (t) {
                this.pushChat(sid, "agent", t, "sent");
                agent.lastMessage = t;
                changed = message = true;
              }
            } else if (name !== "Agent" && name !== "Task") {
              this.pushChat(sid, "tool", activity(name, input), "sent");
              changed = true;
            }
          }
        }
      } else if (d.type === "user") {
        // Pictures: ones you attached, and ones Claude looked at (screenshots, photos it opened).
        for (const b of content) {
          if (b.type === "image") changed = (await this.addImage(sid, "you", b)) || changed;
          if (b.type === "tool_result" && Array.isArray(b.content)) {
            const parts = b.content as Record<string, unknown>[];
            // A message you sent while Claude was working rides along in a tool result.
            const yours = parts.some((x) => x.type === "text" && typeof x.text === "string" && /user sent a new message/i.test(x.text));
            for (const x of parts) if (x.type === "image") changed = (await this.addImage(sid, yours ? "you" : "agent", x)) || changed;
          }
        }
      }
    }
    if (changed) {
      agent.updated = Date.now();
      if (message) await this.commit({ kind: "chat", id: sid });
      else await this.commitLive(sid);
    }
    return json({ ok: true });
  }

  private async addImage(sid: string, from: "you" | "agent", block: Record<string, unknown>, key = ""): Promise<boolean> {
    const src = (block.source ?? {}) as Record<string, unknown>;
    if (src.type !== "base64" || typeof src.data !== "string") return false;
    const type = String(src.media_type ?? "image/png");
    if (!/^image\/(png|jpeg|jpg)$/.test(type)) return false;
    let bytes: Uint8Array;
    try {
      const bin = atob(src.data);
      bytes = new Uint8Array(bin.length);
      for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    } catch {
      return false;
    }
    if (bytes.length > IMAGE_MAX_BYTES) return false;
    const id = shortId();
    await this.ctx.storage.put("img:" + id, { type, data: bytes });
    const chat = (this.state.chats[sid] ??= []);
    chat.push({ id: shortId(), from, text: from === "you" ? "" : "Looked at this", status: "sent", ts: Date.now(), image: id, ...(key ? { key } : {}) });
    if (chat.length > CHAT_MAX) chat.splice(0, chat.length - CHAT_MAX);
    // Forget the oldest pictures beyond the last few.
    const ids = chat.filter((c) => c.image).map((c) => c.image as string);
    for (const old of ids.slice(0, Math.max(0, ids.length - IMAGES_KEPT))) {
      await this.ctx.storage.delete("img:" + old);
      for (const c of chat) if (c.image === old) delete c.image;
    }
    return true;
  }

  /**
   * Hold a finished session's Stop hook for one round (HOLD_S), waiting for a message from the
   * screen. A message turns into a new prompt (decision "block"); otherwise the hook is told to
   * ask again while listening lasts, and finally let go so the session goes idle as usual.
   */
  private async listen(agent: Agent, sid: string): Promise<Response> {
    const until = agent.listenUntil ?? 0;
    if (until <= Date.now()) return empty();
    this.listeners.get(sid)?.("cancel"); // only one hold per session
    const why = await new Promise<"message" | "cancel" | "timeout">((resolve) => {
      const timer = setTimeout(() => {
        this.listeners.delete(sid);
        resolve("timeout");
      }, Math.min(HOLD_S * 1000, Math.max(1000, until - Date.now())));
      this.listeners.set(sid, (w) => {
        clearTimeout(timer);
        this.listeners.delete(sid);
        resolve(w);
      });
    });
    // You typed on the computer instead: let go without taking anything from the screen.
    if (why === "cancel") return empty();
    const woke = why === "message";
    const queued = this.takeQueued(sid);
    if (queued.length) {
      this.setAgent(agent, "working", oneLine(queued[0]));
      await this.commit({ kind: "chat", id: sid });
      return json({
        decision: "block",
        reason: `The user sent ${queued.length > 1 ? "these messages" : "this message"} from their Deskling desk screen. Treat it like a new prompt from them:\n${queued.map((m) => `- ${m}`).join("\n")}`,
      });
    }
    if (!woke && (agent.listenUntil ?? 0) > Date.now()) return json({ again: true });
    return empty();
  }

  /** Put a request on the screen and wait for an answer, a decision, or the timeout. */
  private async ask(agent: Agent, kind: Pending["kind"], tool: string, summary: string, detail: string, questions: Question[], wait: number, key = ""): Promise<Resolution> {
    const now = Date.now();
    const p: Pending = {
      id: shortId(),
      ...(key ? { key } : {}),
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
    this.addFeed("alert", kind === "question" ? `${agentLabel(agent.source)} has a question` : "Permission needed", kind === "question" ? `${agent.name} · ${oneLine(summary, 60)}` : `${agent.name} wants to use ${tool}`, agent.id);

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

  /**
   * A permission dialog open in a terminal session with the live channel: show it on the screen
   * and answer it from there (the terminal dialog stays open too; the first answer wins).
   */
  private async onChannelPermission(body: Record<string, unknown>): Promise<Response> {
    const ch = this.channels.get(String(body.cid ?? ""));
    if (!ch) return json({ error: "unknown channel, register again" }, 409);
    if (!ch.agentId) this.linkChannel(ch);
    const agent = ch.agentId ? this.state.agents[ch.agentId] : undefined;
    if (!agent) return json({});
    const tool = oneLine(body.tool_name, 40) || "Tool";
    // Already on the screen from the PermissionRequest hook: that answer is on its way.
    if (this.state.pending.some((p) => p.agentId === agent.id && p.tool === tool)) return json({});
    const wait = Math.min(MAX_WAIT_S, Math.max(5, Number(body.wait ?? MAX_WAIT_S) || MAX_WAIT_S));
    const r = await this.ask(agent, "permission", tool, oneLine(body.input_preview, 160), oneLine(body.description, 120), [], wait);
    if ("decision" in r && (r.decision === "allow" || r.decision === "deny")) return json({ behavior: r.decision });
    return json({});
  }

  /** Match a channel to the Claude Code session on the same machine and folder. */
  private linkChannel(ch: Channel): void {
    const taken = new Set([...this.channels.values()].filter((c) => c !== ch && c.agentId).map((c) => c.agentId));
    const match = Object.values(this.state.agents)
      .filter((a) => a.source === "claude-code" && a.host === ch.host && a.name === ch.name && !taken.has(a.id))
      .sort((a, b) => b.since - a.since)[0];
    if (match) ch.agentId = match.id;
  }

  private bridgeLive(a: Agent): boolean {
    return !!a.polledAt && Date.now() - a.polledAt < CHANNEL_LIVE_MS;
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
      ws.send(this.snapshot({ kind: "hello" }, viewOf(ws)));
      return;
    }
    if (msg.type === "view") {
      // The chat open on that screen: it gets that one in full, the rest as their latest lines.
      const view = oneLine(msg.agentId, 72);
      ws.serializeAttachment({ view });
      ws.send(this.snapshot({ kind: "agent", id: view }, view));
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
      if (agent.source !== "claude-code" && !this.bridgeLive(agent)) {
        this.addFeed("info", "Can't message that one", `${agent.name} doesn't accept messages from Deskling`, agentId);
        await this.commit({ kind: "feed" });
        return;
      }
      this.pushChat(agentId, "you", text, "queued");
      await this.commit({ kind: "chat", id: agentId });
      this.listeners.get(agentId)?.("message");
      for (const wake of this.bridgePolls.values()) wake(agentId);
      for (const ch of this.channels.values()) if (ch.agentId === agentId) this.channelPolls.get(ch.id)?.();
      return;
    }
    if (type === "start") {
      await this.startSession(String(msg.target ?? ""), String(msg.project ?? ""), clip(msg.prompt, 2000), oneLine(msg.engine, 16), oneLine(msg.model, 60));
      return;
    }
    if (type === "listen") {
      // "Keep listening": when the session finishes, its Stop hook waits for messages from here.
      const a = this.state.agents[String(msg.agentId ?? "")];
      if (!a) return;
      const minutes = Math.max(0, Math.min(LISTEN_MAX_MIN, Number(msg.minutes ?? 30) || 0));
      a.listenUntil = minutes ? Date.now() + minutes * 60 * 1000 : 0;
      if (!minutes) this.listeners.get(a.id)?.("cancel");
      await this.commit({ kind: "agent", id: a.id });
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

  // ------------------------------------------------------------ helpers (subagents)

  private helper(agent: Agent, id: string, name = ""): Helper {
    const list = (agent.helpers ??= []);
    let h = list.find((x) => x.id === id);
    if (!h) {
      h = { id, name: name || "Helper", task: "", done: false, updated: Date.now() };
      list.push(h);
      // Keep the working ones and the most recent finished ones.
      const now = Date.now();
      agent.helpers = list.filter((x) => !x.done || now - x.updated < HELPER_KEEP_MS).slice(-HELPER_MAX);
    }
    return h;
  }

  /** Subagent hand-backs arrive in the session as prompts: mark those helpers finished. */
  private finishHandbacks(agent: Agent, prompt: string): number {
    let n = 0;
    for (const m of prompt.matchAll(/<agent-message\s+from="([^"]+)"/g)) {
      const h = agent.helpers?.find((x) => x.id === m[1]);
      if (h) {
        h.done = true;
        h.task = "";
        h.updated = Date.now();
      }
      n++;
    }
    return n;
  }

  /** The excerpts a message replies to (the app attaches them as @"…excerpt….txt" files). */
  private quoteFor(agent: Agent, raw: string): string {
    const out: string[] = [];
    for (const m of raw.matchAll(/([^"\\/]+excerpt_from_previous_claude_message\.txt)/g)) {
      const q = agent.quotes?.[m[1]];
      if (q) out.push(q);
    }
    return out.join("\n");
  }

  private pushYou(sid: string, text: string, quote: string): void {
    this.pushChat(sid, "you", clip(text, CHAT_TEXT_MAX), "typed");
    const line = this.state.chats[sid]?.at(-1);
    if (line && quote) line.quote = clip(quote, 400);
  }

  private upsertAgent(id: string, source: Source, name: string, host: string, os: string): Agent {
    const now = Date.now();
    let a = this.state.agents[id];
    if (!a) {
      a = { id, source, name, host, os, state: "idle", task: "", lastPrompt: "", lastMessage: "", since: now, updated: now };
      this.state.agents[id] = a;
      this.adoptNew(a);
    }
    a.source = source;
    // The first name sticks: later events come from wherever Claude has cd'd to.
    if (!a.name || a.name === "claude") a.name = name || a.name;
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
      else if ((a.state === "working" || a.state === "waiting") && now - a.updated > QUIET_MS) this.setAgent(a, "idle", "Quiet for a while");
    }
    this.state.reminders = this.state.reminders.filter((r) => !r.done || (r.doneAt ?? 0) > now - REMINDER_KEEP_DONE_MS);
    this.state.pending = this.state.pending.filter((p) => this.waiters.has(p.id));
    for (const [id, ch] of this.channels) if (now - ch.lastPoll > 10 * 60 * 1000) this.channels.delete(id);
    if (this.state.jobs?.length) this.state.jobs = this.state.jobs.filter((j) => now - j.ts < 10 * 60 * 1000);
  }

  /**
   * `view`: the chat open on the screen it's for. That one gets its full chat; the others get
   * their latest lines (enough for notifications). null (screens from before views) gets all.
   */
  private snapshot(cause: Cause, view: string | null = null): string {
    const s = this.state;
    const rank: Record<AgentState, number> = { needs_you: 0, waiting: 1, working: 2, done: 3, idle: 4 };
    const agents = Object.values(s.agents)
      .sort((a, b) => rank[a.state] - rank[b.state] || b.updated - a.updated)
      .slice(0, 12)
      .map((a) => ({
        ...a,
        lastMessage: clip(a.lastMessage, 280),
        lastPrompt: clip(a.lastPrompt, 160),
        channel: this.channelLive(a.id) || this.bridgeLive(a),
        canMessage: a.source === "claude-code" || this.bridgeLive(a),
        chat: view === null || view === a.id ? chatView(s.chats[a.id] ?? []) : chatView(s.chats[a.id] ?? []).slice(-TAIL_CHAT),
        helpers: (a.helpers ?? []).map(({ id, name, task, done }) => ({ id, name, task, done })),
        helperNames: undefined,
        listening: (a.listenUntil ?? 0) > Date.now(),
        listenUntil: a.listenUntil ?? 0,
        holding: this.listeners.has(a.id),
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
      // Where the screen can start a new session.
      targets: [
        ...(s.routine ? [{ id: "cloud", name: s.routine.name || "Cloud", kind: "cloud", projects: [] as string[] }] : []),
        ...this.machinesOnline().map((m) => ({ id: m.id, name: m.id, kind: "computer", projects: m.projects.map((p) => p.name).slice(0, 12), engines: m.engines ?? [] })),
      ],
    });
  }

  private async save(): Promise<void> {
    await this.ctx.storage.put("state", this.state);
  }

  /**
   * Live activity arrives with every tool call. Screens get it at most every 1.5 s (the last
   * one always lands), so a busy session doesn't make them redraw constantly.
   */
  private liveAt = 0;
  private liveTimer: ReturnType<typeof setTimeout> | null = null;
  private async commitLive(sid: string): Promise<void> {
    const now = Date.now();
    if (now - this.liveAt >= 1500 && !this.liveTimer) {
      this.liveAt = now;
      await this.commit({ kind: "agent", id: sid });
      return;
    }
    await this.save();
    if (this.liveTimer) return;
    this.liveTimer = setTimeout(() => {
      this.liveTimer = null;
      this.liveAt = Date.now();
      void this.commit({ kind: "agent", id: sid });
    }, Math.max(100, 1500 - (now - this.liveAt)));
  }

  /** Persist, re-arm reminders and push a fresh snapshot to every screen. */
  private async commit(cause: Cause): Promise<void> {
    this.prune();
    await this.save();
    await this.scheduleAlarm();
    const byView = new Map<string | null, string>();
    for (const ws of this.ctx.getWebSockets()) {
      const view = viewOf(ws);
      let msg = byView.get(view);
      if (msg === undefined) byView.set(view, (msg = this.snapshot(cause, view)));
      try {
        ws.send(msg);
      } catch {
        /* socket went away */
      }
    }
  }
}
