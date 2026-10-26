# Deskling for Claude Code, Cursor and friends

Deploy the relay first ([docs/setup.md](../docs/setup.md)) and keep its URL and token handy. Your relay's `/setup` page has all of this with copy buttons.

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

That assumes you copied the script to `%USERPROFILE%\deskling\`. If settings `env` doesn't reach hooks on your setup, put the URL and token in `%USERPROFILE%\.deskling\config` instead (see below).

## Cowork and cloud sessions (the Claude app, Claude Code on the web)

Those sessions never see your computer, so they can't read `~/.deskling/config` and the app doesn't ask for plugin settings. Instead, download your own copy of the plugin from your relay's `/setup` page (**Claude app, Cowork and cloud sessions → Download my plugin**). It's the same plugin with a `deskling.conf` inside that holds your relay URL and token. Upload it in the Claude app under **Customize → Plugins → Add → Upload plugin**. Plugins on your Claude account sync to Cowork (on your computer and in the cloud) and to Claude Code, so every session reports from then on. Cloud sessions show up as **Cloud** and Cowork ones without a project folder as **Cowork**.

Keep that zip to yourself: it has your token in it. Having the marketplace plugin installed as well is fine; the relay drops the doubled events. For Claude Code on the web with network access set to **Trusted**, add your relay's domain to the environment's allowed domains.

## Cursor

Cursor's hooks give Deskling status (prompt sent, files edited, finished) and, if you opt in, shell and MCP approvals.

1. Copy [`cursor/deskling-cursor.sh`](cursor/deskling-cursor.sh) to `~/.cursor/hooks/deskling-cursor.sh` and `chmod +x` it.
2. Copy [`cursor/hooks.json`](cursor/hooks.json) to `~/.cursor/hooks.json`. Use [`cursor/hooks.with-approvals.json`](cursor/hooks.with-approvals.json) instead if you want every shell command and MCP call to wait for Allow or Deny on Deskling.
3. Put your URL and token in `~/.deskling/config`.

Approvals mode makes every command wait up to `DESKLING_WAIT` seconds (default 30) for the screen before Cursor asks you itself, so only turn it on when you're away from the keyboard. Cursor has no way to receive messages from outside, so Deskling can't message Cursor chats.

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
