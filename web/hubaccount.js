// FAVOURITES AND RATINGS (/api/, hubapi/api.php), the same on every device page (tools/make_fm1.py puts this file in at
// /*ACCOUNT*/; its style is web/hubaccount.css). Favourites work without an account: on this browser only (localStorage).
// Ratings need one: a sign-in by mail, no password (a link, or a 6-digit code typed on any device); signed in, favourites
// and ratings follow the address to every device, and the browser's favourites move into the account.
// The page sets HUB before this: cards() its card elements (each with data-hub = its id: "zp12", "smk37:obervave"),
// name(id), refresh() (the chips and the filter again), resort() (the order again, when it is by rating).
const FAV_KEY = "fm1hub.favs", PEND_KEY = "fm1hub.pending";
let me = null, rates = {};                              // me: {email, vibe, favs, ratings: {id: 1..5}} or null; rates: {id: [average, count]}
let localFavs = (() => { try { const l = JSON.parse(localStorage.getItem(FAV_KEY) || "[]"); return Array.isArray(l) ? l : []; } catch (e) { return []; } })();
const keepLocal = () => { try { localStorage.setItem(FAV_KEY, JSON.stringify(localFavs)); } catch (e) {} };
const isFav = (id) => (me ? me.favs : localFavs).includes(id);
const score = (id) => { const r = rates[id]; return r ? (r[0] * r[1] + 3 * 2) / (r[1] + 2) : 0; };   // (a few votes weigh little: 2 of 3 stars in front)
const hubEsc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const hub$ = (id) => document.getElementById(id);
async function api(path, data) {
  const r = await fetch("/api/" + path, data === undefined ? { cache: "no-store", credentials: "same-origin" }
    : { method: "POST", credentials: "same-origin", headers: { "Content-Type": "application/json", "X-Hub": "1" }, body: JSON.stringify(data) });
  let j = {};
  try { j = await r.json(); } catch (e) {}
  if (!r.ok || !j.ok) { const err = new Error(j.error || `The hub's server did not answer (${r.status}).`); err.status = r.status; throw err; }
  return j;
}
const evA = (action, name) => { try { (window._paq = window._paq || []).push(["trackEvent", "Account", action, name || ""]); } catch (e) {} };
function toast(text) {
  const t = document.createElement("div");
  t.className = "toast"; t.textContent = text; t.setAttribute("role", "status");
  document.body.append(t); setTimeout(() => t.remove(), 3200);
}
// one card's heart and stars, as they are now (after a render, a click, a sign-in); a card without them gets them here
function paintCard(el) {
  const id = el.dataset.hub;
  if (!id) return;
  let h = el.querySelector(".fav");
  if (!h) { h = document.createElement("button"); h.type = "button"; h.className = "fav"; el.prepend(h); }
  const on = isFav(id);
  h.setAttribute("aria-pressed", on); h.textContent = on ? "♥" : "♡";
  h.title = on ? "In your favourites: click to take it out" : "Add to your favourites";
  h.setAttribute("aria-label", h.title + ": " + HUB.name(id));
  h.onclick = () => toggleFav(id, h);
  let r = el.querySelector(".rate");
  if (!r) { const by = el.querySelector(".by"); if (!by) return; r = document.createElement("span"); r.className = "rate"; by.append(r); }
  const mine = me && me.ratings[id], avg = rates[id], fill = mine || (avg ? Math.round(avg[0]) : 0);
  r.classList.toggle("mine", !!mine);
  r.title = mine ? `Your rating: ${mine} of 5. Click to change, click it again to take it back` : me ? "Rate it: click a star" : "Rate it (sign in by mail, no password)";
  r.innerHTML = [1, 2, 3, 4, 5].map((n) => `<button type="button" data-n="${n}" class="${n <= fill ? "on" : ""}" aria-label="${n} of 5 stars">★</button>`).join("")
    + `<span class="rn">${avg ? `<b>${avg[0].toFixed(1)}</b> (${avg[1]})` : "rate it"}</span>`;
  const bs = [...r.querySelectorAll("button")];
  bs.forEach((b) => {
    b.onmouseenter = () => bs.forEach((x) => x.classList.toggle("hov", +x.dataset.n <= +b.dataset.n));
    b.onclick = () => rate(id, +b.dataset.n === mine ? 0 : +b.dataset.n);
  });
  r.onmouseleave = () => bs.forEach((x) => x.classList.remove("hov"));
}
const paintAll = () => { HUB.cards().forEach(paintCard); acct(); };
async function toggleFav(id, h) {
  const on = !isFav(id);
  h.classList.remove("pop"); void h.offsetWidth; if (on) h.classList.add("pop");
  evA(on ? "Fav" : "Unfav", id);
  if (!me) {
    localFavs = on ? [...new Set([...localFavs, id])] : localFavs.filter((x) => x !== id);
    keepLocal(); paintAll(); HUB.refresh();
    if (on && localFavs.length === 1) toast("Saved on this browser. Sign in to keep your favourites on every device.");
    return;
  }
  me.favs = on ? [...new Set([...me.favs, id])] : me.favs.filter((x) => x !== id);   // (at once; the server's answer follows)
  paintAll(); HUB.refresh();
  try { me = (await api("fav", { fw: id, on })).user; } catch (e) { toast(e.message); await loadMe(); }
  paintAll(); HUB.refresh();
}
async function rate(id, stars) {
  if (!me) {                                            // (after the sign-in this rating is given: kept until then)
    try { localStorage.setItem(PEND_KEY, JSON.stringify({ fw: id, stars })); } catch (e) {}
    return dialog("email", `To rate ${HUB.name(id)}, sign in. One account, no password: a link or a code by mail.`);
  }
  try {
    const j = await api("rate", { fw: id, stars });
    me = j.user; rates = j.ratings; evA(stars ? "Rate" : "Unrate", id + (stars ? " " + stars : ""));
    if (stars) toast(`Thanks! ${stars} of 5 for ${HUB.name(id)}.`);
  } catch (e) { toast(e.message); }
  paintAll(); HUB.resort();
}
// after a sign-in: the browser's favourites into the account, a rating given before it
async function signedIn(user) {
  me = user;
  if (localFavs.length) {
    try { me = (await api("merge", { favs: localFavs })).user; localFavs = []; keepLocal(); } catch (e) {}
  }
  let p = null;
  try { p = JSON.parse(localStorage.getItem(PEND_KEY) || "null"); localStorage.removeItem(PEND_KEY); } catch (e) {}
  if (p && p.fw && p.stars) await rate(p.fw, p.stars);
  paintAll(); HUB.refresh();
}
const JUST_IN = new URLSearchParams(location.search).has("signedin");   // (back from the mail's link: the address clean again)
if (JUST_IN) { const u = new URLSearchParams(location.search); u.delete("signedin"); history.replaceState(null, "", location.pathname + (u.size ? "?" + u : "") + location.hash); }
async function loadMe() {
  try {
    const j = await api("me");
    rates = j.ratings || {};
    if (j.user) { await signedIn(j.user); if (JUST_IN) toast("Signed in as " + j.user.email); }
    else { me = null; paintAll(); HUB.refresh(); }
    HUB.resort();
  } catch (e) { paintAll(); }                           // (no server: the hearts on this browser still work)
}
// the account button in the toolbar (beside View), and its dialog: the address, then the code; signed in: sign out, delete
function acct() {
  let a = hub$("acct");
  if (!a) { const o = hub$("opts"); if (!o) return; a = document.createElement("div"); a.className = "sorts"; a.id = "acct"; o.after(a); }
  a.innerHTML = me ? `<button type="button" class="in" title="Signed in as ${hubEsc(me.email)}">Account</button>` : '<button type="button">Sign in</button>';
  a.querySelector("button").onclick = () => dialog(me ? "account" : "email");
}
let mailTo = "";
function dialog(step, why = "") {
  let d = hub$("acctdlg");
  if (!d) {
    document.body.insertAdjacentHTML("beforeend", '<dialog class="dlg" id="acctdlg" aria-label="Your account"><form method="dialog" class="x"><button aria-label="Close">×</button></form><div id="acctbody"></div></dialog>');
    d = hub$("acctdlg");
  }
  const b = hub$("acctbody");
  const privacy = '<p class="small">Your address is only used to sign you in (and for the VIBE FM list, if you tick it): no sharing. You can delete the account any time. <a href="/impressum/#datenschutz">Privacy</a></p>';
  if (step === "email") {
    b.innerHTML = `<h3>Sign in to the <b>hub</b></h3>
      <p>${hubEsc(why || "Rate firmwares and keep your favourites on every device.")}</p>
      <p class="small">No password: we mail you a sign-in link and a 6-digit code. Either one signs you in.</p>
      <form id="f-mail" novalidate><input type="email" id="in-mail" autocomplete="email" placeholder="you@example.com" required value="${hubEsc(mailTo)}">
      <label class="hp" aria-hidden="true">Website <input type="text" id="in-web" tabindex="-1" autocomplete="off"></label>
      <label class="vb"><input type="checkbox" id="in-vibe"> <span>Also put me on the <a href="/vibe/?ref=hub-account" target="_blank" rel="noopener">VIBE FM</a> list: news about VIBE FM, the synth made for open firmware. Unsubscribe any time.</span></label>
      <p class="err" id="d-err"></p><div class="row"><button class="go" id="b-send">Send me the mail</button>
      ${mailTo ? '<button type="button" class="lnk" id="b-have">I have a code</button>' : ""}</div></form>${privacy}`;
    hub$("f-mail").onsubmit = async (e) => {
      e.preventDefault();
      const email = hub$("in-mail").value.trim(), btn = hub$("b-send");
      if (!/^\S+@\S+\.\S+$/.test(email)) { hub$("d-err").textContent = "Please check the address."; return; }
      btn.disabled = true; btn.textContent = "Sending…";
      try { await api("login", { email, website: hub$("in-web").value, vibe: hub$("in-vibe").checked }); mailTo = email; evA("Login mail", hub$("in-vibe").checked ? "vibe" : ""); dialog("code"); }
      catch (err) { hub$("d-err").textContent = err.message; btn.disabled = false; btn.textContent = "Send me the mail"; }
    };
    if (hub$("b-have")) hub$("b-have").onclick = () => dialog("code");
  } else if (step === "code") {
    b.innerHTML = `<h3>Check your <b>inbox</b></h3>
      <p>We sent a mail to <b>${hubEsc(mailTo)}</b>. Click its link, or type its code here (on any device):</p>
      <form id="f-code"><input class="code" id="in-code" inputmode="numeric" autocomplete="one-time-code" maxlength="7" placeholder="000000" required>
      <p class="err" id="d-err"></p><div class="row"><button class="go" id="b-code">Sign in</button></div></form>
      <p class="small">Nothing there after a minute? Look in the spam folder. <button type="button" class="lnk" id="b-again">Another address or a new mail</button></p>`;
    hub$("f-code").onsubmit = async (e) => {
      e.preventDefault();
      const btn = hub$("b-code"); btn.disabled = true;
      try { const j = await api("code", { email: mailTo, code: hub$("in-code").value }); d.close(); evA("Login code"); await signedIn(j.user); toast("Signed in as " + j.user.email); }
      catch (err) { hub$("d-err").textContent = err.message; btn.disabled = false; }
    };
    hub$("b-again").onclick = () => dialog("email");
    setTimeout(() => hub$("in-code").focus(), 50);
  } else {
    b.innerHTML = `<h3>Your <b>account</b></h3>
      <p>Signed in as <b>${hubEsc(me.email)}</b>.</p>
      ${me.vibe ? '<p class="small">On the <a href="/vibe/" target="_blank" rel="noopener">VIBE FM</a> list: each of its mails has an unsubscribe link.</p>' : ""}
      <p class="small">${me.favs.length} favourite${me.favs.length === 1 ? "" : "s"}, ${Object.keys(me.ratings).length} rating${Object.keys(me.ratings).length === 1 ? "" : "s"} (all devices). On another device, sign in with the same address.</p>
      <p class="err" id="d-err"></p><div class="row"><button type="button" class="quiet" id="b-out">Sign out</button><button type="button" class="danger" id="b-del">Delete my account</button></div>${privacy}`;
    hub$("b-out").onclick = async () => { try { await api("logout", {}); } catch (e) {} me = null; d.close(); paintAll(); HUB.refresh(); toast("Signed out on this device."); };
    hub$("b-del").onclick = async (e) => {
      if (e.currentTarget.dataset.sure !== "1") { e.currentTarget.dataset.sure = "1"; e.currentTarget.textContent = "Sure? Click again: all your favourites and ratings go"; return; }
      try { await api("delete", {}); me = null; d.close(); evA("Delete"); await loadMe(); toast("Your account is deleted."); }
      catch (err) { hub$("d-err").textContent = err.message; }
    };
  }
  if (!d.open) d.showModal();
  if (step === "email") setTimeout(() => hub$("in-mail").focus(), 50);
}
