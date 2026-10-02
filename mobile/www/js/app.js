/*
 * app.js - BricoHams RT-950 Programmer (web / Android)
 * Same operations as the desktop Toolkit: read, edit, country codeplugs,
 * write (differential, verified), .rt950 files, CHIRP export and backups.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
(async function () {
"use strict";
const R = window.RT950;
const $ = id => document.getElementById(id);
const VERSION = "0.3.0";
let lang = "es", cp = null, transport = null, link = null, base = null, LEGAL = null, COUNTRIES = {};
const store = {
  get(k, d) { try { const v = localStorage.getItem("bh." + k); return v == null ? d : JSON.parse(v); } catch (e) { return d; } },
  set(k, v) { try { localStorage.setItem("bh." + k, JSON.stringify(v)); return true; } catch (e) { return false; } },
};
const ES = {};
document.querySelectorAll("[data-t]").forEach(el => { ES[el.dataset.t] = el.textContent; });
document.querySelectorAll("[data-tp]").forEach(el => { ES[el.dataset.tp] = el.placeholder; });
const T = k => (lang === "en" ? (window.TEXTS.en[k]) : ES[k]) || window.TEXTS.en[k] || k;
const TX = {   // texts only used from JS, Spanish
  connected: "Conectado", reading: "Leyendo", writing: "Escribiendo", done: "Hecho", all: "Todas las zonas",
  no_ble: "Este navegador no tiene acceso a Bluetooth. Usa Chrome en Android, la app de Android o el Toolkit de escritorio.",
  no_serial: "El cable USB necesita Chrome o Edge en un ordenador (Web Serial).",
  confirm_write: "¿Escribir la configuración en la radio? Antes se lee una copia de seguridad.",
  nothing: "Nada que escribir: la radio ya tiene esta configuración.", saved: "Guardado", channel: "Canal",
  loaded: "cargado", empty_cp: "Codeplug vacío (lee la radio o abre un fichero)",
  k_repeater: "Repetidores FM de radioaficionado", k_airport: "Aeropuertos (solo RX, AM)", k_simplex: "Símplex / APRS / ISS",
  k_pmr: "PMR446 (solo RX)", k_cb: "CB 27 MHz (solo RX)",
  help_html: "<p><b>1.</b> Pestaña Radio → <b>Bluetooth</b> (o cable USB) y elige tu RT-950.</p><p><b>2.</b> <b>Leer de la radio</b>: se carga la configuración y se guarda una copia (pestaña Ficheros).</p><p><b>3.</b> Edita canales, zonas y ajustes, o carga un <b>codeplug por país</b>.</p><p><b>4.</b> <b>Escribir en la radio</b>: se vuelve a leer la radio, solo se envían los bloques que cambian y se verifican.</p><p>En Android permite el acceso a Bluetooth («Dispositivos cercanos»). El firmware se actualiza con el Toolkit de escritorio o el flasheador web (cable USB).</p>",
};
const t = k => lang === "en" ? (window.TEXTS.en[k] || k) : (TX[k] || ES[k] || k);

/* ------------------------------------------------------------ bootstrap */
const fetchJSON = async p => (await fetch(p)).json();
R.setMeta(await fetchJSON("data/meta.json"));
LEGAL = await fetchJSON("data/legal.json");
for (const c of ["es", "gb"]) COUNTRIES[c] = await fetchJSON(`data/country-${c}.json`);
cp = (() => { const j = store.get("current", null); try { return j ? R.Codeplug.fromJSON(j) : new R.Codeplug(); } catch (e) { return new R.Codeplug(); } })();

function log(m) { const el = $("log"); el.textContent += m + "\n"; el.scrollTop = el.scrollHeight; }
function status(m, p) {
  $("statusText").textContent = m;
  const pr = $("prog");
  if (p == null) pr.hidden = true; else { pr.hidden = false; pr.value = p; }
}
function persist() { store.set("current", cp.toJSON()); }
const mhz = R.mhz;

function applyLang() {
  document.documentElement.lang = lang;
  document.querySelectorAll("[data-t]").forEach(el => { el.textContent = T(el.dataset.t); });
  document.querySelectorAll("[data-tp]").forEach(el => { el.placeholder = T(el.dataset.tp); });
  $("langbtn").textContent = lang === "es" ? "EN" : "ES";
  renderAll();
}
$("langbtn").onclick = () => { lang = lang === "es" ? "en" : "es"; store.set("lang", lang); applyLang(); };

document.querySelectorAll(".tabs button").forEach(b => b.onclick = () => {
  document.querySelectorAll(".tabs button").forEach(x => x.classList.toggle("on", x === b));
  document.querySelectorAll(".tab").forEach(s => s.classList.toggle("on", s.id === "tab-" + b.dataset.tab));
  window.scrollTo(0, 0);
});

/* ------------------------------------------------------------ disclaimer */
async function ensureDisclaimer() {
  if (store.get("disclaimer", 0) === LEGAL.version) return true;
  $("discText").textContent = LEGAL.disclaimer[lang];
  const dlg = $("discDlg");
  return new Promise(res => {
    $("discYes").onclick = () => { store.set("disclaimer", LEGAL.version); dlg.close(); res(true); };
    $("discNo").onclick = () => { dlg.close(); res(false); };
    dlg.showModal();
  });
}

/* --------------------------------------------------------------- radio */
function support() {
  const msgs = [];
  if (!R.NativeBleTransport.available() && !R.BleTransport.available()) msgs.push(t("no_ble"));
  if (!R.SerialTransport.available()) $("btnSerial").disabled = true;
  $("support").hidden = !msgs.length; $("support").textContent = msgs.join(" ");
}

async function connect(kind) {
  if (!(await ensureDisclaimer())) return;
  try {
    if (transport) await transport.close();
    transport = kind === "serial" ? new R.SerialTransport()
      : (R.NativeBleTransport.available() ? new R.NativeBleTransport() : new R.BleTransport());
    await transport.open();
    link = new R.OemLink(transport, log);
    status(`${t("connected")}: ${transport.name}`);
    log(`${t("connected")} (${transport.kind}): ${transport.name}`);
  } catch (e) { log("✗ " + e.message); status(T("disconnected")); transport = null; link = null; }
}
$("btnBle").onclick = () => connect("ble");
$("btnSerial").onclick = () => connect("serial");

async function session(fn) {
  if (!link) { await connect(R.NativeBleTransport.available() || R.BleTransport.available() ? "ble" : "serial"); if (!link) return; }
  const btns = ["btnRead", "btnWrite"].map($);
  btns.forEach(b => b.disabled = true);
  try {
    const model = await link.handshake();
    log("model: " + model);
    await fn();
    await link.end(true);
  } catch (e) { log("✗ " + e.message); status("✗ " + e.message); }
  finally { btns.forEach(b => b.disabled = false); }
}

function addBackup(c, tag) {
  const list = store.get("backups", []);
  const item = {when: new Date().toISOString().slice(0, 19).replace("T", " "), tag, data: c.toJSON()};
  list.unshift(item);
  while (list.length > 12 || (!store.set("backups", list) && list.length > 1)) list.pop();
  renderBackups();
}

$("btnRead").onclick = () => session(async () => {
  const c = await R.readCodeplug(link, (d, n, w) => status(`${t("reading")} ${w}`, 100 * d / n));
  cp = c; base = c; persist(); addBackup(c, "read");
  status(`${t("done")} — ${link.model}`); renderAll();
});

$("btnWrite").onclick = async () => {
  if (!(await ensureDisclaimer()) || !confirm(t("confirm_write"))) return;
  await session(async () => {
    const backup = await R.readCodeplug(link, (d, n, w) => status(`${t("reading")} ${w}`, 100 * d / n));
    addBackup(backup, "before-write");
    const written = await R.writeCodeplug(link, cp, {base: backup, progress: (d, n, w) => status(`${t("writing")} ${w}`, 100 * d / n)});
    base = backup;
    log(written.length ? "written: " + written.join(", ") : t("nothing"));
    status(t("done"));
  });
};

/* ------------------------------------------------------------- channels */
function zoneOptions(sel, withAll) {
  sel.innerHTML = (withAll ? `<option value="-1">${t("all")}</option>` : "") +
    Array.from({length: R.ZONES}, (_, z) => `<option value="${z}">${z + 1} · ${cp.zoneName(z) || "-"}</option>`).join("");
}
function renderChannels() {
  const z = +($("zoneSel").value || -1), q = $("search").value.trim().toLowerCase();
  const items = [];
  for (const c of cp.channels()) {
    if (c.empty || (z >= 0 && c.zone !== z)) continue;
    const text = `${c.name} ${mhz(c.rx)} ${c.txTone} ${c.rxAm} ${c.bandwidth}`.toLowerCase();
    if (q && !text.includes(q)) continue;
    const dup = c.txEnable === "OFF" ? "RX" : (c.tx !== c.rx ? (Math.abs(c.tx - c.rx) < 10e6 ? ((c.tx > c.rx ? "+" : "−") + (Math.abs(c.tx - c.rx) / 1e6).toFixed(3)) : "split") : "");
    items.push(`<li data-i="${c.index}"><span class="n">${c.number}</span><span class="f">${mhz(c.rx)} <small>${dup}</small></span>
      <span class="s">${c.rxAm}</span><span>${escapeHtml(c.name)}</span><span class="s">${c.txTone !== "OFF" ? c.txTone : ""}</span></li>`);
    if (items.length > 400) break;
  }
  $("chList").innerHTML = items.join("") || `<li><span></span><span class="muted">${t("empty_cp")}</span></li>`;
}
const escapeHtml = s => s.replace(/[&<>"]/g, c => ({"&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;"})[c]);
$("zoneSel").onchange = renderChannels;
$("search").oninput = renderChannels;
$("chList").onclick = e => { const li = e.target.closest("li[data-i]"); if (li) editChannel(+li.dataset.i); };
$("btnAdd").onclick = () => {
  const z = Math.max(0, +$("zoneSel").value);
  for (let i = z * R.PER_ZONE; i < (z + 1) * R.PER_ZONE; i++) if (cp.channel(i).empty) return editChannel(i, true);
  alert("Zone full");
};

function fillSelect(sel, opts) { sel.innerHTML = opts.map(o => `<option>${o}</option>`).join(""); }
function editChannel(i, isNew) {
  const c = cp.channel(i), f = $("chForm");
  $("chTitle").textContent = `${t("channel")} ${c.number} · ${T("t_zones").slice(0, -1)} ${c.zone + 1}`;
  const tones = window.__tones;
  fillSelect(f.rxTone, tones); fillSelect(f.txTone, tones);
  fillSelect(f.power, ["High", "Mid", "Low"]); fillSelect(f.bandwidth, ["Wide", "Narrow"]);
  fillSelect(f.pttId, ["OFF", "BOT", "EOT", "BOTH"]);
  f.name.value = c.name; f.rx.value = c.empty ? "" : mhz(c.rx); f.tx.value = c.empty ? "" : mhz(c.tx);
  for (const k of ["rxTone", "txTone", "power", "bandwidth", "rxAm", "txEnable", "scanAdd", "busyLock", "pttId"]) f[k].value = c[k];
  const dlg = $("chDlg");
  dlg.onclose = () => {
    if (dlg.returnValue === "delete") { cp.clearChannel(i); }
    else if (dlg.returnValue === "ok") {
      const rx = parseFloat(f.rx.value.replace(",", ".")), tx = parseFloat((f.tx.value || f.rx.value).replace(",", "."));
      if (!(rx >= 18 && rx < 1000)) { alert("RX?"); return; }
      const n = new R.Channel(i, Object.assign({}, c, {rx: Math.round(rx * 1e6), tx: Math.round(tx * 1e6), name: f.name.value}));
      for (const k of ["rxTone", "txTone", "power", "bandwidth", "rxAm", "txEnable", "scanAdd", "busyLock", "pttId"]) n[k] = f[k].value;
      try { cp.setChannel(n); } catch (e) { alert(e.message); return; }
    } else return;
    persist(); renderChannels();
  };
  dlg.showModal();
}

/* ---------------------------------------------------------------- zones */
function renderZones() {
  $("zoneEdit").innerHTML = Array.from({length: R.ZONES}, (_, z) =>
    `<label class="set"><span>${z + 1}</span><input data-z="${z}" maxlength="16" value="${escapeHtml(cp.zoneName(z))}"></label>`).join("");
  $("zoneEdit").querySelectorAll("input").forEach(inp => inp.onchange = () => {
    cp.setZoneName(+inp.dataset.z, inp.value); persist(); zoneOptions($("zoneSel"), true);
  });
}

/* -------------------------------------------------------------- country */
function renderCountry() {
  const sel = $("ctySel"), cur = sel.value || "es";
  sel.innerHTML = Object.entries(COUNTRIES).map(([k, d]) => `<option value="${k}">${d.title[lang]}</option>`).join("");
  sel.value = cur;
  const d = COUNTRIES[sel.value];
  $("ctyDesc").textContent = `${d.description[lang]} (${d.generated}: ${d.sources.join("; ")})`;
  if (!$("ctyKinds").children.length) {
    $("ctyKinds").innerHTML = R.KINDS.map(k => `<label><input type="checkbox" value="${k}" checked> <span data-k="${k}"></span></label>`).join("");
  }
  $("ctyKinds").querySelectorAll("[data-k]").forEach(s => s.textContent = t("k_" + s.dataset.k));
  if (!$("ctyLoc").value) $("ctyLoc").value = store.get("locator", "");
}
$("ctySel").onchange = renderCountry;
function ctyRun(target) {
  const kinds = [...$("ctyKinds").querySelectorAll("input:checked")].map(i => i.value);
  const loc = $("ctyLoc").value.trim() || null;
  if (loc) store.set("locator", loc);
  const rep = R.applyCountry(target, COUNTRIES[$("ctySel").value], {
    mode: document.querySelector("input[name=mode]:checked").value, locator: loc, kinds, renameZones: $("ctyRename").checked});
  return `${rep.country} (${rep.mode}): ${rep.zones.reduce((s, z) => s + z.added, 0)}\n` + rep.zones.map(z =>
    `${String(z.zone + 1).padStart(2)} ${z.name.padEnd(16)} +${z.added}` + (z.duplicates ? ` (${z.duplicates} =)` : "") +
    (z.dropped.length ? ` (${z.dropped.length} ✗)` : "")).join("\n");
}
$("btnCtyPrev").onclick = () => { try { $("ctyOut").textContent = ctyRun(R.Codeplug.fromJSON(cp.toJSON())); } catch (e) { $("ctyOut").textContent = "✗ " + e.message; } };
$("btnCtyApply").onclick = () => { try { $("ctyOut").textContent = ctyRun(cp); persist(); renderAll(); } catch (e) { $("ctyOut").textContent = "✗ " + e.message; } };

/* ------------------------------------------------------------- settings */
function renderSettings() {
  const M = window.__meta, groups = [...new Set(M.select.map(f => f.group).concat(M.text.map(f => f.group)))]
    .filter(g => !/^VFO|Zones/.test(g));
  const sel = $("grpSel"), cur = sel.value || groups[0];
  sel.innerHTML = groups.map(g => `<option>${g}</option>`).join(""); sel.value = cur;
  const q = $("setSearch").value.trim().toLowerCase(), g = sel.value, html = [];
  M.text.filter(f => (q ? f.title.toLowerCase().includes(q) : f.group === g) && f.group !== "Zones").forEach((f, i) =>
    html.push(`<label class="set"><span>${f.title}</span><input data-tx="${M.text.indexOf(f)}" maxlength="${f.length}" value="${escapeHtml(cp.textValue(f))}"></label>`));
  M.select.filter(f => (q ? f.title.toLowerCase().includes(q) : f.group === g) && !/^VFO/.test(f.group)).forEach(f => {
    const v = cp.fieldGet(f);
    const opts = f.options.map((o, i) => `<option value="${i}" ${i === v ? "selected" : ""}>${o}</option>`).join("") +
      (v >= f.options.length ? `<option selected value="${v}">(0x${v.toString(16)})</option>` : "");
    html.push(`<label class="set"><span>${f.title}</span><select data-sf="${M.select.indexOf(f)}">${opts}</select></label>`);
  });
  $("settings").innerHTML = html.join("") || "-";
  $("settings").querySelectorAll("[data-sf]").forEach(s => s.onchange = () => { cp.fieldSet(M.select[+s.dataset.sf], +s.value); persist(); });
  $("settings").querySelectorAll("[data-tx]").forEach(s => s.onchange = () => { cp.setTextValue(M.text[+s.dataset.tx], s.value); persist(); });
}
$("grpSel").onchange = renderSettings;
$("setSearch").oninput = renderSettings;

/* ---------------------------------------------------------------- files */
async function saveFile(name, text, mime) {
  const C = window.Capacitor;
  if (C && C.isNativePlatform && C.isNativePlatform()) {
    const FS = C.registerPlugin("Filesystem"), Share = C.registerPlugin("Share");
    const r = await FS.writeFile({path: name, data: text, directory: "CACHE", encoding: "utf8"});
    await Share.share({title: name, url: r.uri, dialogTitle: name});
    return;
  }
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([text], {type: mime}));
  a.download = name; document.body.appendChild(a); a.click(); a.remove();
}
const stamp = () => new Date().toISOString().slice(0, 16).replace(/[-:T]/g, "");
$("btnSave").onclick = () => saveFile(`rt950-${stamp()}.rt950`, JSON.stringify(cp.toJSON(), null, 1), "application/json");
$("btnChirp").onclick = () => saveFile(`rt950-chirp-${stamp()}.csv`, R.exportChirp(cp), "text/csv");
$("btnNew").onclick = () => { if (confirm("?")) { cp = new R.Codeplug(); persist(); renderAll(); } };
$("openFile").onchange = async e => {
  const f = e.target.files[0]; if (!f) return;
  try { cp = R.Codeplug.fromJSON(JSON.parse(await f.text())); persist(); renderAll(); log(`${f.name} ${t("loaded")}`); }
  catch (err) { alert(err.message); }
};
function renderBackups() {
  const list = store.get("backups", []);
  $("bkList").innerHTML = list.map((b, i) => `<li><span>${b.when} · ${b.tag}</span><button data-b="${i}" class="small">↺</button><button data-s="${i}" class="small">⤓</button></li>`).join("") || "<li>-</li>";
  $("bkList").querySelectorAll("[data-b]").forEach(btn => btn.onclick = () => {
    cp = R.Codeplug.fromJSON(list[+btn.dataset.b].data); persist(); renderAll(); log("backup " + list[+btn.dataset.b].when);
  });
  $("bkList").querySelectorAll("[data-s]").forEach(btn => btn.onclick = () => {
    const b = list[+btn.dataset.s]; saveFile(`rt950-backup-${b.when.replace(/[-: ]/g, "")}-${b.tag}.rt950`, JSON.stringify(b.data), "application/json");
  });
}

/* ----------------------------------------------------------------- help */
function renderHelp() {
  $("helpBody").innerHTML = t("help_html");
  $("backupBody").textContent = LEGAL.backup[lang];
  $("disclaimerBody").textContent = LEGAL.disclaimer[lang];
  $("about").innerHTML = `BricoHams RT-950 Programmer ${VERSION} · GPL-3.0 · BricoHams — ${lang === "es" ? "Radioafición · Hazlo tú mismo" : "Amateur radio · Do it yourself"}<br>
    ${lang === "es" ? "Protocolo Bluetooth documentado por" : "Bluetooth protocol documented by"} rt950-ble (bartasx, MIT). ${lang === "es" ? "Datos" : "Data"}: URE, ukrepeater.net, OurAirports.<br>
    <a href="https://github.com/anatolbricoham/rt950pro-satellite-mode">github.com/anatolbricoham/rt950pro-satellite-mode</a>`;
}

function renderAll() {
  zoneOptions($("zoneSel"), true);
  $("cpinfo").textContent = `${cp.meta.model || ""} · ${cp.meta.source || ""} ${cp.meta.read_at || ""} · ${cp.channels().filter(c => !c.empty).length} ch`;
  renderChannels(); renderZones(); renderCountry(); renderSettings(); renderBackups(); renderHelp();
}

window.__meta = await fetchJSON("data/meta.json");
window.__tones = window.__meta.tones;
lang = store.get("lang", (navigator.language || "es").startsWith("es") ? "es" : "en");
support();
applyLang();
ensureDisclaimer();
const nativeApp = !!(window.Capacitor && window.Capacitor.isNativePlatform && window.Capacitor.isNativePlatform());
if (!nativeApp && "serviceWorker" in navigator && location.protocol === "https:") navigator.serviceWorker.register("sw.js").catch(() => {});
})();
