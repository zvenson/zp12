// The hub as an app (web/hubapp.js registers this; fm1/app/manifest.webmanifest): always the newest page from the
// network, the copy kept here only when there is none (offline). Never kept: the account (/api/), the packages (/fw/),
// the sources (/src/), VIBE and its admin (/vibe), the simulators (/play/), other hosts (Matomo, GitHub).
const KEEP = "hub-v1";
const PASS = /^\/(api|fw|src|vibe|play)(\/|$)|\.php$/;
const PAGES = ["/", "/smk37/", "/kb/", "/impressum/"];

self.addEventListener("install", (e) => {
  self.skipWaiting();
  e.waitUntil(caches.open(KEEP).then((c) => Promise.all(PAGES.map((p) => c.add(new Request(p, { cache: "reload" })).catch(() => {})))));
});

self.addEventListener("activate", (e) => {
  e.waitUntil(caches.keys().then((ks) => Promise.all(ks.filter((k) => k !== KEEP).map((k) => caches.delete(k))))
                .then(() => self.clients.claim()));
});

self.addEventListener("fetch", (e) => {
  const r = e.request, u = new URL(r.url);
  if (r.method !== "GET" || u.origin !== location.origin || PASS.test(u.pathname)) return;
  // (an image under its fingerprint ?v= never changes: kept one; everything else: the network first)
  const fixed = /\.png$/.test(u.pathname) && u.searchParams.has("v");
  e.respondWith(fixed ? fromKeep(r).then((k) => k || fromNet(r, e)) : fromNet(r, e).then((res) => res.status >= 500 ? fromKeep(r).then((k) => k || res) : res,
                                                                                         () => fromKeep(r).then((k) => k || offline(r))));
});

// (no-cache: the browser asks the server each time, as the server's Cache-Control says; ok answers kept for offline)
function fromNet(r, e) {
  return fetch(r.mode === "navigate" ? r : new Request(r, { cache: "no-cache" })).then((res) => {
    if (res.ok && res.type === "basic") { const c = res.clone(); e.waitUntil(caches.open(KEEP).then((k) => k.put(key(r), c))); }
    return res;
  });
}
// (a page under its path alone: /?app=1 and /?q=acid are the same page offline; the catalogue under its path: yesterday's key too)
const key = (r) => { const u = new URL(r.url); return /\.png$/.test(u.pathname) ? r : u.origin + u.pathname; };
const fromKeep = (r) => caches.open(KEEP).then((k) => k.match(key(r)));
function offline(r) {
  if (r.mode !== "navigate") return Response.error();
  return new Response('<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">' +
    '<title>Offline – The Firmware Hub</title><body style="margin:0;display:grid;place-items:center;min-height:100vh;background:#151213;' +
    'color:#efe8e1;font:17px/1.6 -apple-system,Segoe UI,Roboto,Arial,sans-serif;text-align:center;padding:24px;box-sizing:border-box">' +
    '<div><p style="font-size:22px;font-weight:800;color:#42f5f5;margin:0 0 8px">You are offline</p>' +
    '<p style="margin:0 0 20px">The Firmware Hub needs the internet for this page.</p>' +
    '<button onclick="location.reload()" style="font:inherit;padding:10px 22px;border-radius:999px;border:0;background:#42f5f5;color:#151213;font-weight:700">Try again</button></div>',
    { status: 503, headers: { "Content-Type": "text/html; charset=utf-8" } });
}
