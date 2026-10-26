# Setting up Deskling

About 20 minutes, start to finish. You plug in a cable once, to install the firmware. Everything after that happens over Wi-Fi.

## What you're setting up, and why

```
your Mac / PC (Claude Code, Cursor)  ──►  your relay (Cloudflare Worker)  ◄──  Deskling on your desk
cloud agents, cron jobs, scripts     ──►        free, always online
```

**The relay** is a tiny server that you host yourself, on your own free Cloudflare account. Everything meets there. Your agents post "Claude needs permission", "finished" and so on to it. The screen keeps a live connection to it and sends your taps back. It's needed because your agents run on several machines (and in the cloud), and the screen sits behind your home router. Neither side can reach the other directly, but both can reach the relay.

- **Cost:** for one person, it should fit in Cloudflare's free Workers plan. You don't need a credit card or a domain.
- **Privacy:** it's your account and your token. Nothing goes through a server run by anyone else.
- **What it stores:** session names, short task summaries, reminders and the last few chat lines. It doesn't store your code, and it keeps no logs beyond what Cloudflare shows you.

## 1. Deploy your relay (5 min)

**Option A: one click.** This needs a free [Cloudflare](https://dash.cloudflare.com/sign-up) account and a GitHub account.

[![Deploy to Cloudflare](https://deploy.workers.cloudflare.com/button)](https://deploy.workers.cloudflare.com/?url=https://github.com/moderniselife/deskling/tree/main/relay)

1. Click the button and sign in to Cloudflare.
2. When it asks for **DESKLING_TOKEN**, make up a long random password, e.g. run `openssl rand -hex 24`, or mash the keyboard for 30+ characters. Save it in your password manager. You'll paste it twice more.
3. Leave `FIRMWARE_URL` as it is, and click **Deploy**.
4. When it finishes, you get a URL like `https://deskling-relay.<you>.workers.dev`. That's your **relay URL**.

Cloudflare copies the relay code into a new repo on your GitHub and redeploys whenever that repo changes. This runs on Cloudflare's builders, not GitHub Actions, so it doesn't use any Actions minutes.

**Option B: from your terminal.** This needs Node 18+.

```bash
git clone https://github.com/moderniselife/deskling && cd deskling/relay
npm install
npx wrangler login                        # opens the browser once
npx wrangler secret put DESKLING_TOKEN    # paste your long random password
npx wrangler deploy                       # prints your relay URL
```

**Check it:** open your relay URL. It redirects to the setup page, `https://<relay>/setup`. Paste your token. If you see "0 screens connected", it's working.

## 2. Try it in the browser before the screen exists (2 min)

Open `https://<relay>/sim` in another tab and paste the same token. That's a working Deskling in your browser, talking to your relay with the same protocol as the real screen.

Back on the **/setup** tab, press the buttons under **Try it**:

| Button | What you should see in the simulator |
| --- | --- |
| Send a notification | It appears in the feed, and Deskling waves |
| Remind me in 1 minute | A reminder card. A minute later it starts nudging you |
| Add a pretend session / Remove it | A "demo-project" session appears / disappears |
| Ask for permission | A full-screen request. Tap Allow, and the setup page says "You tapped Allow ✔" |
| Ask a question | Three options. Tap one, and the setup page shows your answer |

If these work, your relay is fine. Anything that goes wrong later is on the agent side or the screen side.

## 3. Connect Claude Code (3 min, on every computer you use)

In Claude Code (terminal or the desktop app), on Mac or Windows:

```
/plugin marketplace add moderniselife/deskling
/plugin install deskling@deskling
```

It asks for your **relay URL** and **token**. Give the computer a name if you like (it defaults to its hostname). Then restart Claude Code.

**Check it:** start a session in any project. Within a second, it shows up in `/sim` as a session. Ask Claude to do something that needs permission, e.g. "run `ls` in a new terminal". The simulator jumps to the permission screen. Tap **Allow** and Claude carries on.

Now try `remind me in 2 minutes to stretch`. Claude calls the `deskling_remind` tool, and the reminder lands on the screen.

**Messaging idle sessions** (optional): start Claude with `claude --channels plugin:deskling@deskling`. Text you type on the screen then reaches the session even while it's sitting idle. Without the flag, messages are delivered the next time the session stops or you send a prompt.

**Cursor and scripts:** the setup page has a `~/.deskling/config` snippet to copy. See [hooks/README](../hooks/README.md) for the Cursor hooks and the `deskling` command (`deskling notify "Deploy finished"`).

## 4. Install the firmware (5 min, the only time you need a cable)

1. Use a USB-C **data** cable to connect the board to your Mac or PC. Use the port next to the CH340 chip, usually labelled UART. On Windows, you may need the [CH340 driver](https://www.wch-ic.com/downloads/CH341SER_EXE.html).
2. In **Chrome or Edge**, open `https://<relay>/flash`.
3. Pick your panel. It's on the back label, `SKU:10153001 (XXXX)`: below 2624 is the original panel, 2624 or higher is the newer one. Yours says 2521, so that's the original.
4. Click **Connect and install** and choose the serial port. The page works out the chip revision and downloads the right build. It takes about 2 minutes.

You can unplug the cable now and power it from any 5 V / 1 A+ USB supply.

## 5. First boot: Wi-Fi and relay (3 min, no cable)

On the screen:

1. Tap **Choose Wi-Fi**, pick your network and type the password on the on-screen keyboard.
2. Deskling joins your network and shows a QR code, an address (usually `http://deskling.local`) and a **4-digit code**.

On your phone or computer, on the same Wi-Fi:

3. Scan the QR or open the address, then type the 4-digit code.
4. Paste your **relay URL** and **token**, and save. (No second device handy? Tap **Type it in here instead** and use the on-screen keyboard.)

The screen says **All set!** and jumps to the home screen. The `/setup` page now says **1 screen connected**. Press the **Try it** buttons again, and this time they appear on your desk.

> If the Wi-Fi join fails, Deskling makes its own hotspot called `Deskling-XXXX`. Join it from your phone and open `http://192.168.4.1` to set Wi-Fi and the relay from there.

## 6. Updates, forever, without the cable

- **Over the air:** Deskling checks for new firmware when it joins Wi-Fi, then every 12 hours. Tap the rail's **Settings** icon, then **Firmware** to check now and install. If a new build doesn't come up healthy within 3 minutes, it rolls back to the previous one by itself.
- **From your computer:** the `deskling.local` page has a firmware upload box for a `.bin` you built yourself.
- **Changing Wi-Fi, relay, brightness or time zone:** use Settings on the screen, or the `deskling.local` page.

## Troubleshooting

| Problem | What to do |
| --- | --- |
| `/setup` says the token doesn't match | It's case-sensitive. Re-run `npx wrangler secret put DESKLING_TOKEN`, or change it in the Cloudflare dashboard → Workers → deskling-relay → Settings → Variables. Then update the plugin (`/plugin` → deskling → configure) and the screen. |
| Sessions don't show up | Run `/plugin` in Claude Code and check deskling is enabled and configured. Hooks give up after a few seconds, so a wrong URL never blocks Claude, it just stays silent. |
| Permission requests don't reach the screen | The plugin waits 30 s by default (the `approval_wait` setting) before falling back to the normal prompt on your computer. Make sure the screen or `/sim` is open. |
| `/flash` can't see a port | Try another cable (many are charge-only) or another USB-C port on the board. On Windows, install the CH340 driver. |
| `/flash` says "Couldn't load the firmware list" | No release is published yet; see [firmware/README](../firmware/README.md#releases). |
| Three colour bars or a garbled picture after flashing | Wrong panel option. Flash again and pick the other panel. |
| Touches land in the wrong place | Open an issue saying which way they're off (mirrored, swapped, upside down). It's a build option (`DESKLING_TOUCH_*`), so the fix ships as an update. |
| Stuck on "Joining Wi-Fi…" | Deskling only sees 2.4 GHz networks, so check your router has 2.4 GHz turned on. If the log shows SDIO errors, the C6 co-processor firmware needs updating ([firmware/README](../firmware/README.md#3-if-somethings-off)). |
| `deskling.local` doesn't open | Some Windows and Android setups don't resolve `.local`. Use the IP address shown on the screen instead. |
