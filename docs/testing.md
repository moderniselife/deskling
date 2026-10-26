# Testing Deskling

One command runs every automated check:

```bash
scripts/test-all.sh              # everything except the firmware builds (about 3 minutes)
scripts/test-all.sh --quick      # skips the AddressSanitizer simulator run
scripts/test-all.sh --firmware   # also builds the firmware for all four boards (needs ESP-IDF in the shell)
```

It needs Node 22+, a C compiler and cmake (for the simulator), and Playwright with Chromium (for the web pages). The OpenCode test needs `opencode` on the PATH (`npm i -g opencode-ai`) and skips itself otherwise. The CI workflow (`.github/workflows/ci.yml`) runs the same script on pull requests, and on demand from the Actions tab.

Run it before pushing to `main`: `main` deploys the relay and the website.

## What it checks

| Step | What it is | What it catches |
| --- | --- | --- |
| relay | A local relay (`wrangler dev`), then `relay/test/smoke.mjs`, `tests/web/app.test.mjs`, `relay/test/push.test.mjs` and `relay/test/fuzz.mjs` against it | Every hook event, permissions and questions, messages, Keep listening, transcripts and pictures, runners and new sessions, the bridge API, screen views; Deskling in the browser (`/app`) on a desktop and a phone; push notifications decrypted and their signature checked the way a browser would; 300 broken or odd requests with no server error, nothing hanging and a sound snapshot afterwards |
| hooks | `hooks/test/hook.test.mjs`: the Claude Code hook script against a fake relay | Transcript streaming (tool output never sent), quotes, Keep listening and letting go when you type, staying quiet under the runner |
| runner | `hooks/runner/test/runner.test.mjs` | Finding projects once each, starting Claude Code for a job, offering the agents that are installed |
| opencode | `hooks/opencode/test/opencode.test.mjs`: a real OpenCode with a stand-in model (no API key) and a fake relay | The OpenCode plugin (replies, thinking, steps, model, context, permissions from the screen and from the computer, questions, helpers, pictures, screen messages), the Deskling agents' screen tools, and the runner starting OpenCode both directly and over ACP |
| simulator | `firmware/tools/run_sim.sh`: the real UI code on your computer, driven by taps, with a PNG of every screen | Every screen and flow, then 150 rounds of random snapshots and taps |
| simulator-asan | The same, built with AddressSanitizer | Memory errors and crashes (`DK_CHAOS=2500` for a longer random run) |
| pictures | The board's PNG decoder under valgrind on a simulator screenshot | Chat pictures |
| web | `tests/web/site.test.mjs` and `portal.test.mjs` | The landing page (phone menu, screens viewer, motion switch, no-JavaScript fallback, no sideways scrolling) and the board's own page (its code, restarts, not hammering the board) |
| firmware | `idf.py build` for p4r1/p4r3 × lcd1/lcd2 | Anything that only the ESP32-P4 compiler catches |

## By hand, on the board

The things the tests can't reach, for each `-testN` build:

- Install over the air from Settings → Firmware, and with the full image from the browser installer.
- `deskling.local` in Safari and Chrome, and that it keeps answering for an hour.
- A real Claude Code session: its conversation, a permission answered on the screen, a question, Keep listening.
- A chat with a picture (PNG and JPEG).
- Starting a session from the screen on a computer with the runner.
