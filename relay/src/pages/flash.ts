// /flash: install Deskling's firmware from the browser (Chrome or Edge, USB cable, once).
// It picks the right build for the board's chip revision automatically.
export const FLASH_HTML = /* html */ `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Install Deskling</title>
<style>
:root{--ground:#FBF6EF;--card:#fff;--ink:#2A2433;--soft:#3A3346;--muted:#6A6274;--lilac:#CFC4F2;--purple:#5B4FA8;--good:#2F8A63;--goodbg:#D3EEDF;--alert:#C8553D;--alertbg:#FDE8E1;--line:#E4DCCF}
*{box-sizing:border-box}
body{margin:0;background:var(--ground);color:var(--ink);font:600 17px/1.5 ui-rounded,"Nunito","SF Pro Rounded",system-ui,sans-serif}
main{max-width:760px;margin:0 auto;padding:32px 16px 64px}
h1{font-size:40px;margin:0 0 4px;font-weight:900}
h2{font-size:14px;letter-spacing:1.2px;text-transform:uppercase;color:var(--muted);margin:0 0 10px}
section{background:var(--card);border-radius:26px;padding:22px;margin:16px 0}
.muted{color:var(--muted);font-weight:600}
ol{padding-left:22px;margin:8px 0}li{margin:6px 0}
.choices{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px}
.choice{border:3px solid var(--line);border-radius:20px;padding:14px 16px;cursor:pointer;display:block}
.choice:has(input:checked){border-color:var(--ink);background:var(--ground)}
.choice input{margin-right:8px}
button{font:inherit;font-weight:900;font-size:20px;border:none;border-radius:20px;background:var(--ink);color:var(--ground);padding:16px 28px;cursor:pointer;min-height:56px}
button:disabled{opacity:.5;cursor:default}
progress{width:100%;height:18px;accent-color:var(--purple)}
.status{font-weight:800;margin-top:10px}.ok{color:var(--good)}.bad{color:var(--alert)}
pre{background:var(--ground);border-radius:14px;padding:12px;max-height:180px;overflow:auto;font-size:13px;white-space:pre-wrap}
.warn{background:var(--alertbg);border-radius:18px;padding:14px 16px}
label.check{display:flex;gap:10px;align-items:center;margin-top:12px}
code{background:var(--ground);padding:2px 6px;border-radius:6px}
</style>
</head>
<body>
<main>
  <h1>Install Deskling</h1>
  <p class="muted">You only do this once, with a cable. After that, Deskling updates itself over Wi-Fi.</p>

  <div id="nosupport" class="warn" hidden><b>This browser can't talk to USB devices.</b> Open this page in Chrome or Edge on a Mac or PC.</div>

  <section>
    <h2>1 · Plug it in</h2>
    <ol>
      <li>Use a USB-C <b>data</b> cable (some cables are charge-only).</li>
      <li>Plug it into the USB-C port on the board that's labelled <b>UART</b> or sits next to the CH340 chip. If nothing shows up in step 3, try the next port.</li>
      <li>On Windows, if no port appears, install the <a href="https://www.wch-ic.com/downloads/CH341SER_EXE.html" target="_blank" rel="noopener">CH340 driver</a>. Macs already have it.</li>
    </ol>
  </section>

  <section>
    <h2>2 · Which screen do you have?</h2>
    <p class="muted">Look at the label on the back: the number in brackets after the SKU, e.g. <code>SKU:10153001 (2521)</code>.</p>
    <div class="choices">
      <label class="choice"><input type="radio" name="lcd" value="lcd1" checked> <b>Below 2624</b><br><span class="muted">Original panel (most boards from 2025)</span></label>
      <label class="choice"><input type="radio" name="lcd" value="lcd2"> <b>2624 or higher</b><br><span class="muted">Newer panel</span></label>
    </div>
    <label class="check"><input type="checkbox" id="erase" checked> Erase everything first (recommended the first time)</label>
  </section>

  <section>
    <h2>3 · Install</h2>
    <button id="go">Connect and install</button>
    <p class="muted" id="version"></p>
    <progress id="prog" max="100" value="0" hidden></progress>
    <div id="status" class="status"></div>
    <details><summary class="muted">Details</summary><pre id="log"></pre></details>
  </section>

  <section>
    <h2>Something not right?</h2>
    <p class="muted">Black screen, or stuck? This restarts Deskling and shows what it says while it starts up. Plug into the same USB-C port you installed through, click the button, pick the port, and send the log to whoever's helping you.</p>
    <div style="display:flex;gap:10px;flex-wrap:wrap">
      <button id="mon" style="font-size:17px;min-height:48px;padding:10px 20px">Show the log</button>
      <button id="moncopy" style="font-size:17px;min-height:48px;padding:10px 20px;background:var(--card);color:var(--ink);border:3px solid var(--ink)" hidden>Copy log</button>
    </div>
    <pre id="monlog" hidden style="max-height:420px"></pre>
  </section>

  <section id="next" hidden>
    <h2>4 · Say hi</h2>
    <p>Deskling is restarting. In a few seconds it'll ask you to pick your Wi-Fi on the screen, then show you an address to open on this computer, where you paste your relay details. Get them from <a href="/setup">your relay's setup page</a>.</p>
  </section>
</main>

<script type="module">
const FIRMWARE = "__FIRMWARE_URL__";
const $ = (id) => document.getElementById(id);
const log = (m) => { $('log').textContent += m + "\\n"; $('log').scrollTop = 1e9; };
const status = (m, cls = '') => { $('status').textContent = m; $('status').className = 'status ' + cls; };

if (!('serial' in navigator)) { $('nosupport').hidden = false; $('go').disabled = true; $('mon').disabled = true; }

let manifest = null;
fetch(FIRMWARE + 'manifest.json', { cache: 'no-store' })
  .then((r) => r.ok ? r.json() : Promise.reject(r.status))
  .then((m) => { manifest = m; $('version').textContent = 'Latest firmware: ' + m.version + ' (' + (m.date || '') + ')'; })
  .catch(() => { $('version').textContent = 'Couldn\\u2019t load the firmware list. Has a release been published yet?'; });

// Serial monitor: restart the board and stream its boot log (UART0 through the CH340, 115200 baud).
let monPort = null, monStop = false;
$('mon').onclick = async () => {
  if (monPort) { monStop = true; return; }
  const out = $('monlog');
  const add = (t) => { out.textContent = (out.textContent + t).slice(-60000); out.scrollTop = 1e9; };
  try {
    monPort = await navigator.serial.requestPort();
    await monPort.open({ baudRate: 115200 });
    out.hidden = false; out.textContent = ''; $('moncopy').hidden = false;
    $('mon').textContent = 'Stop';
    add('[restarting Deskling\\u2026]\\n');
    await monPort.setSignals({ dataTerminalReady: false, requestToSend: true });
    await new Promise((r) => setTimeout(r, 120));
    await monPort.setSignals({ dataTerminalReady: false, requestToSend: false });
    monStop = false;
    const reader = monPort.readable.getReader();
    const dec = new TextDecoder();
    const stopper = setInterval(() => { if (monStop) reader.cancel(); }, 200);
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        add(dec.decode(value, { stream: true }).replace(/\\x1b\\[[0-9;]*m/g, ''));
      }
    } finally {
      clearInterval(stopper);
      reader.releaseLock();
    }
  } catch (e) {
    add('\\n[' + (e && e.message ? e.message : e) + ']\\n');
  } finally {
    try { await monPort?.close(); } catch {}
    monPort = null;
    $('mon').textContent = 'Show the log';
  }
};
$('moncopy').onclick = async () => { await navigator.clipboard.writeText($('monlog').textContent); $('moncopy').textContent = 'Copied!'; setTimeout(() => $('moncopy').textContent = 'Copy log', 1200); };

$('go').onclick = async () => {
  if (!manifest) return status('No firmware to install yet.', 'bad');
  const lcd = document.querySelector('input[name=lcd]:checked').value;
  $('go').disabled = true;
  let transport, timer = null;
  try {
    const { ESPLoader, Transport } = await import('https://cdn.jsdelivr.net/npm/esptool-js@0.7.0/+esm');
    const port = await navigator.serial.requestPort();
    transport = new Transport(port, true);
    const loader = new ESPLoader({
      transport, baudrate: 921600,
      terminal: { clean() {}, writeLine: (d) => log(d), write: (d) => log(d) },
    });
    status('Connecting\\u2026');
    const chip = await loader.main();
    log('Chip: ' + chip);
    if (!/ESP32-P4/.test(chip)) throw new Error('That\\u2019s an ' + chip + ', not Deskling\\u2019s ESP32-P4. Try the other USB-C port.');
    const major = await loader.chip.getMajorChipVersion(loader);
    const variant = (major >= 3 ? 'p4r3' : 'p4r1') + '-' + lcd;
    const build = manifest.builds[variant];
    if (!build) throw new Error('No build published for ' + variant + ' yet.');
    log('Chip revision v' + major + '.x \\u2192 build ' + variant);

    status('Downloading ' + manifest.version + '\\u2026');
    const res = await fetch(FIRMWARE + build.full);
    if (!res.ok) throw new Error('Download failed (' + res.status + ')');
    const data = new Uint8Array(await res.arrayBuffer());
    log('Image: ' + (data.length / 1048576).toFixed(1) + ' MB');

    $('prog').hidden = false;
    $('prog').removeAttribute('value'); // indeterminate: the browser animates it
    const t0 = Date.now();
    const what = $('erase').checked ? 'Erasing the old firmware' : 'Getting ready';
    const tick = () => status(what + '\\u2026 ' + Math.round((Date.now() - t0) / 1000) + ' s (this part can take up to 2 minutes, with no progress bar)');
    tick();
    timer = setInterval(tick, 1000);
    await loader.writeFlash({
      fileArray: [{ data, address: 0 }],
      flashMode: 'keep', flashFreq: 'keep', flashSize: 'keep',
      eraseAll: $('erase').checked, compress: true,
      reportProgress: (_i, written, total) => { if (timer) { clearInterval(timer); timer = null; } $('prog').value = (written / total) * 100; status('Installing\\u2026 ' + Math.round((written / total) * 100) + '%'); },
    });
    await loader.after('hard_reset');
    status('Done! Deskling is starting up.', 'ok');
    $('next').hidden = false;
  } catch (e) {
    console.error(e);
    status(e && e.message ? e.message : String(e), 'bad');
    log(String(e && e.stack || e));
  } finally {
    if (timer) clearInterval(timer);
    try { await transport?.disconnect(); } catch {}
    $('go').disabled = false;
  }
};
</script>
</body>
</html>`;
