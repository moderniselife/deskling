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
3. **Night clock.** Big clock, sleeping Deskling, next reminder and overnight jobs.

## Deskling (the character)

A rounded blob with eyes, blush cheeks and a mouth. Built from primitive shapes so it can be drawn with LVGL objects or a small canvas, not bitmaps.

| Mood | When | Motion |
| --- | --- | --- |
| working | an agent is running | bobs (8 px, 3.2 s), blinks every ~4.6 s, eyes glance side to side, cheeks pulse, a "thinking" bubble with three bouncing dots |
| cheer | a reminder is ticked off, a task finishes | arc eyes, open smile, squash-and-stretch jumps, sparkles |
| worried | an agent needs you | trembles, brows bounce, eyes dart, a sweat drop falls on loop, two rings pulse out behind it |
| sleepy | idle / night | breathes (slow squash), drifts, "z"s float up and fade |

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

All of these are translate, scale, rotate or opacity changes, which map onto `lv_anim_t` with ease-in-out or overshoot paths. The ESP32-P4's PPA (2D accelerator) and the 32 MB PSRAM should handle full-screen double buffering. Target 30 fps or better.
