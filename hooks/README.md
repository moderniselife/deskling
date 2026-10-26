# Deskling for Claude Code, OpenCode, Cursor and friends

Deploy the relay first ([docs/setup.md](../docs/setup.md)) and keep its URL and token handy. Your relay's `/setup` page has all of this with copy buttons.

## What works with what

| Agent | How | On the screen | From the screen |
| --- | --- | --- | --- |
| Claude Code (terminal, desktop app, Cowork, cloud) | [plugin](#claude-code-macos-windows-linux-and-the-code-tab-in-the-desktop-app) | everything: replies, thinking, steps, helpers, pictures | approve, answer, message, start new sessions |
| OpenCode, any model (local ones too) | [plugin](#opencode-any-model-local-ones-too) | everything | approve, answer, message, start new sessions with a model |
| Gemini CLI, Cursor, Codex, Copilot, Goose, Qwen Code, Kimi, Kiro | started by the [runner](#starting-sessions-from-deskling) (ACP) | replies, thinking, steps | approve, message, start new sessions |
| Cursor in the editor | [hooks](#cursor) | status and edits | approve shell and MCP calls |
| Anything else | [bridge API](#other-agents-the-bridge-api) or the [`deskling` CLI](#everything-else-the-deskling-cli) | what you send | what you build |

## Claude Code (macOS, Windows, Linux, and the Code tab in the desktop app)

Inside Claude Code:

```
/plugin marketplace add moderniselife/deskling
/plugin install deskling@deskling
/plugin configure deskling@deskling
```

`/plugin configure` asks for these (or pipe JSON to `claude plugin configure deskling@deskling --values-stdin` from a terminal):

| Option | |
| --- | --- |
| Relay URL | `https://deskling-relay.<you>.workers.dev` |
| Relay token | your `DESKLING_TOKEN` |
| This computer's name | optional, defaults to the hostname |
| Seconds to wait for an answer on the screen | default 30. `0` turns screen approvals and questions off. |

What you get:

- **Status:** every session on every machine shows up on Deskling as working, waiting, needs you, or done.
- **Permissions:** Allow or Deny on the screen. If you don't answer in time, the normal prompt appears in your terminal. While a request is waiting for the screen, the terminal prompt is held back, so lower the wait if you're usually at the keyboard.
- **Questions** (experimental): when Claude asks a multiple-choice question, tap an answer on Deskling. There's no documented hook API for answering, so the hook cancels the question dialog and hands Claude your answer as text. If you don't answer, the dialog appears as normal.
- **Messages to a session:** type or quick-reply on Deskling. A busy session gets the message when it finishes its current step. An idle session gets it with your next prompt, or straight away with channels (below).
- **Tools for Claude:** `deskling_remind` ("remind me at 3pm to…"), `deskling_notify`, `deskling_reply`. These need Node 18+ on the machine. Without Node, everything else still works.

### Keep listening (any session, including Cowork and cloud)

On a session's screen on Deskling, tap **Keep listening**. When Claude next finishes, it doesn't stop: it waits (for up to 30 minutes) for a message from Deskling, and that message becomes its next prompt. Tap it again to stop listening. While it's listening, the session counts as still running on your computer, so turn it off before you go back to typing there.

### Talking to idle sessions (channels, research preview)

Start Claude with Deskling as a channel:

```bash
claude --channels plugin:deskling@deskling
```

Messages from the screen now arrive immediately, even while the session is idle, and Claude answers on the screen. Channels need a claude.ai login or a Console API key. On Team and Enterprise plans, an admin has to enable them. If Deskling doesn't notice channel mode, set `DESKLING_CHANNEL=1` in the environment you start Claude from.

### Windows

The hooks are bash scripts. Claude Code runs them through Git Bash, which comes with [Git for Windows](https://git-scm.com/download/win). Without Git Bash, skip the plugin and add [`scripts/deskling-hook.ps1`](scripts/deskling-hook.ps1) to `%USERPROFILE%\.claude\settings.json` yourself:

```json
{
  "env": { "DESKLING_URL": "https://deskling-relay.<you>.workers.dev", "DESKLING_TOKEN": "<token>" },
  "hooks": {
    "SessionStart":      [{ "hooks": [{ "type": "command", "shell": "powershell", "command": "& \"$HOME\\deskling\\deskling-hook.ps1\"", "async": true }] }],
    "UserPromptSubmit":  [{ "hooks": [{ "type": "command", "shell": "powershell", "command": "& \"$HOME\\deskling\\deskling-hook.ps1\" sync", "timeout": 8 }] }],
    "Notification":      [{ "hooks": [{ "type": "command", "shell": "powershell", "command": "& \"$HOME\\deskling\\deskling-hook.ps1\"", "async": true }] }],
    "Stop":              [{ "hooks": [{ "type": "command", "shell": "powershell", "command": "& \"$HOME\\deskling\\deskling-hook.ps1\" stop", "timeout": 7200 }] }],
    "SessionEnd":        [{ "hooks": [{ "type": "command", "shell": "powershell", "command": "& \"$HOME\\deskling\\deskling-hook.ps1\"", "async": true }] }],
    "PreToolUse":        [{ "matcher": "AskUserQuestion", "hooks": [{ "type": "command", "shell": "powershell", "command": "& \"$HOME\\deskling\\deskling-hook.ps1\" wait", "timeout": 130 }] },
                          { "hooks": [{ "type": "command", "shell": "powershell", "command": "& \"$HOME\\deskling\\deskling-hook.ps1\" live", "async": true, "timeout": 5 }] }],
    "PostToolUse":       [{ "matcher": "SendUserMessage", "hooks": [{ "type": "command", "shell": "powershell", "command": "& \"$HOME\\deskling\\deskling-hook.ps1\"", "async": true, "timeout": 5 }] }],
    "SubagentStart":     [{ "hooks": [{ "type": "command", "shell": "powershell", "command": "& \"$HOME\\deskling\\deskling-hook.ps1\"", "async": true, "timeout": 5 }] }],
    "SubagentStop":      [{ "hooks": [{ "type": "command", "shell": "powershell", "command": "& \"$HOME\\deskling\\deskling-hook.ps1\"", "async": true, "timeout": 5 }] }],
    "PermissionRequest": [{ "hooks": [{ "type": "command", "shell": "powershell", "command": "& \"$HOME\\deskling\\deskling-hook.ps1\" wait", "timeout": 130 }] }]
  }
}
```

That assumes you copied [`scripts/deskling-hook.ps1`](scripts/deskling-hook.ps1) and [`scripts/deskling-stream.ps1`](scripts/deskling-stream.ps1) (it streams the conversation) to `%USERPROFILE%\deskling\`. If settings `env` doesn't reach hooks on your setup, put the URL and token in `%USERPROFILE%\.deskling\config` instead (see below).

## Cowork and cloud sessions (the Claude app, Claude Code on the web)

Those sessions never see your computer, so they can't read `~/.deskling/config` and the app doesn't ask for plugin settings. Instead, download your own copy of the plugin from your relay's `/setup` page (**Claude app, Cowork and cloud sessions → Download my plugin**). It's the same plugin with a `deskling.conf` inside that holds your relay URL and token. Upload it in the Claude app under **Customize → Plugins → Add → Upload plugin**. Plugins on your Claude account sync to Cowork (on your computer and in the cloud) and to Claude Code, so every session reports from then on. Cloud sessions show up as **Cloud** and Cowork ones without a project folder as **Cowork**.

Keep that zip to yourself: it has your token in it. Having the marketplace plugin installed as well is fine; the relay drops the doubled events. For Claude Code on the web with network access set to **Trusted**, add your relay's domain to the environment's allowed domains.

### Cloud repos

Sessions started by a routine (New session → Cloud on Deskling) or on Claude Code on the web run in a fresh cloud container. If your personal plugin doesn't load there, give the repo the hooks instead:

1. In the cloud environment's settings, add the environment variables `DESKLING_URL` and `DESKLING_TOKEN` (your relay), and if its network access is **Trusted**, allow your relay's domain and `raw.githubusercontent.com`.
2. Commit this as `.claude/settings.json` in the repo (merge it with any hooks you already have). Each container downloads the hook script once:

```json
{
  "hooks": {
    "SessionStart": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "bash -c 'f=/tmp/deskling-hook.sh; [ -s $f ] || curl -fsSL https://raw.githubusercontent.com/moderniselife/deskling/main/hooks/scripts/deskling-hook.sh -o $f; bash $f'",
            "async": true,
            "timeout": 10
          }
        ]
      }
    ],
    "UserPromptSubmit": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "bash -c 'f=/tmp/deskling-hook.sh; [ -s $f ] || curl -fsSL https://raw.githubusercontent.com/moderniselife/deskling/main/hooks/scripts/deskling-hook.sh -o $f; bash $f sync'",
            "timeout": 8
          }
        ]
      }
    ],
    "Notification": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "bash -c 'f=/tmp/deskling-hook.sh; [ -s $f ] || curl -fsSL https://raw.githubusercontent.com/moderniselife/deskling/main/hooks/scripts/deskling-hook.sh -o $f; bash $f'",
            "async": true,
            "timeout": 10
          }
        ]
      }
    ],
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "bash -c 'f=/tmp/deskling-hook.sh; [ -s $f ] || curl -fsSL https://raw.githubusercontent.com/moderniselife/deskling/main/hooks/scripts/deskling-hook.sh -o $f; bash $f stop'",
            "timeout": 7200
          }
        ]
      }
    ],
    "SessionEnd": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "bash -c 'f=/tmp/deskling-hook.sh; [ -s $f ] || curl -fsSL https://raw.githubusercontent.com/moderniselife/deskling/main/hooks/scripts/deskling-hook.sh -o $f; bash $f'",
            "async": true,
            "timeout": 5
          }
        ]
      }
    ],
    "PreToolUse": [
      {
        "matcher": "AskUserQuestion",
        "hooks": [
          {
            "type": "command",
            "command": "bash -c 'f=/tmp/deskling-hook.sh; [ -s $f ] || curl -fsSL https://raw.githubusercontent.com/moderniselife/deskling/main/hooks/scripts/deskling-hook.sh -o $f; bash $f wait'",
            "timeout": 130
          }
        ]
      },
      {
        "hooks": [
          {
            "type": "command",
            "command": "bash -c 'f=/tmp/deskling-hook.sh; [ -s $f ] || curl -fsSL https://raw.githubusercontent.com/moderniselife/deskling/main/hooks/scripts/deskling-hook.sh -o $f; bash $f live'",
            "async": true,
            "timeout": 5
          }
        ]
      }
    ],
    "PermissionRequest": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "bash -c 'f=/tmp/deskling-hook.sh; [ -s $f ] || curl -fsSL https://raw.githubusercontent.com/moderniselife/deskling/main/hooks/scripts/deskling-hook.sh -o $f; bash $f wait'",
            "timeout": 130
          }
        ]
      }
    ],
    "SubagentStart": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "bash -c 'f=/tmp/deskling-hook.sh; [ -s $f ] || curl -fsSL https://raw.githubusercontent.com/moderniselife/deskling/main/hooks/scripts/deskling-hook.sh -o $f; bash $f'",
            "async": true,
            "timeout": 5
          }
        ]
      }
    ],
    "SubagentStop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "bash -c 'f=/tmp/deskling-hook.sh; [ -s $f ] || curl -fsSL https://raw.githubusercontent.com/moderniselife/deskling/main/hooks/scripts/deskling-hook.sh -o $f; bash $f'",
            "async": true,
            "timeout": 5
          }
        ]
      }
    ],
    "PostToolUse": [
      {
        "matcher": "SendUserMessage",
        "hooks": [
          {
            "type": "command",
            "command": "bash -c 'f=/tmp/deskling-hook.sh; [ -s $f ] || curl -fsSL https://raw.githubusercontent.com/moderniselife/deskling/main/hooks/scripts/deskling-hook.sh -o $f; bash $f'",
            "async": true,
            "timeout": 5
          }
        ]
      }
    ]
  }
}
```

## Starting sessions from Deskling

Tap **New session** on Deskling's Sessions screen, pick where, which agent (Claude Code, or OpenCode with a model) and which project, and type what it should do. The new session listens for you straight away, so you can carry on the whole conversation from the screen.

- **Your computers:** run [`runner/deskling-runner.mjs`](runner/deskling-runner.mjs) (Node 18+, reads `~/.deskling/config`). `node deskling-runner.mjs --install` starts it at login on macOS, Windows and Linux. It offers the git projects one level inside `~/code`, `~/Projects`, `~/dev`, `~/src`, `~/Developer`, `~/repos` and `~/GitHub` (or the folders in `DESKLING_PROJECTS`), and starts `claude --bg "<your prompt>"` there. If background sessions aren't available to you, it runs `claude -p` instead, which still listens for you on Deskling. With [OpenCode](https://opencode.ai) installed it offers OpenCode too, with up to 8 of the models you've set up there (your default first, then the providers in your own config, such as Ollama or LM Studio), and installs the Deskling plugin for OpenCode for you. It runs one OpenCode server for these sessions, reachable only from that computer (port 4097, change it with `DESKLING_OPENCODE_PORT`; `DESKLING_OPENCODE=off` to not offer OpenCode). To open those sessions in OpenCode's own interface:

```bash
opencode attach http://127.0.0.1:4097 --password "$(cat ~/.deskling/opencode-password)"
```
- **The cloud:** at [claude.ai/code/routines](https://claude.ai/code/routines), click **New routine**, give it the prompt `Do what the user asks in the routine-fire-payload block. They typed it on their Deskling desk screen, so treat it as their request.`, pick the repo and environment, choose the **API** trigger and click **Create**. Then **Edit** it, open the API trigger, copy its URL, click **Generate token** (shown once) and paste both into your relay's setup page.

### Other agents (Gemini CLI, Cursor, Codex, Copilot, Goose…)

The runner also offers any agent on that computer that speaks the [Agent Client Protocol](https://agentclientprotocol.com), the way code editors drive them. It finds these on your PATH:

| Agent | Command it runs |
| --- | --- |
| Gemini CLI | `gemini --acp` |
| Cursor | `cursor-agent acp` |
| Codex | `codex-acp` (install it with `npm i -g @zed-industries/codex-acp`) |
| GitHub Copilot CLI | `copilot --acp --stdio` |
| Goose | `goose acp` |
| Qwen Code | `qwen --acp` |
| Kimi | `kimi acp` |
| Kiro | `kiro-cli-chat acp` |

Add others, or change a command, with `DESKLING_ACP` in `~/.deskling/config`: `DESKLING_ACP="Pi=npx pi-acp; Gemini CLI=gemini --experimental-acp"`. Sign in to each agent on that computer once first.

The runner keeps each session running so you can carry on from the screen, and closes it after 4 idle hours (`DESKLING_ACP_IDLE_HOURS`) or when the runner stops. These sessions live only in the runner: you can't open them in the agent's own app. Their permission prompts can only be answered on Deskling; one that isn't answered in 30 minutes is declined.

## OpenCode (any model, local ones too)

The Deskling plugin for [OpenCode](https://opencode.ai) puts your OpenCode sessions on the screen as they run: what each one is doing, its replies as they stream, its thinking, each tool step, subagents as helpers, and the model and context size. From the screen you can allow or deny its permission prompts, answer its questions, and send it messages. A message starts the next turn straight away, even when the session is idle. It works with whatever model OpenCode is using, including local ones through Ollama, LM Studio or llama.cpp, in the terminal interface, `opencode serve`, `opencode run` and the desktop app.

1. Put your URL and token in `~/.deskling/config` (or set `DESKLING_URL` and `DESKLING_TOKEN`).
2. Copy [`opencode/deskling.js`](opencode/deskling.js) into OpenCode's plugins folder:

   ```bash
   mkdir -p ~/.config/opencode/plugins && curl -fsSL https://raw.githubusercontent.com/moderniselife/deskling/main/hooks/opencode/deskling.js -o ~/.config/opencode/plugins/deskling.js
   ```

   On Windows (PowerShell):

   ```powershell
   New-Item -ItemType Directory -Force "$HOME\.config\opencode\plugins" | Out-Null; Invoke-WebRequest https://raw.githubusercontent.com/moderniselife/deskling/main/hooks/opencode/deskling.js -OutFile "$HOME\.config\opencode\plugins\deskling.js"
   ```

3. Restart OpenCode.

The [runner](#starting-sessions-from-deskling) installs the plugin for you when it finds OpenCode. For just one project, put the file in that project's `.opencode/plugins/` instead.

### The Deskling agents

[`opencode/agents`](opencode/agents) is a small team of OpenCode agents made for working while you're away from the keyboard. Copy them to `~/.config/opencode/agents/` (the runner does this for you, and leaves them alone once you've edited them).

- **deskling** (pick it with Tab in OpenCode): plans first, hands pieces to its helpers, checks its own work, and uses the screen: it notifies you when something you'd want to know happens, asks you there when it needs a decision, and can set reminders. Sessions you start from Deskling use it.
- **deskling-planner**: reads the code and writes a short, concrete plan. Can't change files.
- **deskling-tester**: finds and runs the tests, type checks and linters, and fixes what the change broke. Can't push.
- **deskling-reviewer**: reviews the change for bugs, missed cases and security. Can't change files.

They work with any model; with a small local model, give the helpers a bigger one with `model:` in their files if you have one. The plugin gives every OpenCode agent the screen tools: `deskling_notify`, `deskling_remind` and `deskling_ask`.

A permission prompt waits on Deskling for up to `DESKLING_WAIT` seconds (default 110) while OpenCode shows its own prompt too. Answer either one: answering on the computer takes it off the screen.

Tested with OpenCode 1.19 (`opencode-ai` on npm). Its test (`node hooks/opencode/test/opencode.test.mjs`) runs a real OpenCode against a stand-in model, so it needs no API key.

## Cursor

Cursor's hooks give Deskling status (prompt sent, files edited, finished) and, if you opt in, shell and MCP approvals.

1. Copy [`cursor/deskling-cursor.sh`](cursor/deskling-cursor.sh) to `~/.cursor/hooks/deskling-cursor.sh` and `chmod +x` it.
2. Copy [`cursor/hooks.json`](cursor/hooks.json) to `~/.cursor/hooks.json`. Use [`cursor/hooks.with-approvals.json`](cursor/hooks.with-approvals.json) instead if you want every shell command and MCP call to wait for Allow or Deny on Deskling.
3. Put your URL and token in `~/.deskling/config`.

Approvals mode makes every command wait up to `DESKLING_WAIT` seconds (default 30) for the screen before Cursor asks you itself, so only turn it on when you're away from the keyboard. Cursor has no way to receive messages from outside, so Deskling can't message Cursor chats.

## Other agents: the bridge API

Anything that can make HTTP requests can put a full session on Deskling, the same way the OpenCode plugin does. All requests take `Authorization: Bearer <token>`.

| Request | What it does |
| --- | --- |
| `POST /agent/update` | Report a session: `{id, source, name, host, os, state, task, model, context, helpers, lines}`. `state` is `idle`, `working`, `waiting`, `needs_you` or `done`. `lines` are chat lines `{key, from, text}` with `from` one of `you`, `agent`, `tool` or `thought`; sending a `key` again updates that line, so streamed text grows in place. `{id, end: true}` removes the session. |
| `POST /agent/ask` | Ask on the screen and wait for the answer: `{id, key, kind: "permission" or "question", tool, summary, detail, questions, wait}`. Returns `{decision: "allow" or "deny"}`, `{answers: {question: label}}` or `{timeout: true}`. |
| `POST /agent/resolve` | `{id, key}`: the request was answered somewhere else, so take it off the screen. |
| `GET /agent/poll?ids=a,b&wait=25` | Wait for messages typed on the screen for those sessions: `{messages: [{id, text}]}`. While a bridge polls, its sessions accept messages from the screen. |

`source` is one of `opencode`, `codex`, `gemini`, `cursor` or `custom`, and decides the badge on the screen.

## Everything else: the `deskling` CLI

[`scripts/deskling`](scripts/deskling) is plain bash plus curl. Use it from cron, CI, cloud scheduled tasks, Codex or Gemini CLI hooks, or anything else:

```bash
deskling notify "Daily digest is ready" "12 new emails" --level good
deskling remind "Stand up and stretch" --in 45m
deskling remind "Review the PR" --at 15:00 --from "Claude"
deskling status nightly working "Reading inboxes" --name "Nightly digest" --host cloud
deskling status nightly done "Digest sent"
deskling done-status nightly
```

## Config file

Every script reads `~/.deskling/config` (`%USERPROFILE%\.deskling\config` on Windows) if the settings aren't in the environment:

```bash
DESKLING_URL="https://deskling-relay.<you>.workers.dev"
DESKLING_TOKEN="<token>"
DESKLING_NAME="studio-mac"   # optional
DESKLING_WAIT=30             # optional
```
