// web/hubapp.js: the hub as an app, on every device page (make_fm1 puts it in at /*APP*/; the manifest, icons: fm1/app/,
// the worker: fm1/sw.js). Android / Chrome / Edge: "Install" (the browser's own prompt); iPhone / iPad: "Add to Home Screen"
// by hand, and why installing a firmware does not work there (no Web MIDI in any iOS browser).
(() => {
  const ua = navigator.userAgent;
  const ios = /iPad|iPhone|iPod/.test(ua) || (navigator.platform === "MacIntel" && navigator.maxTouchPoints > 1);   // (an iPad says "Mac")
  const standalone = matchMedia("(display-mode: standalone)").matches || navigator.standalone === true;
  const touch = matchMedia("(pointer: coarse)").matches;
  const wanted = !standalone && new URLSearchParams(location.search).has("app");   // (the QR code's /?app=1)
  window.hubApp = {
    ios, standalone,
    // (what the Install / Connect buttons say without Web MIDI: on iOS no other browser helps)
    noMidi: ios ? "Installing doesn't work on iPhone or iPad yet: no browser there has Web MIDI. Use Chrome or Edge on a computer, or Chrome on Android."
                : "This browser has no Web MIDI: use Chrome or Edge.",
  };

  // the worker: always the newest page, a copy only offline; asked for a new version each time the app comes back
  let reg = null;
  if ("serviceWorker" in navigator && isSecureContext)
    navigator.serviceWorker.register("/sw.js", { updateViaCache: "none" }).then((r) => { reg = r; }).catch(() => {});

  // Shop in the menu (Amazon.de affiliate links): Germany and Austria only, by the country Cloudflare sees (/api/cc, nothing kept)
  fetch("/api/cc", { cache: "no-store" }).then((r) => r.json())
    .then((j) => { if (/^(DE|AT)$/.test(j.cc)) document.getElementById("shopdd")?.removeAttribute("hidden"); }).catch(() => {});

  // the app left in the background for a while (a phone keeps it for days): the page anew when it comes back,
  // never while an install writes (fm1.html's `writing`) or a dialog is open
  let away = 0;
  document.addEventListener("visibilitychange", () => {
    if (document.hidden) { away = Date.now(); return; }
    reg?.update().catch(() => {});
    const busy = (typeof writing !== "undefined" && writing) || document.querySelector("dialog[open]");
    if (standalone && away && Date.now() - away > 30 * 60e3 && !busy) location.reload();
  });

  // the bar at the bottom: on a phone, once (closed: 60 days quiet); "Get the app" in the Devices menu shows it any time
  const KEY = "fm1hub.appbar";
  let prompt = null, bar = null;
  const quiet = () => { try { return Date.now() < +(localStorage.getItem(KEY) || 0); } catch { return false; } };
  const hush = () => { try { localStorage.setItem(KEY, String(Date.now() + 60 * 864e5)); } catch {} };
  const SHARE = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3v12M8 7l4-4 4 4M6 11H5v10h14V11h-1"/></svg>';
  // the panel: iPhone / iPad the how-to (no prompt there), a phone the browser's Install, a computer the code to scan
  // with the phone (and Install for this computer where the browser offers it)
  function show() {
    if (standalone) return;
    bar?.remove();
    bar = document.createElement("div");
    bar.className = "appbar";
    bar.setAttribute("role", "dialog");
    bar.setAttribute("aria-label", "The Firmware Hub as an app");
    const icon = '<img src="/app/icon-192.png" alt="" width="44" height="44">';
    const go = prompt ? '<button class="go" type="button">Install</button>' : "";
    if (ios) {
      bar.innerHTML = `${icon}<div class="t"><b>The hub on your Home Screen</b>
        <span>Tap Share ${SHARE} then <b>Add to Home Screen</b>.</span>
        <small>Browse, rate, keep favourites. Installing a firmware needs Chrome or Edge on a computer, or Chrome on Android: iPhone and iPad have no Web MIDI yet.</small></div>`;
    } else if (touch) {
      bar.innerHTML = `${icon}<div class="t"><b>The Firmware Hub as an app</b>` + (prompt
        ? "<span>On your home screen, always the newest firmwares.</span></div>" + go
        : "<span>In Chrome: the menu <b>⋮</b>, then <b>Install app</b> or <b>Add to Home screen</b>.</span></div>");
    } else {
      bar.classList.add("wide");
      bar.innerHTML = `<img class="qr" src="/app/qr.svg" alt="QR code: thefirmwarehub.com" width="132" height="132"><div class="t">
        <b>The Firmware Hub on your phone</b>
        <span>Scan the code with your phone’s camera. The hub opens, then:</span>
        <span><b>Android:</b> tap <b>Install</b> in the bar at the bottom (or <b>📱 Get the app</b>).</span>
        <span><b>iPhone:</b> tap Share ${SHARE} then <b>Add to Home Screen</b>.</span>
        <small>Always the newest firmwares, your favourites and stars on every device. Installing a firmware from the phone: Android with Chrome and a USB-C cable (iPhone: no Web MIDI yet).</small>
        ${prompt ? '<span class="pc">Or as an app on this computer: <button class="go" type="button">Install</button></span>' : ""}</div>`;
    }
    bar.querySelector(".go")?.addEventListener("click", async () => {
      const p = prompt; prompt = null;
      p.prompt();
      const { outcome } = await p.userChoice.catch(() => ({}));
      if (outcome === "accepted") bar.remove(); else hush();
    });
    const x = document.createElement("button");
    x.className = "x"; x.type = "button"; x.setAttribute("aria-label", "Close"); x.textContent = "×";
    x.onclick = () => { hush(); bar.remove(); };
    bar.append(x);
    document.body.append(bar);
  }
  addEventListener("beforeinstallprompt", (e) => {      // (Chrome / Edge: installable; their own mini bar replaced by ours)
    e.preventDefault(); prompt = e;
    // (a phone: the bar with Install; one already open without it (the QR code's how-to) gets it now)
    if (touch && (bar?.isConnected ? !bar.querySelector(".go") : !quiet())) show();
  });
  addEventListener("appinstalled", () => { bar?.remove(); prompt = null; });
  addEventListener("DOMContentLoaded", () => {
    // visible: "Get the app" beside the page's buttons (FM-1, SMK-37), and in the Devices menu; gone inside the app
    const row = document.querySelector("header .bar");
    if (row && !standalone) {
      const b = Object.assign(document.createElement("a"), { className: "btn ghost getapp", href: "#app", textContent: "📱 Get the app" });
      row.insertBefore(b, row.querySelector(".btn.coffee"));
    }
    document.querySelectorAll("#getapp, .getapp").forEach((a) => {
      if (standalone) return a.remove();
      a.onclick = (e) => {                               // (on a phone the browser's prompt right away where there is one)
        e.preventDefault(); document.getElementById("dd")?.classList.remove("open");
        show(); if (touch) bar?.querySelector(".go")?.click();
      };
    });
    if (location.hash === "#app" || wanted) show();      // (the menu's link from VIBE's page; the QR code: came for the app)
    else if (ios && !standalone && !quiet()) setTimeout(show, 1500);   // (Safari has no prompt: the bar a moment after the page)
  });
})();
