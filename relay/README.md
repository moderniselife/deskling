# Deskling relay

A Cloudflare Worker with one Durable Object. Agents post events to it, and the screen holds a WebSocket to it. It's the only piece that has to be reachable from everywhere, so local and cloud agents both work without opening ports at home.

## Deploy

One click (it asks for `DESKLING_TOKEN`, a long random password you make up):

[![Deploy to Cloudflare](https://deploy.workers.cloudflare.com/button)](https://deploy.workers.cloudflare.com/?url=https://github.com/moderniselife/deskling/tree/main/relay)

Or from a terminal, with a free Cloudflare account and Node 18+:

```bash
cd relay
npm install
npx wrangler login
npx wrangler secret put DESKLING_TOKEN   # paste a long random string, e.g. from: openssl rand -hex 24
npx wrangler deploy
```

Wrangler prints your URL, e.g. `https://deskling-relay.<you>.workers.dev`. You need that URL and the token for the hooks, the CLI and the screen.

Pages on the relay (no secrets in them; they ask for the token and keep it in the browser tab):

| Page | What it's for |
| --- | --- |
| `/setup` (`/` redirects here) | Your relay URL and token to copy, plugin install commands, `~/.deskling/config`, live screen count, and buttons that send a test notification, reminder, permission request and question |
| `/sim` | A browser version of Deskling that speaks the same protocol as the firmware |
| `/flash` | Browser installer for the firmware (Chrome/Edge, USB). It reads builds from `FIRMWARE_URL` (default: the update server at `https://deskling.mojolayers.com/firmware/`) |

The whole walkthrough is in [docs/setup.md](../docs/setup.md).

## Local dev

```bash
echo DESKLING_TOKEN=dev-token > .dev.vars
npm run dev                       # http://127.0.0.1:8787, simulator at /sim
npm test                          # end-to-end smoke test against the dev server
```

## API

Every request except the pages above needs `Authorization: Bearer <DESKLING_TOKEN>`. The WebSocket also accepts `?token=`.

### From agents

| Endpoint | What it does |
| --- | --- |
| `POST /hook` | Claude Code hook JSON as-is. Headers: `X-Deskling-Host`, `X-Deskling-OS`, `X-Deskling-Source` (`claude-code` default, or `cursor`). `?wait=N` sets how long permission and question requests wait for the screen (max 110 s). For Cursor, `?approve=1` enables screen approvals. The response body is the hook output Claude Code or Cursor expects (often empty). |
| `POST /api/event` | `{"kind":"notify","title","body","level":"info|good|alert"}` |
| | `{"kind":"reminder","title","at":"ISO or epoch","in_minutes":N,"from"}` |
| | `{"kind":"status","id","name","state":"idle|working|waiting|needs_you|done","task","message","source","host"}`. Add `"remove":true` to drop it. |
| | `{"kind":"clear_reminder","id"}` |
| `GET /api/state` | Everything, as JSON |
| `GET /api/health` | `{ok, screens, agents, pending}` |
| `POST /channel/register` | `{"host","os","cwd"}` → `{"cid","agentId"}`. Used by the Claude Code channel server. |
| `GET /channel/poll?cid=&wait=25` | Long-polls for messages typed on the screen for that session |
| `POST /channel/reply` | `{"cid","text"}`: Claude's reply, shown in the session's chat |

### The screen (WebSocket `/ws`)

The relay sends `{"type":"snapshot","cause":{kind,id},agents,feed,reminders,pending,focus,…}` on connect and after every change. `cause.kind` tells the screen which animation to play: `permission`, `question`, `done`, `ping`, `chat`, `reminder_due` and so on.

The screen sends:

```jsonc
{"type":"decision","id":"<pending id>","decision":"allow|deny|computer"}
{"type":"answer","id":"<pending id>","answers":{"<question text>":"<label>"},"note":"optional"}
{"type":"message","agentId":"<session>","text":"…"}      // queued for that Claude Code session
{"type":"unqueue","agentId":"…","id":"<chat line id>"}  // take back a queued message
{"type":"dismiss","agentId":"…","remove":false}
{"type":"reminder_done","id":"…","done":true}
{"type":"action","name":"focus|snooze_all|ping_when_done"}
{"type":"hello"}                                          // ask for a fresh snapshot
```

`POST /api/client` accepts the same messages over HTTP, for testing with curl.

## How messages reach a session

A message typed on the screen is queued for that session, then delivered by whichever happens first:

1. **Channel** (instant): if the session was started with Deskling as a channel, the channel server long-polls `/channel/poll` and pushes the message into the session, even when it's idle.
2. **Stop hook**: when Claude finishes its current turn, the Stop hook returns `{"decision":"block","reason":…}`, so Claude carries on with your message.
3. **Your next prompt**: the UserPromptSubmit hook attaches it as extra context.
