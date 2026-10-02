/* i18n.js - minimal ES/EN switch for the BricoHams site. The Spanish text is
   the HTML itself; T.en holds the English strings keyed by data-i18n. */
window.BH = {
  lang: "es",
  init(T, onChange) {
    const saved = (() => { try { return localStorage.getItem("bh-lang"); } catch (e) { return null; } })();
    const nav = (navigator.language || "es").slice(0, 2);
    const original = {};
    document.querySelectorAll("[data-i18n]").forEach(el => { original[el.dataset.i18n] = el.innerHTML; });
    const apply = l => {
      BH.lang = l === "en" ? "en" : "es";
      document.documentElement.lang = BH.lang;
      document.querySelectorAll("[data-i18n]").forEach(el => {
        const k = el.dataset.i18n;
        el.innerHTML = BH.lang === "en" && T.en[k] ? T.en[k] : original[k];
      });
      document.querySelectorAll("[data-lang]").forEach(b => b.classList.toggle("on", b.dataset.lang === BH.lang));
      try { localStorage.setItem("bh-lang", BH.lang); } catch (e) {}
      if (onChange) onChange(BH.lang);
    };
    document.querySelectorAll("[data-lang]").forEach(b => b.onclick = () => apply(b.dataset.lang));
    apply(saved || (nav === "es" ? "es" : "en"));
  },
};
