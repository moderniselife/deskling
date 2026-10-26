# deskling.mojolayers.com

A Cloudflare Worker with static assets, deployed by Cloudflare Workers Builds on every push to `main` (root directory `site/`).

| Path | What |
| --- | --- |
| `/` | Landing page (`public/index.html`). The hero Deskling plays the firmware's real sounds from `public/sounds/`. |
| `/install` | Browser installer (Chrome/Edge + USB), built from the relay's `/flash` page. |
| `/firmware/*` | Update server: `manifest.json` and versioned `.bin` builds, served from the `firmware-dist` branch through Cloudflare's cache (`src/worker.ts`). Screens check `/firmware/manifest.json`. |

```bash
cd site && npm install
npm run dev        # http://127.0.0.1:8787
npm run typecheck
```

Regenerate the sounds after changing `firmware/main/app/sound_synth.c`:

```bash
cd firmware && cc -O2 -Imain/app tools/render_sounds.c main/app/sound_synth.c -lm -o /tmp/render_sounds && /tmp/render_sounds ../site/public/sounds
```
