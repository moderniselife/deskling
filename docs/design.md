# Design and motion spec

Canvas resolution is **1280×800 landscape**. The panel is natively 800×1280 portrait, so the firmware rotates it.

## Look

- **Font:** Nunito (700/800/900), plus JetBrains Mono for commands. Both need converting to LVGL font files at the sizes used.
- **Shapes:** flat fills, big rounded corners (24–36 px on cards, full pills on tags), no gradients or blur. Everything maps directly onto LVGL styles.
- **Touch targets:** 44 px minimum.

| Token | Hex | Use |
| --- | --- | --- |
| ground | `#FBF6EF` | day background |
| card | `#FFFFFF` | cards |
| ink | `#2A2433` | text, primary buttons |
| muted | `#6A6274` | secondary text |
| accent | `#CFC4F2` | Deskling card (user-selectable: `#F4C7AE`, `#BFE6D3`, `#F5E1A4`) |
| alert | `#FDE8E1` / `#C8553D` | needs-you background / dot |
| good | `#D3EEDF` / `#2F8A63` | done |
| night | `#221D2B` / `#332C3F` | idle background / cards |

## Screens

1. **Home.** Deskling card with the main agent's status, agent list, live clock and date, reminders (tap to tick off), live feed, and quick actions (focus mode, snooze all, ping me when done).
2. **Needs you.** Full screen when an agent needs permission: the agent, machine, exact command, and Allow once / Deny / Later. Answering swaps to a confirmation state.
3. **Night clock.** Big clock, sleeping Deskling, next reminder and overnight jobs. Comes on by itself after 22:00 when nothing needs you and the screen hasn't been touched for 5 minutes. The backlight drops to 25%. Tap to wake.
4. **Sessions.** A nav rail (Home / Sessions / Night) and filter tabs (All, Needs you, Working, Done). A 3-column grid of session cards, each with a source badge (Claude Code, Cursor, Agent), name, machine, current task, state pill and time since the last update. Cards that need you are tinted and wiggle.
5. **Session chat.** Who it is on the left, with the delivery mode: live channel, next step, or can't message. Chat bubbles on the right (queued, delivered, Claude's replies drawn from their Markdown, with a typing indicator). A long reply ends in a "Show all" pill that opens a full-screen reader on a white page; "Back to the chat" or a swipe back closes it. Steps sit between the bubbles as quiet dotted lines; older ones fold into an "N earlier steps" pill with a chevron that opens them in place under a lilac rule. Quick replies ("Yes please", "Continue", "Run the tests", "Wrap up and summarise"), and a message box with an on-screen keyboard.
6. **Question.** Butter-yellow takeover: a curious Deskling with a "?" bubble, the question, and 2–4 big answer buttons with descriptions. Multi-select and multi-question flows step through with Next/Send. "Answer on my computer instead" hands it back to the terminal.
7. **Quests.** Deskling's score on the lilac card: Deskling (wearing what it has unlocked) inside a purple level ring, "Level 4", an XP bar, a streak chip with a pulsing coral flame, a butter combo chip with its time running down, today's XP, and the week's four challenges with thin bars. On the right, "Today's quests" with a segment per quest that turns green when done, the connected services, and a sync button. Quest cards: a coloured source square (Mojo Desk purple, Linear ink, Jira blue), key, status, estimate, due date, title, and a big round check that needs a second tap ("Tap: done"). Bosses are coral cards with a wiggling "Boss" tag and an HP bar. Tapping a card opens its actions. Below, "More quests" with Add. Focus fills the screen in night colours: a countdown ring with Deskling in headphones, the quest, the boss's HP draining, Done and Stop focusing.
8. **Installing an update.** Over the air or uploaded from the setup page: a busy Deskling on its lilac card, "Getting an upgrade", the old and new version, and a big rounded progress bar with a shine sweeping along it. When it's done Deskling cheers, the bar turns green and it restarts; if it fails there's a gentle message and Back. Nothing else takes the screen meanwhile.

Banners drop in from the top for due reminders and finished tasks.

## Deskling (the character)

A rounded blob with eyes, blush cheeks and a mouth. Built from primitive shapes so it can be drawn with LVGL objects or a small canvas, not bitmaps.

| Mood | When | Motion |
| --- | --- | --- |
| working | an agent is running | bobs (8 px, 3.2 s), blinks every ~4.6 s, eyes glance side to side, cheeks pulse, a "thinking" bubble with three bouncing dots |
| cheer | a reminder is ticked off, a task finishes | arc eyes, open smile, squash-and-stretch jumps, sparkles |
| worried | an agent needs you | trembles, brows bounce, eyes dart, a sweat drop falls on loop, two rings pulse out behind it |
| sleepy | idle / night | breathes (slow squash), drifts, "z"s float up and fade |
| curious | a question, or "your turn" | head tilts side to side, a "?" bubble bobs |

## UI motion

| Element | Motion |
| --- | --- |
| cards on screen enter | rise 18 px + fade, staggered 50–120 ms |
| status dots | ping ring (scale 1 → 2.8, fade) |
| "Needs you" tag | wiggle every 3.5 s |
| busy meter | 5 bars bouncing out of phase |
| clock | colon blinks once a second |
| feed | new item pops in at the top while the list slides down 70 px |
| reminder tick | check pops in with overshoot, row fades to 55% and strikes through, Deskling cheers for ~1.8 s |
| buttons | press scales to 0.94 |
| primary action on needs-you | pulsing glow ring |
| night stars | twinkle out of phase |
| +XP | pill pops in, floats up 90 px and fades, with a burst of squares |
| combo | butter pill pops with overshoot, shakes, burst |
| boss defeated, day cleared, challenge, level up | screen dims, a card pops in, rings grow (their own size) and fade, bursts or confetti falling across the screen; level up shows Deskling cheering in its new outfit |
| quest check | first tap widens to "Tap: done" in butter and pops; second tap turns it green |
| XP bar | fills over 0.7 s |

Keep motion cheap to draw: LVGL renders anything scaled or rotated through an offscreen layer and resamples it, so prefer translate, opacity and size changes (a ping ring grows its own size and border rather than scaling), keep boxes that do get transformed tight, and only set a style in an animation when its value changes (`dk_style`). Screen changes capture the outgoing screen once and animate that picture rather than redrawing both screens every frame. `tools/bench_ui.sh` times every interaction and `tools/frame_diff.py` checks an optimisation didn't change a pixel of the motion.

All of these are translate, scale, rotate or opacity changes, which map onto `lv_anim_t` with ease-in-out or overshoot paths. The ESP32-P4's PPA (2D accelerator) and the 32 MB PSRAM should handle full-screen double buffering. Target 30 fps or better.
