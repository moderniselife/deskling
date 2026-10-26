// Deskling for OpenCode: shows your OpenCode sessions on your Deskling, live, and lets you answer
// from the screen. Copy this file to ~/.config/opencode/plugins/deskling.js (or a project's
// .opencode/plugins/). It reads your relay from DESKLING_URL / DESKLING_TOKEN or ~/.deskling/config.
//
// What reaches the screen: each session (named after its folder), what it's doing, its replies as
// they stream, its thinking, each tool step, subagents as helpers, the model and context size.
// From the screen: allow or deny its permission prompts, answer its questions, and send messages
// (they start it straight away, even when it's idle).
//
// It also gives OpenCode's agents three tools for the screen: deskling_notify (a notification),
// deskling_remind (a reminder at a time) and deskling_ask (a question with options, answered on the
// screen). The Deskling agents in hooks/opencode/agents use them.
//
// Works with OpenCode 1.x (the opencode-ai package): in the TUI and in `opencode serve`.
import { readFileSync } from "node:fs";
import { homedir, hostname, platform } from "node:os";
import { basename, join } from "node:path";

const VERSION = "0.1.0";

function fileConfig() {
  const out = {};
  try {
    for (const line of readFileSync(join(homedir(), ".deskling", "config"), "utf8").split(/\r?\n/)) {
      const m = line.match(/^\s*(?:export\s+)?([A-Z_]+)\s*=\s*"?([^"]*)"?\s*$/);
      if (m) out[m[1]] = m[2];
    }
  } catch {
    /* none */
  }
  return out;
}

function activity(tool, input = {}, title = "") {
  const s = (k) => (typeof input[k] === "string" ? input[k] : "");
  const file = (p) => basename(p || "");
  let t;
  switch (tool) {
    case "bash":
      t = s("description") || "Running " + s("command");
      break;
    case "read":
      t = "Reading " + file(s("filePath"));
      break;
    case "edit":
    case "patch":
    case "multiedit":
      t = "Editing " + file(s("filePath"));
      break;
    case "write":
      t = "Writing " + file(s("filePath"));
      break;
    case "grep":
    case "glob":
      t = "Searching for " + (s("pattern") || "files");
      break;
    case "list":
      t = "Looking in " + (file(s("path")) || "the folder");
      break;
    case "webfetch":
      t = "Reading " + s("url").replace(/^https?:\/\//, "");
      break;
    case "todowrite":
    case "todoread":
      t = "Updating its to-do list";
      break;
    case "task":
      t = "Starting a helper: " + (s("description") || "a subagent");
      break;
    default:
      t = title || "Using " + tool;
  }
  t = t.replace(/\s+/g, " ").trim();
  return t.length > 90 ? t.slice(0, 89) + "…" : t;
}

const firstLines = (t) =>
  String(t || "")
    .split(/\n+/)
    .map((l) => l.trim())
    .filter((l) => l && !/^[-.…]+$/.test(l))
    .slice(0, 3)
    .join("\n");

export const DesklingPlugin = async ({ directory, serverUrl }) => {
  const fc = fileConfig();
  const env = process.env;
  const URL_BASE = (env.DESKLING_URL || fc.DESKLING_URL || "").replace(/\/+$/, "");
  const TOKEN = env.DESKLING_TOKEN || fc.DESKLING_TOKEN || "";
  if (!URL_BASE || !TOKEN) return {};
  // Started by the Deskling runner over ACP: the runner already shows this session.
  if (env.DESKLING_BRIDGED === "1") return {};
  const HOST = (env.DESKLING_NAME || fc.DESKLING_NAME || hostname()).split(".")[0];
  const OS = { darwin: "mac", win32: "windows", linux: "linux" }[platform()] ?? platform();
  const WAIT = Math.max(5, Math.min(110, Number(env.DESKLING_WAIT || fc.DESKLING_WAIT || 110) || 110));
  const auth = { authorization: `Bearer ${TOKEN}`, "content-type": "application/json" };
  // The OpenCode server this plugin runs in (to send prompts and answers back to it).
  const ocHeaders = { "content-type": "application/json" };
  if (env.OPENCODE_SERVER_PASSWORD) ocHeaders.authorization = "Basic " + btoa(`${env.OPENCODE_SERVER_USERNAME || "opencode"}:${env.OPENCODE_SERVER_PASSWORD}`);
  const oc = (path, body) => fetch(new URL(path, serverUrl), { method: "POST", headers: ocHeaders, body: JSON.stringify(body) });

  const sessions = new Map(); // sessionID -> { parentID, directory, title, busy, helpers: Map }
  const roles = new Map(); // messageID -> "user" | "assistant"
  const texts = new Map(); // partID -> streamed text so far
  const agentId = (sid) => `oc-${sid}`;
  const root = (sid) => {
    let s = sessions.get(sid);
    let id = sid;
    for (let i = 0; s?.parentID && i < 5; i++) {
      id = s.parentID;
      s = sessions.get(id);
    }
    return id;
  };
  const session = (sid) => {
    if (!sessions.has(sid)) sessions.set(sid, { parentID: null, directory, title: "", busy: false, helpers: new Map() });
    return sessions.get(sid);
  };

  // ---- batched updates to the relay (streaming text arrives many times a second)
  const pending = new Map(); // sid -> { fields, lines: Map(key -> line) }
  let timer = null;
  function queue(sid, fields = {}, line = null) {
    const p = pending.get(sid) ?? { fields: {}, lines: new Map() };
    Object.assign(p.fields, fields);
    if (line) p.lines.set(line.key, line);
    pending.set(sid, p);
    if (!timer) timer = setTimeout(flush, 300);
  }
  async function flush() {
    timer = null;
    const batch = [...pending.entries()];
    pending.clear();
    for (const [sid, p] of batch) {
      const s = session(sid);
      const body = {
        id: agentId(sid),
        source: "opencode",
        name: basename(s.directory || directory) || "opencode",
        host: HOST,
        os: OS,
        ...p.fields,
        lines: [...p.lines.values()],
      };
      if (s.helpers.size) body.helpers = [...s.helpers.values()].slice(-4);
      try {
        await fetch(`${URL_BASE}/agent/update`, { method: "POST", headers: auth, body: JSON.stringify(body) });
      } catch {
        /* relay unreachable: the next update carries on */
      }
    }
  }

  // ---- messages typed on the screen become prompts here, straight away
  let polling = false;
  async function pollLoop() {
    if (polling) return;
    polling = true;
    for (;;) {
      const ids = [...sessions.entries()].filter(([, s]) => !s.parentID).map(([sid]) => agentId(sid));
      if (!ids.length) {
        await new Promise((r) => setTimeout(r, 3000));
        continue;
      }
      try {
        const res = await fetch(`${URL_BASE}/agent/poll?ids=${encodeURIComponent(ids.join(","))}&wait=25`, { headers: auth });
        const { messages = [] } = await res.json();
        for (const m of messages) {
          const sid = m.id.replace(/^oc-/, "");
          await oc(`/session/${sid}/prompt_async`, { parts: [{ type: "text", text: m.text }] });
        }
      } catch {
        await new Promise((r) => setTimeout(r, 5000));
      }
    }
  }

  async function ask(sid, body) {
    try {
      const res = await fetch(`${URL_BASE}/agent/ask`, { method: "POST", headers: auth, body: JSON.stringify({ id: agentId(root(sid)), wait: WAIT, ...body }) });
      return await res.json();
    } catch {
      return {};
    }
  }

  async function onPermission(p) {
    const meta = p.metadata || {};
    const summary = meta.command || meta.filePath || meta.filepath || (p.patterns || []).join(" ") || p.permission;
    const r = await ask(p.sessionID, { key: p.id, kind: "permission", tool: { bash: "Bash", edit: "Edit", write: "Write", read: "Read", webfetch: "WebFetch", websearch: "WebSearch", task: "Task", external_directory: "Folder access", doom_loop: "Keep going" }[p.permission] || p.permission, summary, detail: meta.description || "" });
    if (r.decision !== "allow" && r.decision !== "deny") return; // answered on the computer, or no answer: OpenCode's own prompt stays
    const reply = r.decision === "allow" ? "once" : "reject";
    const ok = await oc(`/permission/${p.id}/reply`, { reply }).then((x) => x.ok).catch(() => false);
    if (!ok) await oc(`/session/${p.sessionID}/permissions/${p.id}`, { response: reply }).catch(() => {});
  }

  async function onQuestion(q) {
    const questions = (q.questions || []).map((x) => ({ question: x.question, header: x.header || "", multiSelect: !!x.multiple, options: (x.options || []).map((o) => ({ label: o.label, description: o.description || "" })) }));
    const r = await ask(q.sessionID, { key: q.id, kind: "question", tool: "Question", summary: questions[0]?.question || "", detail: questions[0]?.header || "", questions });
    if (!r.answers) return;
    const answers = questions.map((x) => {
      const a = r.answers[x.question];
      return Array.isArray(a) ? a : a ? String(a).split("\n") : [];
    });
    await oc(`/question/${q.id}/reply`, { answers }).catch(() => {});
  }

  pollLoop();

  // ---- tools for the agents: the screen as an output (and a way to ask you)
  const str = (description) => ({ type: "string", description });
  const tool = {
    deskling_notify: {
      description:
        "Show a short notification on the person's Deskling desk screen. Use it for things they'd want to know even when they're away from the computer: a long job finished, something failed, or you're blocked.",
      args: { title: str("A few words, like 'Tests pass' or 'Deploy failed'"), body: str("One short sentence with the detail, or an empty string") },
      async execute(args) {
        const r = await fetch(`${URL_BASE}/api/event`, { method: "POST", headers: auth, body: JSON.stringify({ kind: "notify", title: String(args.title).slice(0, 80), body: String(args.body || "").slice(0, 200), level: "info" }) }).catch(() => null);
        return r?.ok ? "Shown on Deskling." : "Deskling didn't answer; carry on without it.";
      },
    },
    deskling_remind: {
      description: "Set a reminder on the person's Deskling screen that goes off later, for example to check a deploy or review a pull request.",
      args: { title: str("What to remember, like 'Check the preview deploy'"), in_minutes: { type: "number", description: "Minutes from now" } },
      async execute(args) {
        const at = Math.round(Date.now() / 1000 + Math.max(1, Number(args.in_minutes) || 30) * 60);
        const r = await fetch(`${URL_BASE}/api/event`, { method: "POST", headers: auth, body: JSON.stringify({ kind: "reminder", title: String(args.title).slice(0, 80), at, from: "OpenCode" }) }).catch(() => null);
        return r?.ok ? `Reminder set for ${new Date(at * 1000).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" })}.` : "Deskling didn't answer; the reminder wasn't set.";
      },
    },
    deskling_ask: {
      description:
        "Ask the person a question on their Deskling desk screen and wait for the answer (up to about two minutes). Give 2 to 4 short options. Use it for a decision only they can make; if there's no answer, pick the safest option and say so.",
      args: { question: str("The question, one sentence"), options: { type: "array", items: { type: "string" }, description: "2 to 4 short answers to choose from" } },
      async execute(args, ctx) {
        const options = (Array.isArray(args.options) ? args.options : []).map(String).filter(Boolean).slice(0, 4);
        const question = String(args.question).slice(0, 200);
        session(ctx.sessionID);
        queue(root(ctx.sessionID)); // make sure the screen knows the session before asking
        await flush();
        const r = await ask(ctx.sessionID, { kind: "question", tool: "Question", summary: question, detail: "", wait: 110, questions: [{ question, header: "", multiSelect: false, options: options.map((label) => ({ label, description: "" })) }] });
        const a = r.answers?.[question];
        if (a) return `They answered: ${Array.isArray(a) ? a.join(", ") : a}`;
        return r.timeout ? "No answer from Deskling in time." : "Deskling didn't answer.";
      },
    },
  };

  return {
    tool,
    event: async ({ event }) => {
      const t = event?.type;
      const p = event?.properties || {};
      try {
        if (t === "session.created" || t === "session.updated") {
          const info = p.info || {};
          const s = session(info.id);
          s.parentID = info.parentID || null;
          s.directory = info.directory || s.directory;
          s.title = info.title || s.title;
          if (s.parentID) {
            const parent = session(s.parentID);
            const h = parent.helpers.get(info.id) || { id: info.id, name: s.title || "Helper", task: "", done: false };
            h.name = (s.title || h.name).replace(/\s*\(@\w+ subagent\)$/, "");
            parent.helpers.set(info.id, h);
            queue(s.parentID);
          } else {
            queue(info.id, t === "session.created" ? { state: "idle", task: "Ready for a prompt" } : {});
          }
        } else if (t === "session.deleted") {
          const sid = p.info?.id;
          sessions.delete(sid);
          await fetch(`${URL_BASE}/agent/update`, { method: "POST", headers: auth, body: JSON.stringify({ id: agentId(sid), end: true }) }).catch(() => {});
        } else if (t === "session.status") {
          const sid = p.sessionID;
          const s = session(sid);
          const type = p.status?.type;
          if (s.parentID) {
            const h = session(s.parentID).helpers.get(sid);
            if (h) {
              h.done = type === "idle";
              if (h.done) h.task = "";
              queue(s.parentID);
            }
          } else if (type === "busy" && !s.busy) {
            s.busy = true;
            queue(sid, { state: "working", task: "Working" });
          } else if (type === "retry") {
            queue(sid, { state: "working", task: "Retrying: " + (p.status.message || "") });
          } else if (type === "idle" && s.busy) {
            s.busy = false;
            queue(sid, { state: "done", task: "Finished" });
          }
        } else if (t === "message.updated") {
          const info = p.info || {};
          roles.set(info.id, info.role);
          if (info.role === "assistant" && !session(info.sessionID).parentID) {
            const tk = info.tokens || {};
            const ctx = (tk.input || 0) + (tk.cache?.read || 0) + (tk.cache?.write || 0);
            queue(info.sessionID, { model: info.modelID ? `${info.providerID}/${info.modelID}` : undefined, ...(ctx ? { context: ctx } : {}) });
          }
        } else if (t === "message.part.delta") {
          if (p.field !== "text") return;
          const text = (texts.get(p.partID) || "") + (p.delta || "");
          texts.set(p.partID, text);
          const s = session(p.sessionID);
          if (s.parentID || roles.get(p.messageID) !== "assistant") return;
          queue(p.sessionID, {}, { key: p.partID, from: "agent", text });
        } else if (t === "message.part.updated") {
          const part = p.part || {};
          const sid = part.sessionID;
          const s = session(sid);
          if (s.parentID) {
            // A helper's screenshots show in the main chat.
            if (part.type === "tool" && part.state?.status === "completed" && Array.isArray(part.state.attachments))
              part.state.attachments.forEach((a, i) => {
                if (/^image\/(png|jpeg)$/.test(a.mime || "") && String(a.url || "").startsWith("data:")) queue(root(sid), {}, { key: `${part.id}-${i}`, from: "agent", text: "", image: a.url });
              });
            if (part.type === "tool" && part.state?.status === "running") {
              const h = session(s.parentID).helpers.get(sid);
              if (h) {
                h.task = activity(part.tool, part.state.input, part.state.title);
                queue(s.parentID);
              }
            }
            return;
          }
          if (part.type === "text" && !part.synthetic && part.text) {
            texts.set(part.id, part.text);
            const role = roles.get(part.messageID);
            if (role === "user") queue(sid, {}, { key: part.id, from: "you", text: part.text });
            else if (role === "assistant") queue(sid, {}, { key: part.id, from: "agent", text: part.text });
          } else if (part.type === "file" && /^image\/(png|jpeg)$/.test(part.mime || "") && String(part.url || "").startsWith("data:")) {
            // A picture you attached, or one it produced.
            queue(sid, {}, { key: part.id, from: roles.get(part.messageID) === "user" ? "you" : "agent", text: "", image: part.url });
          } else if (part.type === "reasoning" && part.text) {
            const text = firstLines(part.text);
            if (text) queue(sid, {}, { key: part.id, from: "thought", text });
          } else if (part.type === "tool") {
            const st = part.state || {};
            if (st.status === "running" || st.status === "completed" || st.status === "error") {
              const text = activity(part.tool, st.input, st.title);
              queue(sid, st.status === "running" ? { task: text } : {}, { key: part.id, from: "tool", text: st.status === "error" ? text + " (failed)" : text });
            }
            // Pictures a tool looked at (screenshots, images it read).
            if (st.status === "completed" && Array.isArray(st.attachments))
              st.attachments.forEach((a, i) => {
                if (/^image\/(png|jpeg)$/.test(a.mime || "") && String(a.url || "").startsWith("data:")) queue(sid, {}, { key: `${part.id}-${i}`, from: "agent", text: "", image: a.url });
              });
            if (part.tool === "task" && st.metadata?.sessionId) {
              const h = s.helpers.get(st.metadata.sessionId) || { id: st.metadata.sessionId, name: st.input?.description || "Helper", task: "", done: false };
              h.done = st.status === "completed" || st.status === "error";
              s.helpers.set(h.id, h);
              queue(sid);
            }
          }
        } else if (t === "permission.asked" || t === "permission.updated") {
          if (p.id && p.sessionID) onPermission(p);
        } else if (t === "question.asked") {
          if (p.id && p.sessionID) onQuestion(p);
        } else if (t === "permission.replied" || t === "question.replied" || t === "question.rejected") {
          // Answered (here or on the screen): take it off the screen if it's still there.
          const key = p.requestID || p.permissionID || p.id;
          if (key && p.sessionID)
            fetch(`${URL_BASE}/agent/resolve`, { method: "POST", headers: auth, body: JSON.stringify({ id: agentId(root(p.sessionID)), key }) }).catch(() => {});
        }
      } catch {
        /* never get in OpenCode's way */
      }
    },
  };
};

export default { id: "deskling", server: DesklingPlugin };
