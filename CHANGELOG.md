# Changelog

## v0.3.2 (not released yet)

Being tested as the `v0.3.2-testN` builds. Install the newest one over the air or with the browser installer.

### On the screen

- **Quests.** Your tickets from Mojo Desk, Linear and Jira as a game: today's quests, bosses with HP, a focus timer, XP, levels, streaks, combos, weekly challenges and outfits Deskling unlocks, with celebrations for each. Start, finish, plan, focus or hand a ticket to Claude from the board or `/app`; changes go back to the service. See [docs/quests.md](docs/quests.md).

- **Whole conversations.** A session's chat shows your messages, Claude's replies as it writes them, its thinking, each step it takes, its helpers, pictures and the passages you quoted. It scrolls, and keeps your place when new lines arrive.
- **Formatted replies.** Replies show their Markdown: headings, bold, `code`, lists, quotes, code blocks, tables and links.
- **Show all.** A long reply has a Show all button that opens the whole of it in a reader.
- **Earlier steps open.** "12 earlier steps" opens in place to list them, and folds away again, like the steps in the Claude app.
- **Wait for my reply.** When Claude finishes, it waits for your next message from Deskling instead of stopping, so you can carry on the conversation without the computer. While it waits, the session shows "Your turn · Waiting for your reply" with the minutes left, a card in the chat and a notification; "Stop waiting" lets it finish. Tapped after Claude has already stopped, it says it starts next time. (It was called "Keep listening".)
- **New session.** Start a session from the screen: in the cloud (with a routine), or on one of your computers running the Deskling runner, in one of its projects. Pick the agent too: Claude Code, OpenCode with any of your models (local ones included), or Gemini CLI, Cursor, Codex, Copilot, Goose and others the runner finds.
- **OpenCode.** OpenCode sessions show up like Claude Code ones, with an "Oc" badge, and you can approve, answer and message them.
- Tapping a notification, or a session in the notification list, opens its chat.
- Notification banners, a scrollable notification list you can clear, and gestures: swipe from the left edge to go back, up from the bottom for home.
- Boot animation, touch calibration (five taps, redo it from Settings or the board's page), and sounds with volume and quiet hours.
- Faster drawing, and screens only redraw when something changed.
- Snapshots are smaller: only the chat that's open is sent in full.
- Right way up in the Deskling case out of the box (USB ports on the left). "Right way up in the case" in Settings, or on the board's page, turns it the other way; touch follows, and your touch calibration carries over.

### Deskling in your browser

- `/app` on your relay is Deskling in a browser: sessions, whole conversations, approvals, questions, messages, reminders, notifications and New session. It works on a phone, can be added to the home screen, and can push notifications even when it's closed.
- Replies in `/app` are formatted too, long ones have Show all, and earlier steps open in place.

### The board

- Smoother chats: only lines that changed are redrawn, updates arriving faster than the screen can draw are merged (the newest wins), and nothing redraws under your finger while you scroll.
- The relay connection no longer stalls while the screen is busy: "Show all", earlier steps and pictures arrive straight away, and a connection that gets stuck reconnecting, or goes quiet, is started again by itself.
- Notifications and other one-line text are cut with "…" instead of spilling over the next row.
- The board keeps its small internal memory for Wi-Fi and the network: the screen's objects move to PSRAM once internal memory gets low, so a long chat no longer stops deskling.local answering or the relay connecting. deskling.local also answers while a picture is being decoded.
- The board's page shows memory, how long updates take to draw, and how often the relay connection was restarted.

- The board's page (deskling.local) keeps answering: more connections for the web server, IPv4-only answers for deskling.local (Chrome on a Mac timed out), and a page that stops retrying with an old code after a restart.
- The board remembers its last crash and shows it, with the reason it last restarted, on its page.
- Pictures in chats no longer crash the board.

### Your computer and the relay

- The Claude Code plugin streams the conversation, sends your quoted passages, and handles messages you send while Claude works.
- Permission prompts answered on the computer come off the screen.
- The runner (`hooks/runner`) starts sessions from the screen: Claude Code, OpenCode, and any agent that speaks the Agent Client Protocol.
- The Deskling plugin for OpenCode, the Deskling agents for OpenCode, and screen tools any OpenCode agent can use (notify, remind, ask).
- A bridge API on the relay (`/agent/update`, `/agent/ask`, `/agent/poll`) so any agent can put a full session on the screen.
- Ollama and LM Studio models on your computer are offered to OpenCode with no setup.
- The relay can't be confused by ids like `__proto__`, and is fuzz-tested; the screen is chaos-tested with random snapshots under AddressSanitizer.
- One command runs every test (`scripts/test-all.sh`), and a CI workflow runs them on pull requests.

## v0.3.1 (26 October 2026)

- Over-the-air updates from deskling.mojolayers.com, the browser installer, Wi-Fi setup and the settings page.
- Sounds on boards with a speaker.
