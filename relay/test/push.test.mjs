// Web Push from a running relay, checked the way a browser would: a stand-in push service
// receives the message, and we decrypt it with the "browser's" keys and check the relay's
// signature.   DESKLING_URL=http://127.0.0.1:8787 DESKLING_TOKEN=dev-token node test/push.test.mjs
import { createServer } from "node:http";

const URL_BASE = process.env.DESKLING_URL ?? "http://127.0.0.1:8787";
const TOKEN = process.env.DESKLING_TOKEN ?? "dev-token";
const auth = { authorization: `Bearer ${TOKEN}`, "content-type": "application/json" };
const { subtle } = globalThis.crypto;
const enc = new TextEncoder();
let failures = 0;
const check = (name, ok, extra = "") => {
  console.log(`${ok ? "PASS" : "FAIL"}  ${name}${ok || !extra ? "" : "  " + extra}`);
  if (!ok) failures++;
};
const b64url = (b) => Buffer.from(b).toString("base64url");
const unb64 = (s) => new Uint8Array(Buffer.from(s, "base64url"));
const hmac = async (key, data) => new Uint8Array(await subtle.sign("HMAC", await subtle.importKey("raw", key, { name: "HMAC", hash: "SHA-256" }, false, ["sign"]), data));
const cat = (...a) => new Uint8Array(Buffer.concat(a.map((x) => Buffer.from(x))));

// The "browser": its keys and auth secret.
const ua = await subtle.generateKey({ name: "ECDH", namedCurve: "P-256" }, true, ["deriveBits"]);
const uaPublic = new Uint8Array(await subtle.exportKey("raw", ua.publicKey));
const authSecret = crypto.getRandomValues(new Uint8Array(16));

// The "push service".
const got = [];
const svc = createServer((req, res) => {
  const chunks = [];
  req.on("data", (d) => chunks.push(d));
  req.on("end", () => {
    got.push({ url: req.url, headers: req.headers, body: new Uint8Array(Buffer.concat(chunks)) });
    res.writeHead(req.url.includes("gone") ? 410 : 201).end();
  });
});
await new Promise((r) => svc.listen(0, "127.0.0.1", r));
const endpoint = `http://127.0.0.1:${svc.address().port}/push/browser-1`;

const { key } = await (await fetch(`${URL_BASE}/api/push/key`, { headers: auth })).json();
check("the relay has a VAPID key", typeof key === "string" && unb64(key).length === 65);
const sub = await fetch(`${URL_BASE}/api/push/subscribe`, { method: "POST", headers: auth, body: JSON.stringify({ endpoint, keys: { p256dh: b64url(uaPublic), auth: b64url(authSecret) } }) });
check("a browser can subscribe", sub.ok);
const bad = await fetch(`${URL_BASE}/api/push/subscribe`, { method: "POST", headers: auth, body: JSON.stringify({ endpoint: "http://example.com/x", keys: { p256dh: "a", auth: "b" } }) });
check("a plain-http endpoint elsewhere is refused", bad.status === 400);

await fetch(`${URL_BASE}/api/event`, { method: "POST", headers: auth, body: JSON.stringify({ kind: "notify", title: "Tests pass", body: "All 12 green", level: "good" }) });
for (let i = 0; i < 50 && !got.length; i++) await new Promise((r) => setTimeout(r, 100));
check("a notification is pushed", got.length === 1);
const m = got[0];
if (m) {
  check("it's sent as aes128gcm with a VAPID signature", m.headers["content-encoding"] === "aes128gcm" && /^vapid t=[\w-]+\.[\w-]+\.[\w-]+, k=/.test(m.headers.authorization ?? ""));
  // Check the signature with the relay's public key.
  const [, jwt, k] = /^vapid t=([^,]+), k=(.+)$/.exec(m.headers.authorization);
  const [h, c, sig] = jwt.split(".");
  const pub = await subtle.importKey("raw", unb64(k), { name: "ECDSA", namedCurve: "P-256" }, false, ["verify"]);
  const okSig = await subtle.verify({ name: "ECDSA", hash: "SHA-256" }, pub, unb64(sig), enc.encode(`${h}.${c}`));
  const claims = JSON.parse(Buffer.from(c, "base64url").toString());
  check("the signature checks out, for this push service", okSig && k === key && claims.aud === new URL(endpoint).origin && claims.exp > Date.now() / 1000);
  // Decrypt it the way the browser would (RFC 8291).
  const b = m.body;
  const salt = b.slice(0, 16);
  const idlen = b[20];
  const asPublic = b.slice(21, 21 + idlen);
  const sealed = b.slice(21 + idlen);
  const asKey = await subtle.importKey("raw", asPublic, { name: "ECDH", namedCurve: "P-256" }, false, []);
  const ecdh = new Uint8Array(await subtle.deriveBits({ name: "ECDH", public: asKey }, ua.privateKey, 256));
  const prkKey = await hmac(authSecret, ecdh);
  const ikm = await hmac(prkKey, cat(enc.encode("WebPush: info\0"), uaPublic, asPublic, [1]));
  const prk = await hmac(salt, ikm);
  const cek = (await hmac(prk, cat(enc.encode("Content-Encoding: aes128gcm\0"), [1]))).slice(0, 16);
  const nonce = (await hmac(prk, cat(enc.encode("Content-Encoding: nonce\0"), [1]))).slice(0, 12);
  let msg = null;
  try {
    const plain = new Uint8Array(await subtle.decrypt({ name: "AES-GCM", iv: nonce }, await subtle.importKey("raw", cek, "AES-GCM", false, ["decrypt"]), sealed));
    msg = JSON.parse(Buffer.from(plain.slice(0, plain.lastIndexOf(2))).toString());
  } catch (e) {
    msg = String(e);
  }
  check("only the browser can read it, and it says what happened", msg?.title === "Tests pass" && msg.body === "All 12 green", JSON.stringify(msg));
}

// A browser that's gone (the push service says 410) is forgotten.
await fetch(`${URL_BASE}/api/push/unsubscribe`, { method: "POST", headers: auth, body: JSON.stringify({ endpoint }) });
const goneEndpoint = endpoint.replace("browser-1", "gone");
await fetch(`${URL_BASE}/api/push/subscribe`, { method: "POST", headers: auth, body: JSON.stringify({ endpoint: goneEndpoint, keys: { p256dh: b64url(uaPublic), auth: b64url(authSecret) } }) });
got.length = 0;
await fetch(`${URL_BASE}/api/event`, { method: "POST", headers: auth, body: JSON.stringify({ kind: "notify", title: "One", body: "", level: "alert" }) });
for (let i = 0; i < 50 && !got.length; i++) await new Promise((r) => setTimeout(r, 100));
await new Promise((r) => setTimeout(r, 500));
got.length = 0;
await fetch(`${URL_BASE}/api/event`, { method: "POST", headers: auth, body: JSON.stringify({ kind: "notify", title: "Two", body: "", level: "alert" }) });
await new Promise((r) => setTimeout(r, 1500));
check("a browser that's gone is forgotten", got.length === 0);
const info = await fetch(`${URL_BASE}/api/event`, { method: "POST", headers: auth, body: JSON.stringify({ kind: "notify", title: "Quiet", body: "", level: "info" }) });
check("info notices aren't pushed", info.ok);

svc.close();
console.log(failures ? `\n${failures} failed` : "\nall passed");
process.exit(failures ? 1 : 0);
