# Deskling brand

A cream desk, a lilac friend with round ears and peach cheeks, and deep ink for words. Deskling is soft, heavy-rounded and a little bit silly, but it always gets the job done. On the web it looks like the printed case: soft clay you could press. On the device it is drawn flat.

The tokens live in `site/public/brand.css`. The firmware colours are in `docs/design.md` and match these.

## Best for

- The landing page, the installer, the relay's setup page and the board's own page.
- Anything that shows Deskling the character.

It is less suited to dense data views. When a page is mostly settings or logs, keep the cream and ink and drop the clay.

## Personality and voice

- Friendly, plain and short. Use the words a person would say out loud, like "Make one", "Say hi" and "It's late".
- Deskling is the subject: "Deskling nudges you", "it dozes off". It never sounds like a mascot shouting.
- No hype words, no exclamation marks in headlines, and no made-up numbers or quotes.
- Buttons say what happens, such as "Install Deskling", "Copy" or "Read the setup guide". There are no arrows on buttons.

## Colour

| Token | Light | Night | Use |
| --- | --- | --- | --- |
| `--ground` | `#FBF6EF` | `#221D2B` | page background (the device's day and night grounds) |
| `--surface` | `#FFFFFF` | `#2E2739` | cards |
| `--sunk` | `#F3ECE1` | `#1A1621` | wells: code, the try-it tray, switch beds |
| `--ink` | `#2A2433` | `#FBF6EF` | text, primary buttons by day |
| `--muted` | `#6A6274` | `#BDB4C9` | secondary text (AA on ground, surface and tints) |
| `--lilac` | `#CFC4F2` | same | Deskling's body, the relay, switches when on |
| `--lilac-deep` | `#BDB1EA` | same | inner ears |
| `--peach` | `#F4B393` | same | cheeks, and only cheeks |
| `--butter` | `#F5E1A4` | same | reminders and questions |
| `--good` / `--good-bg` | `#2F8A63` / `#D3EEDF` | `#7ED3A8` / `#2B4A3C` | done |
| `--alert` / `--alert-bg` | `#C8553D` / `#FDE8E1` | same / `#4A2A26` | needs you |
| `--tint-lilac`, `--tint-peach`, `--tint-butter` | pale | deep | card grounds |
| `--bold` | ink | lilac | the one bold colour: primary buttons and the closing slab |

Rules:

- Text is always ink, ink-2 or muted. Never put text in lilac, peach or butter, except ink on top of them.
- The device drawing keeps its own colours in both modes, because it is a real object.
- The status colours (red, yellow, green, purple dots) mean the same thing everywhere: needs you, question or reminder, done, and message.

## Typography

- **Nunito** for everything: 900 for headlines, the wordmark and buttons, 800 for labels and nav, and 600 for body text. Commands use the system monospace face.
- Headlines are tight (line-height about 1, tracking -0.03em) and balanced. They are sentences ending in a full stop.
- Body text is 17px (16.5px on phones) at 1.6 line height, with lines under about 70 characters.
- Use sentence case everywhere. Labels are not set in all caps.

## Shape

- Pills (999px) for buttons, chips and switches.
- Cards: 44px corners (32px on phones), with 24–32px for smaller pieces inside them. Nested corners step down.
- The closing slab uses 56px corners.
- There are no hairline-bordered flat boxes. If something is raised, it is clay; if it is a well, it is sunk.

## Depth

Everything raised uses `--shadow-clay`: a white highlight on the top edge, a soft shade inside the bottom, and a long, lilac-tinted shadow. Hover raises it to `--shadow-lift`. When pressed, it sinks to `--shadow-press`. Wells such as code blocks and the try-it tray use `--shadow-sunk`. At night the shadows turn into a faint lilac glow.

The device drawing gets the same light: a highlight from the top-left on the bezel and a shade along the bottom.

## Motion

The motion is springy, like the firmware: things overshoot a little and settle.

| What | How |
| --- | --- |
| Deskling (signature) | Lands on its stand and smiles once at load. Then it blinks, its eyes follow the pointer, and it acts out each try-it moment with sound. |
| Buttons | Press down 2px and squash to 97% × 92% in 0.14s, then spring back over 0.5s. Enter and Space give the same press. |
| Switches | The knob springs across and squashes mid-way. |
| Reveals | Sections that weren't on screen at load pop up 28px from 96% over 0.8s. |
| FAQ | The plus turns into a cross and its well fills with lilac. |

The footer **Motion** switch (`html.calm`, remembered as `site-motion`) defaults from `prefers-reduced-motion`. Calm stills everything, and Deskling still changes face, colour and banner. Nothing is ever hidden waiting for motion.

## Signature

The live Deskling in the hero is the one bold thing on the page. Keep it working, keep it big, and keep its tray of moments right under it. Everything else stays quiet: one bold colour, cream space and clay cards.

## Do and don't

- Do use the real firmware screenshots in an ink frame.
- Do keep peach for the cheeks.
- Don't add gradients as decoration. The only gradients are the clay highlight and the shade on the device.
- Don't use the purple-to-blue "AI" gradient, sparkle icons or robot faces. Deskling is the face.
- Don't let anything scroll sideways at 360px. Wide code scrolls inside its own well.

## Accessibility

- AA contrast in both modes, a skip link, and a visible 3px focus ring in `--focus`.
- Every switch is a real `role="switch"` button. The screens viewer is a keyboard tablist (arrows, Home, End).
- The phone menu traps Tab while it is open. Escape or a tap outside closes it.
- Without JavaScript, every screen shows as a captioned list.
