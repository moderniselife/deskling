#!/usr/bin/env node
// Deskling MCP server for Claude Code. Zero dependencies, Node 18+.
//
// Always: gives Claude three tools that talk to your Deskling screen
//   deskling_notify  put a notification in the feed
//   deskling_remind  set a reminder ("remind me at 3pm to ...")
//   deskling_reply   answer you in the session's chat on the screen
//
// As a channel (research preview; start Claude with
//   claude --channels plugin:deskling@deskling
// ): messages you type on the screen arrive in this session straight away,
// even while it's idle, and Claude can answer with deskling_reply.
//
// Settings: DESKLING_URL / DESKLING_TOKEN / DESKLING_NAME (env, or the plugin
// options, or ~/.deskling/config). DESKLING_CHANNEL=1 forces channel mode on.

import { readFileSync } from "node:fs";
import { homedir, hostname, platform } from "node:os";
import { join } from "node:path";
import { createInterface } from "node:readline";

const VERSION = "0.3.0";

// ~/.deskling/config, over the deskling.conf a personal copy of the plugin carries.
function fileConfig() {
  const out = {};
  const files = [];
  if (process.env.CLAUDE_PLUGIN_ROOT) files.push(join(process.env.CLAUDE_PLUGIN_ROOT, "deskling.conf"));
  files.push(join(homedir(), ".deskling", "config"));
  for (const f of files) {
    try {
      for (const line of readFileSync(f, "utf8").split(/\r?\n/)) {
        const m = line.match(/^\s*(?:export\s+)?([A-Z_]+)\s*=\s*"?([^"]*)"?\s*$/);
        if (m) out[m[1]] = m[2];
      }
    } catch {
      /* not there */
    }
  }
  return out;
}
const fc = fileConfig();
const env = process.env;
const pick = (...vals) => vals.find((v) => typeof v === "string" && v.trim() && !v.includes("${")) ?? "";
const URL_BASE = pick(env.DESKLING_URL, env.CLAUDE_PLUGIN_OPTION_RELAY_URL, fc.DESKLING_URL).replace(/\/+$/, "");
const TOKEN = pick(env.DESKLING_TOKEN, env.CLAUDE_PLUGIN_OPTION_TOKEN, fc.DESKLING_TOKEN);
const HOST = (pick(env.DESKLING_NAME, env.CLAUDE_PLUGIN_OPTION_DEVICE_NAME, fc.DESKLING_NAME) || hostname()).split(".")[0];
const OS = { darwin: "mac", win32: "windows", linux: "linux" }[platform()] ?? platform();
const CWD = env.CLAUDE_PROJECT_DIR || process.cwd();

const log = (...a) => process.stderr.write(`[deskling] ${a.join(" ")}\n`);
const send = (msg) => process.stdout.write(JSON.stringify(msg) + "\n");

async function relay(path, init = {}) {
  if (!URL_BASE || !TOKEN) throw new Error("Deskling isn't configured (relay URL and token)");
  const res = await fetch(URL_BASE + path, {
    ...init,
    headers: { authorization: `Bearer ${TOKEN}`, "content-type": "application/json", ...(init.headers ?? {}) },
  });
  const body = await res.json().catch(() => ({}));
  return { status: res.status, body };
}

// ------------------------------------------------------------------ tools

const TOOLS = [
  {
    name: "deskling_notify",
    description: "Show a notification on the user's Deskling desk screen. Use for things they'd want to glance at: a long task finishing, a build result, something that needs them later.",
    inputSchema: {
      type: "object",
      properties: {
        title: { type: "string", description: "Short headline, a few words" },
        body: { type: "string", description: "One line of detail" },
        level: { type: "string", enum: ["info", "good", "alert"], description: "good = success, alert = needs attention" },
      },
      required: ["title"],
    },
  },
  {
    name: "deskling_remind",
    description: "Set a reminder on the user's Deskling desk screen. It pops up with an animation when it's due. Use when the user asks to be reminded of something, or when you agree to follow up later.",
    inputSchema: {
      type: "object",
      properties: {
        title: { type: "string", description: "What to remind them about" },
        at: { type: "string", description: "When, as an ISO 8601 date-time with timezone offset" },
        in_minutes: { type: "number", description: "Or: how many minutes from now" },
      },
      required: ["title"],
    },
  },
  {
    name: "deskling_reply",
    description: "Reply to the user in this session's chat on their Deskling desk screen. Use this to answer messages that arrived from Deskling (they come in as <channel source=\"deskling\"> events), so the answer shows where they're reading.",
    inputSchema: {
      type: "object",
      properties: { text: { type: "string", description: "Your reply. Keep it short: it's shown on a small screen." } },
      required: ["text"],
    },
  },
];

async function callTool(name, args) {
  if (name === "deskling_notify") {
    const { body } = await relay("/api/event", { method: "POST", body: JSON.stringify({ kind: "notify", title: args.title, body: args.body ?? "", level: args.level ?? "info" }) });
    return body.ok ? "Shown on Deskling." : `Deskling said: ${JSON.stringify(body)}`;
  }
  if (name === "deskling_remind") {
    const { body } = await relay("/api/event", {
      method: "POST",
      body: JSON.stringify({ kind: "reminder", title: args.title, at: args.at, in_minutes: args.in_minutes, from: "Claude" }),
    });
    if (!body.ok) return `Deskling said: ${JSON.stringify(body)}`;
    return body.due ? `Reminder set for ${new Date(body.due).toString()}.` : "Reminder added (no time set).";
  }
  if (name === "deskling_reply") {
    await ensureChannel();
    const { status, body } = await relay("/channel/reply", { method: "POST", body: JSON.stringify({ cid: channelId, text: args.text }) });
    if (status === 409) {
      channelId = null;
      await ensureChannel();
      await relay("/channel/reply", { method: "POST", body: JSON.stringify({ cid: channelId, text: args.text }) });
    } else if (!body.ok) return `Deskling said: ${JSON.stringify(body)}`;
    return "Sent to Deskling.";
  }
  throw new Error(`unknown tool ${name}`);
}

// ---------------------------------------------------------------- channel

let channelId = null;
let channelMode = env.DESKLING_CHANNEL === "1";
let polling = false;

async function ensureChannel() {
  if (channelId) return channelId;
  const { body } = await relay("/channel/register", { method: "POST", body: JSON.stringify({ host: HOST, os: OS, cwd: CWD }) });
  channelId = body.cid;
  return channelId;
}

async function pollLoop() {
  if (polling) return;
  polling = true;
  let backoff = 1000;
  for (;;) {
    try {
      await ensureChannel();
      const { status, body } = await relay(`/channel/poll?cid=${encodeURIComponent(channelId)}&wait=25`);
      if (status === 409) {
        channelId = null;
        continue;
      }
      for (const text of body.messages ?? []) {
        send({ jsonrpc: "2.0", method: "notifications/claude/channel", params: { content: text, meta: { from: "deskling_screen" } } });
      }
      backoff = 1000;
    } catch (e) {
      log("poll failed:", e.message);
      await new Promise((r) => setTimeout(r, backoff));
      backoff = Math.min(backoff * 2, 30000);
    }
  }
}

/* A permission dialog: ask on Deskling (the relay waits up to ~110 s per round) and answer it if you tap. */
async function relayPermission(p) {
  if (!p.request_id || !URL_BASE || !TOKEN) return;
  try {
    for (let round = 0; round < 5; round++) {
      await ensureChannel();
      const { status, body } = await relay("/channel/permission", {
        method: "POST",
        body: JSON.stringify({ cid: channelId, request_id: p.request_id, tool_name: p.tool_name, description: p.description, input_preview: p.input_preview, wait: 110 }),
      });
      if (status === 409) {
        channelId = null;
        continue;
      }
      if (body.behavior === "allow" || body.behavior === "deny") {
        send({ jsonrpc: "2.0", method: "notifications/claude/channel/permission", params: { request_id: p.request_id, behavior: body.behavior } });
      }
      return;
    }
  } catch (e) {
    log("permission relay failed:", e.message);
  }
}

// ------------------------------------------------------------ JSON-RPC loop

const INSTRUCTIONS = [
  "Deskling is the user's little desk screen. It shows what your sessions are doing and lets them answer from across the room.",
  "Messages typed on Deskling arrive as <channel source=\"deskling\"> events. Treat them like the user typing to you, and answer with the deskling_reply tool so the answer appears on the screen.",
  "Use deskling_remind when the user asks to be reminded of something, and deskling_notify for results they'd want to glance at later.",
].join(" ");

async function handle(msg) {
  const { id, method, params } = msg;
  if (method === "initialize") {
    const exp = params?.capabilities?.experimental ?? {};
    if (exp["claude/channel"]) channelMode = true;
    send({
      jsonrpc: "2.0",
      id,
      result: {
        protocolVersion: params?.protocolVersion ?? "2025-06-18",
        // claude/channel/permission: Claude Code also sends us its permission dialogs, so they
        // can be answered on Deskling (Claude Code 2.1.234 or later, with --channels).
        capabilities: { tools: {}, experimental: { "claude/channel": {}, "claude/channel/permission": {} } },
        serverInfo: { name: "deskling", version: VERSION },
        instructions: INSTRUCTIONS,
      },
    });
    return;
  }
  if (method === "notifications/initialized") {
    if (channelMode && URL_BASE && TOKEN) pollLoop();
    return;
  }
  if (method === "notifications/claude/channel/permission_request") {
    relayPermission(params ?? {});
    return;
  }
  if (method === "ping") return send({ jsonrpc: "2.0", id, result: {} });
  if (method === "tools/list") return send({ jsonrpc: "2.0", id, result: { tools: TOOLS } });
  if (method === "tools/call") {
    try {
      const text = await callTool(params?.name, params?.arguments ?? {});
      send({ jsonrpc: "2.0", id, result: { content: [{ type: "text", text }] } });
    } catch (e) {
      send({ jsonrpc: "2.0", id, result: { content: [{ type: "text", text: `Couldn't reach Deskling: ${e.message}` }], isError: true } });
    }
    return;
  }
  if (id !== undefined) send({ jsonrpc: "2.0", id, error: { code: -32601, message: `method not found: ${method}` } });
}

const rl = createInterface({ input: process.stdin });
rl.on("line", (line) => {
  if (!line.trim()) return;
  let msg;
  try {
    msg = JSON.parse(line);
  } catch {
    return;
  }
  handle(msg).catch((e) => log("error:", e.message));
});
rl.on("close", () => process.exit(0));
