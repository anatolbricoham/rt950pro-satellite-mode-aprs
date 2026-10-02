/* sw.js - offline cache for the BricoHams RT-950 web programmer */
const CACHE = "bh-rt950-0.3.0";
const FILES = ["./", "index.html", "css/app.css", "js/core.js", "js/transport.js", "js/texts.js", "js/app.js",
  "data/meta.json", "data/legal.json", "data/country-es.json", "data/country-gb.json",
  "img/bricohams_logo.svg", "img/icon_64.png", "img/icon_256.png", "manifest.webmanifest"];
self.addEventListener("install", e => e.waitUntil(caches.open(CACHE).then(c => c.addAll(FILES))));
self.addEventListener("activate", e => e.waitUntil(caches.keys().then(ks => Promise.all(ks.filter(k => k !== CACHE).map(k => caches.delete(k))))));
self.addEventListener("fetch", e => e.respondWith(caches.match(e.request).then(r => r || fetch(e.request))));
