# Deskling

A cute, animated desk companion for your AI agents.

Deskling is a 10.1" touchscreen that sits on your desk in landscape mode and shows you what your agents are doing: Claude Code sessions on your Mac or Windows machine, scheduled tasks in the cloud, and anything else that can send an HTTP request. A little animated character reacts to what's going on. It bobs while Claude works, panics when an agent needs your permission, cheers when you tick off a reminder, and dozes off at night.

## How it fits together

```
 Claude Code (Mac / Windows)          Scheduled tasks, scripts, other agents
   └─ hooks (Notification, Stop)        └─ curl / fetch
                 \                          /
                  ▼                        ▼
            ┌──────────────────────────────────┐
            │  relay/  (Cloudflare Worker)      │
            │  POST /event  ·  WebSocket /ws    │
            └──────────────────────────────────┘
                             │  WebSocket over Wi-Fi
                             ▼
            ┌──────────────────────────────────┐
            │  firmware/  (ESP32-P4 + LVGL)     │
            │  dashboard · alerts · reminders   │
            └──────────────────────────────────┘
                             │  taps (allow / deny / done / snooze)
                             └──────────► back up the WebSocket to the relay
```

The screen is a microcontroller, not a computer, so it doesn't run agents itself. It's a display and remote for agents running elsewhere. The relay sits in the middle so local and cloud agents can both reach it without opening ports on your network.

## Repo layout

| Folder | What lives there |
| --- | --- |
| `firmware/` | ESP-IDF project for the screen: LVGL UI, Wi-Fi via the ESP32-C6, WebSocket client |
| `relay/` | Cloudflare Worker that accepts events and pushes them to the screen |
| `hooks/` | Claude Code hook scripts for macOS and Windows |
| `case/` | 3D-printable housing (parametric CAD + exported STLs) |
| `docs/` | Design spec, motion spec, hardware notes |

## Hardware

- **Board:** Guition JC8012P4A1C_I_W_Y. ESP32-P4 with 32 MB PSRAM, a 10.1" 800×1280 IPS capacitive touch panel over MIPI-DSI, and an ESP32-C6 for Wi-Fi 6 / BLE 5 over ESP-Hosted.
- **Power:** a stable 5 V / 1 A+ supply. Not a laptop USB port.
- Board notes and gotchas live in [`docs/hardware.md`](docs/hardware.md).

## Status

- [x] UI design and motion concept (three screens: home, needs-you, night clock)
- [ ] Relay worker
- [ ] Claude Code hooks (macOS + Windows)
- [ ] Firmware bring-up: display, touch, Wi-Fi
- [ ] Firmware UI port to LVGL
- [ ] Housing (needs board measurements)
