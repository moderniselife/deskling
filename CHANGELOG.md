# Changelog

## v0.3.2 (not released yet)

Being tested as the `v0.3.2-testN` builds. Install the newest one over the air or with the browser installer.

### On the screen

- **Whole conversations.** A session's chat shows your messages, Claude's replies as it writes them, its thinking, each step it takes, its helpers, pictures and the passages you quoted. It scrolls, and keeps your place when new lines arrive.
- **Keep listening.** A finished session waits for your next message from Deskling, so you can carry on the conversation without the computer.
- **New session.** Start a session from the screen: in the cloud (with a routine), or on one of your computers running the Deskling runner, in one of its projects. Pick the agent too: Claude Code, OpenCode with any of your models (local ones included), or Gemini CLI, Cursor, Codex, Copilot, Goose and others the runner finds.
- **OpenCode.** OpenCode sessions show up like Claude Code ones, with an "Oc" badge, and you can approve, answer and message them.
- Tapping a notification, or a session in the notification list, opens its chat.
- Notification banners, a scrollable notification list you can clear, and gestures: swipe from the left edge to go back, up from the bottom for home.
- Boot animation, touch calibration (five taps, redo it from Settings or the board's page), and sounds with volume and quiet hours.
- Faster drawing, and screens only redraw when something changed.
- Snapshots are smaller: only the chat that's open is sent in full.

### The board

- The board's page (deskling.local) keeps answering: more connections for the web server, IPv4-only answers for deskling.local (Chrome on a Mac timed out), and a page that stops retrying with an old code after a restart.
- The board remembers its last crash and shows it, with the reason it last restarted, on its page.
- Pictures in chats no longer crash the board.

### Your computer and the relay

- The Claude Code plugin streams the conversation, sends your quoted passages, and handles messages you send while Claude works.
- Permission prompts answered on the computer come off the screen.
- The runner (`hooks/runner`) starts sessions from the screen: Claude Code, OpenCode, and any agent that speaks the Agent Client Protocol.
- The Deskling plugin for OpenCode, the Deskling agents for OpenCode, and screen tools any OpenCode agent can use (notify, remind, ask).
- A bridge API on the relay (`/agent/update`, `/agent/ask`, `/agent/poll`) so any agent can put a full session on the screen.
- One command runs every test (`scripts/test-all.sh`), and a CI workflow runs them on pull requests.

## v0.3.1 (26 October 2026)

- Over-the-air updates from deskling.mojolayers.com, the browser installer, Wi-Fi setup and the settings page.
- Sounds on boards with a speaker.
