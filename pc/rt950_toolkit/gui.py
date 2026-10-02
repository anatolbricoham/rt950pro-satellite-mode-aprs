"""
gui.py - BricoHams RT-950 Toolkit desktop application (Tkinter: Windows, Linux, macOS)

Tabs: Channels, Zones, VFO, Settings, APRS, Satellites, Boot logo, Log.
Radio operations run in a worker thread; the UI stays responsive.
"""

from __future__ import annotations

import json
import os
import queue
import sys
import threading
import time
import traceback
import webbrowser
from typing import List, Optional

import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog, ttk

from . import APP_NAME, __version__
from . import codeplug as C
from . import csvio, protocol, stock, bootlogo, datfile
from . import gui_extra as X
from .i18n import _, set_lang
from . import i18n

from . import APP_ID
CONF_DIR = os.path.join(os.environ.get("APPDATA") or os.path.join(os.path.expanduser("~"), ".config"), APP_ID)
CONF_FILE = os.path.join(CONF_DIR, "settings.json")


def load_conf() -> dict:
    try:
        with open(CONF_FILE, encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return {"lang": "es", "port": "", "locator": "IM98IB"}


def save_conf(conf: dict):
    try:
        os.makedirs(CONF_DIR, exist_ok=True)
        with open(CONF_FILE, "w", encoding="utf-8") as f:
            json.dump(conf, f, indent=1)
    except Exception:
        pass


class ScrollFrame(ttk.Frame):
    def __init__(self, parent):
        super().__init__(parent)
        self.canvas = tk.Canvas(self, highlightthickness=0)
        sb = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
        self.inner = ttk.Frame(self.canvas)
        self.inner.bind("<Configure>", lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all")))
        self.canvas.create_window((0, 0), window=self.inner, anchor="nw")
        self.canvas.configure(yscrollcommand=sb.set)
        self.canvas.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")


# =========================================================================
#  Channel editor dialog
# =========================================================================

class ChannelDialog(tk.Toplevel):
    def __init__(self, app, ch: C.Channel):
        super().__init__(app)
        self.app, self.ch, self.result = app, ch, None
        self.title("%s %d" % (_("channels"), ch.number))
        self.transient(app)
        f = ttk.Frame(self, padding=10)
        f.pack(fill="both", expand=True)
        tones = app.cp.tones
        self.v = {}
        rows = [
            ("name", "entry", ch.name),
            ("rx", "entry", csvio.mhz(ch.rx_hz) if ch.rx_hz else ""),
            ("tx", "entry", csvio.mhz(ch.tx_hz) if ch.tx_hz else ""),
            ("rx_tone", tones, ch.rx_tone), ("tx_tone", tones, ch.tx_tone),
            ("power", C.POWER, ch.power), ("bw", C.BANDWIDTH, ch.bandwidth),
            ("mod", ["FM", "AM"], ch.rx_am), ("tx_en", C.OFF_ON, ch.tx_enable),
            ("scan", C.OFF_ON, ch.scan_add), ("busy", C.OFF_ON, ch.busy_lock),
            ("scramble", C.SCRAMBLE, ch.scramble), ("encr", C.ENCRYPTION, ch.encryption),
            ("signal", [str(i) for i in range(1, 17)], str(ch.signal_code)),
            ("pttid", C.PTT_ID, ch.ptt_id),
        ]
        for r, (key, kind, val) in enumerate(rows):
            ttk.Label(f, text=_(key)).grid(row=r, column=0, sticky="e", padx=4, pady=2)
            var = tk.StringVar(value=val)
            if kind == "entry":
                w = ttk.Entry(f, textvariable=var, width=18)
            else:
                w = ttk.Combobox(f, textvariable=var, values=kind, state="readonly", width=16)
            w.grid(row=r, column=1, sticky="w", padx=4, pady=2)
            self.v[key] = var
        b = ttk.Frame(f)
        b.grid(row=len(rows), column=0, columnspan=2, pady=8)
        ttk.Button(b, text=_("ok"), command=self.ok).pack(side="left", padx=4)
        ttk.Button(b, text=_("cancel"), command=self.destroy).pack(side="left", padx=4)
        self.bind("<Return>", lambda e: self.ok())
        self.grab_set()

    def ok(self):
        try:
            rx = csvio.parse_mhz(self.v["rx"].get())
            tx = csvio.parse_mhz(self.v["tx"].get()) or rx
            if rx is not None and not (18e6 <= rx <= 1000e6):
                raise ValueError("RX 18-1000 MHz")
        except ValueError as e:
            messagebox.showerror(_("error"), str(e), parent=self)
            return
        g = lambda k: self.v[k].get()
        self.result = C.Channel(self.ch.index, rx, tx, g("rx_tone"), g("tx_tone"), g("power"), g("bw"),
                                g("scramble"), g("busy"), g("scan"), g("encr"), g("tx_en"), g("mod"),
                                int(g("signal")), g("pttid"), g("name")[:12])
        self.destroy()


# =========================================================================
#  Tabs
# =========================================================================

class ChannelsTab(ttk.Frame):
    COLS = [("number", 45), ("name", 110), ("rx", 90), ("tx", 90), ("duplex", 70), ("rx_tone", 70),
            ("tx_tone", 70), ("power", 55), ("bw", 65), ("mod", 50), ("scan", 50), ("tx_en", 70)]

    def __init__(self, parent, app):
        super().__init__(parent)
        self.app = app
        self.clip: List[bytes] = []
        top = ttk.Frame(self)
        top.pack(fill="x", pady=2)
        ttk.Label(top, text=_("zone")).pack(side="left", padx=4)
        self.zone = ttk.Combobox(top, state="readonly", width=22)
        self.zone.pack(side="left")
        self.zone.bind("<<ComboboxSelected>>", lambda e: self.refresh())
        self.empty = tk.BooleanVar(value=False)
        ttk.Checkbutton(top, text=_("show_empty"), variable=self.empty, command=self.refresh).pack(side="left", padx=8)
        for key, cmd in [("edit", self.edit), ("delete", self.delete), ("copy", self.copy), ("paste", self.paste),
                         ("up", lambda: self.move(-1)), ("down", lambda: self.move(1))]:
            ttk.Button(top, text=_(key), command=cmd).pack(side="left", padx=2)
        self.count = ttk.Label(top, text="")
        self.count.pack(side="right", padx=6)
        frm = ttk.Frame(self)
        frm.pack(fill="both", expand=True)
        self.tree = ttk.Treeview(frm, columns=[c for c, _w in self.COLS], show="headings", selectmode="extended")
        for c, w in self.COLS:
            self.tree.heading(c, text=_(c))
            self.tree.column(c, width=w, anchor="w", stretch=c == "name")
        sb = ttk.Scrollbar(frm, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=sb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")
        self.tree.bind("<Double-1>", lambda e: self.edit())
        self.tree.bind("<Delete>", lambda e: self.delete())

    def zone_values(self):
        cp = self.app.cp
        return [_("all")] + ["%d  %s" % (z + 1, cp.zone_name(z)) for z in range(C.ZONES)]

    def refresh(self):
        cp = self.app.cp
        cur = self.zone.current() if self.zone.get() else 0
        self.zone.configure(values=self.zone_values())
        self.zone.current(max(0, cur))
        z = self.zone.current() - 1
        self.tree.delete(*self.tree.get_children())
        used = 0
        for ch in cp.channels():
            if not ch.empty:
                used += 1
            if z >= 0 and ch.zone != z:
                continue
            if ch.empty and not self.empty.get():
                continue
            if ch.empty:
                vals = [ch.number, "", "", "", "", "", "", "", "", "", "", ""]
            else:
                dup, off = ch.duplex()
                dtxt = {"": "", "off": "TX OFF", "split": "split"}.get(dup, "%s%.3f" % (dup, off / 1e6))
                vals = [ch.number, ch.name, csvio.mhz(ch.rx_hz), csvio.mhz(ch.tx_hz), dtxt, ch.rx_tone, ch.tx_tone,
                        ch.power, ch.bandwidth, ch.rx_am, ch.scan_add, ch.tx_enable]
            self.tree.insert("", "end", iid=str(ch.index), values=vals)
        self.count.configure(text="%d / %d" % (used, C.CHANNELS))

    def selected(self) -> List[int]:
        return sorted(int(i) for i in self.tree.selection())

    def edit(self):
        sel = self.selected()
        if not sel:
            return
        d = ChannelDialog(self.app, self.app.cp.channel(sel[0]))
        self.wait_window(d)
        if d.result:
            if d.result.rx_hz is None:
                self.app.cp.clear_channel(d.result.index)
            else:
                self.app.cp.set_channel(d.result)
            self.app.changed()

    def delete(self):
        for i in self.selected():
            self.app.cp.clear_channel(i)
        self.app.changed()

    def copy(self):
        self.clip = [self.app.cp.read(i * 32, 32) for i in self.selected()]

    def paste(self):
        sel = self.selected()
        if not sel or not self.clip:
            return
        for k, rec in enumerate(self.clip):
            if sel[0] + k < C.CHANNELS:
                self.app.cp.write((sel[0] + k) * 32, rec)
        self.app.changed()

    def move(self, d):
        sel = self.selected()
        if len(sel) != 1 or not 0 <= sel[0] + d < C.CHANNELS:
            return
        a, b = sel[0], sel[0] + d
        ra, rb = self.app.cp.read(a * 32, 32), self.app.cp.read(b * 32, 32)
        self.app.cp.write(a * 32, rb)
        self.app.cp.write(b * 32, ra)
        self.app.changed()
        self.tree.selection_set(str(b))
        self.tree.see(str(b))


class ZonesTab(ttk.Frame):
    def __init__(self, parent, app):
        super().__init__(parent, padding=10)
        self.app = app
        self.vars = []
        for z in range(C.ZONES):
            ttk.Label(self, text="%s %d  (%d-%d)" % (_("zone"), z + 1, z * C.CH_PER_ZONE + 1, (z + 1) * C.CH_PER_ZONE)).grid(
                row=z, column=0, sticky="e", padx=4, pady=3)
            v = tk.StringVar()
            e = ttk.Entry(self, textvariable=v, width=16)
            e.grid(row=z, column=1, sticky="w")
            v.trace_add("write", lambda *a, z=z, v=v: self.set(z, v))
            self.vars.append(v)
        self.loading = False

    def set(self, z, v):
        if not self.loading:
            self.app.cp.set_zone_name(z, v.get()[:12])
            self.app.changed(refresh=False)

    def refresh(self):
        self.loading = True
        for z, v in enumerate(self.vars):
            v.set(self.app.cp.zone_name(z))
        self.loading = False


class FieldForm(ScrollFrame):
    """Combobox per select field + entry per text field."""

    def __init__(self, parent, app, fields, texts=(), nums=(), dtmf=(), readonly=False):
        super().__init__(parent)
        self.app, self.fields, self.texts = app, fields, list(texts)
        self.widgets = []
        r = 0
        for d in dtmf:
            ttk.Label(self.inner, text=d.title).grid(row=r, column=0, sticky="e", padx=6, pady=2)
            v = tk.StringVar()
            ttk.Entry(self.inner, textvariable=v, width=d.length + 4).grid(row=r, column=1, sticky="w")
            v.trace_add("write", lambda *a, d=d, v=v: self._set_dtmf(d, v))
            self.widgets.append(("d", d, v))
            r += 1
        for n in nums:
            unit = "MHz" if n.scale == 100 else ("" if n.signed else "kHz")
            ttk.Label(self.inner, text="%s %s" % (n.title, unit)).grid(row=r, column=0, sticky="e", padx=6, pady=2)
            v = tk.StringVar()
            e = ttk.Entry(self.inner, textvariable=v, width=10)
            e.grid(row=r, column=1, sticky="w")
            e.bind("<FocusOut>", lambda ev, n=n, v=v: self._set_num(n, v))
            e.bind("<Return>", lambda ev, n=n, v=v: self._set_num(n, v))
            self.widgets.append(("n", n, v))
            r += 1
        for t in self.texts:
            ttk.Label(self.inner, text=t.title).grid(row=r, column=0, sticky="e", padx=6, pady=2)
            v = tk.StringVar()
            ttk.Entry(self.inner, textvariable=v, width=max(8, min(42, t.length + 2))).grid(row=r, column=1, sticky="w")
            v.trace_add("write", lambda *a, t=t, v=v: self._set_text(t, v))
            self.widgets.append(("t", t, v))
            r += 1
        for f in fields:
            ttk.Label(self.inner, text=f.title).grid(row=r, column=0, sticky="e", padx=6, pady=2)
            cb = ttk.Combobox(self.inner, values=f.options, state="disabled" if readonly else "readonly", width=34)
            cb.grid(row=r, column=1, sticky="w", pady=1)
            cb.bind("<<ComboboxSelected>>", lambda e, f=f, cb=cb: self._set(f, cb))
            self.widgets.append(("f", f, cb))
            r += 1
        self.loading = False

    def _set(self, f, cb):
        f.set(self.app.cp, cb.current())
        self.app.changed(refresh=False)

    def _set_text(self, t, v):
        if not self.loading:
            self.app.cp.set_text_value(t, v.get())
            self.app.changed(refresh=False)

    def _set_dtmf(self, d, v):
        if not self.loading:
            try:
                self.app.cp.set_dtmf_value(d, v.get())
                self.app.changed(refresh=False)
            except ValueError as e:
                self.app.log(str(e))

    def _set_num(self, n, v):
        if self.loading:
            return
        try:
            txt = v.get().strip().replace(",", ".")
            self.app.cp.set_num_value(n, float(txt) if txt else None)
            self.app.changed(refresh=False)
        except ValueError as e:
            messagebox.showerror(_("error"), str(e))

    def refresh(self):
        self.loading = True
        for kind, f, w in self.widgets:
            if kind == "f":
                val = f.get(self.app.cp)
                if 0 <= val < len(f.options):
                    w.current(val)
                else:
                    w.set("(0x%02X)" % val)
            elif kind == "t":
                w.set(self.app.cp.text_value(f))
            elif kind == "d":
                w.set(self.app.cp.dtmf_value(f))
            elif kind == "n":
                val = self.app.cp.num_value(f)
                w.set("" if val is None else (("%.2f" % val) if f.scale == 100 else "%d" % val))
        self.loading = False


class VfoTab(ttk.Frame):
    def __init__(self, parent, app):
        super().__init__(parent, padding=6)
        self.app = app
        nb = ttk.Notebook(self)
        nb.pack(fill="both", expand=True)
        self.parts = []
        fields = C.select_fields()
        for v in range(3):
            fr = ttk.Frame(nb, padding=6)
            nb.add(fr, text="VFO " + "ABC"[v])
            top = ttk.Frame(fr)
            top.pack(fill="x")
            fv, ov = tk.StringVar(), tk.StringVar()
            ttk.Label(top, text=_("rx")).grid(row=0, column=0, sticky="e", padx=4)
            ttk.Entry(top, textvariable=fv, width=14).grid(row=0, column=1, sticky="w")
            ttk.Label(top, text="Offset MHz").grid(row=1, column=0, sticky="e", padx=4)
            ttk.Entry(top, textvariable=ov, width=14).grid(row=1, column=1, sticky="w")
            ttk.Button(top, text=_("apply"), command=lambda v=v, fv=fv, ov=ov: self.apply(v, fv, ov)).grid(row=0, column=2, rowspan=2, padx=8)
            # The maker's model file and the verified editor disagree on VFO
            # bytes 26-28: only frequency and offset are editable here.
            ttk.Label(fr, text=_("vfo_ro"), foreground="#805000").pack(anchor="w", pady=(6, 0))
            form = FieldForm(fr, app, [f for f in fields if f.group == "VFO " + "ABC"[v]], readonly=True)
            form.pack(fill="both", expand=True, pady=6)
            self.parts.append((fv, ov, form))

    def apply(self, v, fv, ov):
        try:
            self.app.cp.set_vfo_freq(v, csvio.parse_mhz(fv.get()))
            if ov.get().strip():
                self.app.cp.set_vfo_offset(v, csvio.parse_mhz(ov.get()))
            self.app.changed(refresh=False)
        except Exception as e:
            messagebox.showerror(_("error"), str(e))

    def refresh(self):
        for v, (fv, ov, form) in enumerate(self.parts):
            f = self.app.cp.vfo_freq(v)
            o = self.app.cp.vfo_offset(v)
            fv.set(csvio.mhz(f) if f else "")
            ov.set(csvio.mhz(o) if o is not None else "")
            form.refresh()


class SettingsTab(ttk.Frame):
    def __init__(self, parent, app):
        super().__init__(parent, padding=4)
        nb = ttk.Notebook(self)
        nb.pack(fill="both", expand=True)
        fields = C.select_fields()
        texts = C.text_fields()
        nums = C.numeric_fields()
        dtmf = C.dtmf_fields()
        self.forms = []
        for g in ["General", "Keys", "DTMF", "FM/AM/SSB"]:
            fr = FieldForm(nb, app, [f for f in fields if f.group == g], [t for t in texts if t.group == g],
                           [n for n in nums if n.group == g], dtmf if g == "DTMF" else ())
            nb.add(fr, text=g)
            self.forms.append(fr)

    def refresh(self):
        for f in self.forms:
            f.refresh()


class AprsTab(ttk.Frame):
    def __init__(self, parent, app):
        super().__init__(parent, padding=6)
        self.app = app
        ttk.Label(self, text=_("aprs_msg_note"), wraplength=700, foreground="#2266aa").pack(fill="x", pady=4)
        texts = [t for t in C.text_fields() if t.group == "APRS"]
        nums = [n for n in C.numeric_fields() if n.group == "APRS"]
        self.form = FieldForm(self, app, [f for f in C.select_fields() if f.group == "APRS"], texts, nums)
        self.form.pack(fill="both", expand=True)

    def refresh(self):
        self.form.refresh()


class SatTab(ttk.Frame):
    def __init__(self, parent, app):
        super().__init__(parent, padding=6)
        from . import sat
        self.sat, self.app = sat, app
        self.res = None
        top = ttk.Frame(self)
        top.pack(fill="x")
        self.loc = tk.StringVar(value=app.conf.get("locator", "IM98IB"))
        self.hours = tk.StringVar(value="48")
        self.minel = tk.StringVar(value="0")
        self.startch = tk.StringVar(value="900")
        for lbl, var, w in [("locator", self.loc, 8), ("hours", self.hours, 4), ("min_el", self.minel, 4)]:
            ttk.Label(top, text=_(lbl)).pack(side="left", padx=3)
            ttk.Entry(top, textvariable=var, width=w).pack(side="left")
        ttk.Button(top, text=_("update_tle"), command=self.update_tle).pack(side="left", padx=4)
        ttk.Button(top, text=_("calc_passes"), command=self.calc).pack(side="left", padx=2)
        ttk.Button(top, text=_("open_report"), command=self.report).pack(side="left", padx=2)
        bot = ttk.Frame(self)
        bot.pack(fill="x", pady=4)
        ttk.Label(bot, text=_("add_doppler")).pack(side="left", padx=3)
        ttk.Entry(bot, textvariable=self.startch, width=5).pack(side="left")
        ttk.Button(bot, text=_("add"), command=self.add_channels).pack(side="left", padx=3)
        ttk.Button(bot, text=_("upload_satdb"), command=self.upload).pack(side="left", padx=10)
        ttk.Button(bot, text=_("set_clock"), command=self.set_clock).pack(side="left", padx=2)
        cols = ("sat", "date", "aos", "los", "max", "az", "rx", "tx")
        self.tree = ttk.Treeview(self, columns=cols, show="headings")
        for c, w in zip(cols, (90, 90, 70, 70, 45, 80, 90, 90)):
            self.tree.heading(c, text=c.upper())
            self.tree.column(c, width=w)
        self.tree.pack(fill="both", expand=True)
        self.out = os.path.join(CONF_DIR, "satellites")

    def _args(self):
        from types import SimpleNamespace
        self.app.conf["locator"] = self.loc.get().strip()
        save_conf(self.app.conf)
        return SimpleNamespace(locator=self.loc.get().strip(), lat=None, lon=None, alt=0.0,
                               hours=float(self.hours.get() or 48), min_el=float(self.minel.get() or 0),
                               tz=self.app.conf.get("tz", "Europe/Madrid"),
                               freqs=self.sat.DEFAULT_FREQS, tle_file=None, offline=False, out=self.out,
                               start_channel=int(self.startch.get() or 900), db_hours=72.0, ics_min_el=15.0)

    def update_tle(self):
        def job():
            os.makedirs(self.out, exist_ok=True)
            n = self.sat.fetch_tles(os.path.join(self.out, "tle_cache.txt"), log=self.app.log)
            self.app.log("TLE: %d" % len(n))
        self.app.run_bg(job)

    def calc(self):
        def job():
            a = self._args()
            self.res = self.sat.run_pipeline(a, log=self.app.log)
            self.app.after(0, self.show)
        self.app.run_bg(job)

    def show(self):
        tz = self.sat.tzinfo_for(self.app.conf.get("tz", "Europe/Madrid"))
        self.tree.delete(*self.tree.get_children())
        for p in self.res["passes"]:
            e = p.sat
            self.tree.insert("", "end", values=(e.name, self.sat.fmt_t(p.aos, tz, "%d/%m"), self.sat.fmt_t(p.aos, tz, "%H:%M:%S"),
                                                self.sat.fmt_t(p.los, tz, "%H:%M:%S"), "%.0f" % p.max_el,
                                                "%03.0f>%03.0f" % (p.aos_az, p.los_az), "%.3f" % e.downlink_mhz,
                                                ("%.3f" % e.uplink_mhz) if e.can_tx else _("rx_only_sat")))

    def report(self):
        p = os.path.join(self.out, "pases_satelites.html")
        if os.path.exists(p):
            webbrowser.open("file://" + p)

    def add_channels(self):
        if not self.res:
            messagebox.showinfo(APP_NAME, _("calc_passes"))
            return
        start = int(self.startch.get() or 900) - 1
        n = 0
        idx = start
        for e in self.res["entries"]:
            if not e.enabled or e.mode in ("LIN_INV", "LIN") or not e.downlink_mhz:
                continue
            fmt = lambda t: "%.1f" % t if t else "OFF"
            steps = self.sat.doppler_steps(e)
            extra = [("ARM", 0, 0, e.arm_tone)] if (e.arm_tone and e.can_tx) else []
            for lab, rxo, txo, *arm in [(a, b, c) for a, b, c in steps] + extra:
                if idx >= C.CHANNELS:
                    break
                tone = arm[0] if arm else e.ctcss_up
                ch = C.Channel(idx, int(round(e.downlink_mhz * 1e6)) + rxo,
                               int(round(e.uplink_mhz * 1e6)) + txo if e.can_tx else int(round(e.downlink_mhz * 1e6)) + rxo,
                               fmt(e.ctcss_down), fmt(tone) if e.can_tx else "OFF", "High", "Wide", "OFF", "OFF",
                               "OFF", "OFF", "ON" if e.can_tx else "OFF", "FM", 1, "OFF", ("%s %s" % (e.label, lab))[:12])
                self.app.cp.set_channel(ch)
                idx += 1
                n += 1
        self.app.changed()
        self.app.log("%d satellite channels written from %d" % (n, start + 1))

    def upload(self):
        port = self.app.port()
        if not port:
            return
        def job():
            if not self.res:
                self.res = self.sat.run_pipeline(self._args(), log=self.app.log)
            self.sat.upload_db(port, self.res["blob"], log=self.app.log)
        self.app.run_bg(job)

    def set_clock(self):
        port = self.app.port()
        if not port:
            return
        def job():
            link = self.sat.RadioLink(port, log=self.app.log)
            try:
                link.handshake()
                link.set_time()
                link.finish()
                self.app.log("clock set")
            finally:
                link.close()
        self.app.run_bg(job)

    def refresh(self):
        pass


class LogoTab(ttk.Frame):
    def __init__(self, parent, app):
        super().__init__(parent, padding=6)
        self.app, self.px = app, None
        b = ttk.Frame(self)
        b.pack(fill="x")
        ttk.Button(b, text=_("load_image"), command=self.load).pack(side="left", padx=2)
        ttk.Button(b, text="BricoHams", command=lambda: self.load(os.path.join(X.BRAND, "bootlogo_240x320.png"))).pack(side="left", padx=2)
        ttk.Button(b, text=_("export_bmp"), command=self.export).pack(side="left", padx=2)
        ttk.Button(b, text=_("upload_logo"), command=self.upload).pack(side="left", padx=2)
        self.canvas = tk.Canvas(self, width=bootlogo.W, height=bootlogo.H, bg="black")
        self.canvas.pack(pady=8)
        self.img = None

    def load(self, p=None):
        p = p or filedialog.askopenfilename(filetypes=[("Images", "*.png *.gif *.ppm *.jpg *.jpeg *.bmp"), ("*", "*")])
        if not p:
            return
        self.px = bootlogo.to_rgb(p)
        self.img = tk.PhotoImage(width=bootlogo.W, height=bootlogo.H)
        rows = []
        for y in range(bootlogo.H):
            rows.append("{" + " ".join("#%02x%02x%02x" % self.px[y * bootlogo.W + x] for x in range(bootlogo.W)) + "}")
        self.img.put(" ".join(rows))
        self.canvas.create_image(0, 0, image=self.img, anchor="nw")

    def export(self):
        if not self.px:
            return
        p = filedialog.asksaveasfilename(defaultextension=".bmp", filetypes=[("BMP", "*.bmp")])
        if p:
            bootlogo.write_bmp(p, self.px)
            self.app.log("BMP: " + p)

    def upload(self):
        port = self.app.port()
        if not port or not self.px:
            return
        def job():
            ser = protocol.open_serial(port)
            try:
                link = protocol.detect(ser, self.app.log)
                if link.kind != "custom":
                    raise IOError("stock firmware: export the BMP and use Tools > Boot Picture in RT-950/950Pro Editor")
                bootlogo.upload_custom(link, self.px, self.app.progress)
                link.end(True)
            finally:
                ser.close()
        self.app.run_bg(job)

    def refresh(self):
        pass


# =========================================================================
#  Main window
# =========================================================================

class App(tk.Tk):
    def __init__(self, start_tab: Optional[str] = None):
        super().__init__()
        self.conf = load_conf()
        set_lang(self.conf.get("lang", "es"))
        self.cp = C.Codeplug()
        self.path: Optional[str] = None
        self.modified = False
        self.q = queue.Queue()
        self.geometry("1140x760")
        self.minsize(940, 600)
        X.set_window_icon(self)
        X.apply_style(self)
        self.protocol("WM_DELETE_WINDOW", self.quit_app)
        self.build(start_tab)
        self.after(100, self.pump)
        if not os.environ.get("BH_SKIP_DISCLAIMER"):
            self.after(300, self._first_run)

    def _first_run(self):
        if not X.ensure_disclaimer(self):
            self.destroy()

    # ---------------------------------------------------------------- build
    def build(self, start_tab=None):
        for w in self.winfo_children():
            w.destroy()
        self.title_update()
        m = tk.Menu(self)
        fm = tk.Menu(m, tearoff=0)
        fm.add_command(label=_("new"), command=self.new)
        fm.add_command(label=_("open"), command=self.open, accelerator="Ctrl+O")
        fm.add_command(label=_("save"), command=self.save, accelerator="Ctrl+S")
        fm.add_command(label=_("save_as"), command=lambda: self.save(True))
        fm.add_command(label=_("open_dat"), command=self.open_dat)
        fm.add_command(label=_("save_dat"), command=self.save_dat)
        fm.add_separator()
        fm.add_command(label=_("import_csv"), command=self.import_csv)
        fm.add_command(label=_("import_editor"), command=self.import_editor)
        fm.add_command(label=_("export_editor"), command=self.export_editor)
        fm.add_command(label=_("import_zones"), command=lambda: self.csv_io(csvio.import_zones, True))
        fm.add_command(label=_("export_zones"), command=lambda: self.csv_io(csvio.export_zones, False))
        fm.add_command(label=_("import_mod"), command=lambda: self.csv_io(csvio.import_modulation, True))
        fm.add_command(label=_("export_mod"), command=lambda: self.csv_io(csvio.export_modulation, False))
        fm.add_command(label=_("import_chirp"), command=self.import_chirp)
        fm.add_command(label=_("export_csv"), command=lambda: self.export_csv(False))
        fm.add_command(label=_("export_chirp"), command=lambda: self.export_csv(True))
        fm.add_separator()
        fm.add_command(label=_("import_raw"), command=self.import_raw)
        fm.add_command(label=_("export_raw"), command=self.export_raw)
        fm.add_separator()
        fm.add_command(label=_("exit"), command=self.quit_app)
        m.add_cascade(label=_("file"), menu=fm)
        rm = tk.Menu(m, tearoff=0)
        rm.add_command(label=_("read_radio"), command=self.read_radio)
        rm.add_command(label=_("write_radio"), command=self.write_radio)
        rm.add_command(label=_("write_channels"), command=lambda: self.write_radio(["channels", "zones"]))
        rm.add_separator()
        rm.add_command(label=_("restore_backup"), command=self.restore_backup)
        rm.add_command(label=_("backups"), command=lambda: self.open_folder(protocol.backup_dir()))
        m.add_cascade(label=_("radio"), menu=rm)
        tm = tk.Menu(m, tearoff=0)
        sm = tk.Menu(tm, tearoff=0)
        for name in stock.STOCK:
            sm.add_command(label=name, command=lambda n=name: self.add_stock(n))
        tm.add_command(label=_("country"), command=lambda: X.CountryDialog(self))
        tm.add_cascade(label=_("stock"), menu=sm)
        m.add_cascade(label=_("tools"), menu=tm)
        hm = tk.Menu(m, tearoff=0)
        lm = tk.Menu(hm, tearoff=0)
        lm.add_command(label="Español", command=lambda: self.lang("es"))
        lm.add_command(label="English", command=lambda: self.lang("en"))
        hm.add_command(label=_("help_manual"), command=X.open_help, accelerator="F1")
        hm.add_command(label=_("help_backup"), command=lambda: X.show_backup_guide(self))
        hm.add_command(label=_("help_disclaimer"), command=lambda: X.show_disclaimer(self))
        hm.add_separator()
        hm.add_command(label=_("help_web"), command=lambda: webbrowser.open(X.HOMEPAGE))
        hm.add_command(label=_("help_online"), command=lambda: webbrowser.open(X.REPO_URL))
        hm.add_separator()
        hm.add_cascade(label=_("language"), menu=lm)
        hm.add_command(label=_("about"), command=self.about)
        m.add_cascade(label=_("help"), menu=hm)
        self.config(menu=m)
        self.bind_all("<Control-s>", lambda e: self.save())
        self.bind_all("<Control-o>", lambda e: self.open())
        self.bind_all("<F1>", lambda e: X.open_help())

        X.BrandHeader(self).pack(fill="x")
        bar = ttk.Frame(self, padding=4)
        bar.pack(fill="x")
        ttk.Label(bar, text=_("port")).pack(side="left")
        self.portv = tk.StringVar(value=self.conf.get("port", ""))
        self.portcb = ttk.Combobox(bar, textvariable=self.portv, width=28)
        self.portcb.pack(side="left", padx=4)
        ttk.Button(bar, text=_("refresh"), command=self.scan_ports).pack(side="left")
        ttk.Button(bar, text=_("read_radio"), command=self.read_radio).pack(side="left", padx=8)
        ttk.Button(bar, text=_("write_radio"), command=self.write_radio).pack(side="left")
        self.model = ttk.Label(bar, text="")
        self.model.pack(side="right")
        self.scan_ports()

        self.nb = ttk.Notebook(self)
        self.nb.pack(fill="both", expand=True)
        self.tabs = {}
        for key, cls in [("channels", ChannelsTab), ("zones", ZonesTab), ("vfo", VfoTab), ("settings", SettingsTab),
                         ("aprs", AprsTab), ("satellites", SatTab), ("firmware", X.FirmwareTab),
                         ("bootlogo", LogoTab)]:
            t = cls(self.nb, self)
            self.nb.add(t, text=_(key))
            self.tabs[key] = t
        logf = ttk.Frame(self.nb)
        self.logtxt = tk.Text(logf, height=10)
        self.logtxt.pack(fill="both", expand=True)
        self.nb.add(logf, text=_("log"))
        st = ttk.Frame(self, padding=2)
        st.pack(fill="x")
        self.status = ttk.Label(st, text="")
        self.status.pack(side="left")
        self.pb = ttk.Progressbar(st, length=260, mode="determinate")
        self.pb.pack(side="right")
        self.refresh()
        if start_tab in self.tabs:
            self.nb.select(self.tabs[start_tab])

    def lang(self, l):
        self.conf["lang"] = l
        save_conf(self.conf)
        set_lang(l)
        self.build()

    def title_update(self):
        name = os.path.basename(self.path) if self.path else "-"
        self.title("%s %s  [%s]%s" % (APP_NAME, __version__, name, " *" if self.modified else ""))

    # --------------------------------------------------------------- helpers
    def log(self, msg):
        self.q.put(("log", str(msg)))

    def progress(self, done, total, what=""):
        self.q.put(("prog", (done, total, what)))

    def pump(self):
        try:
            while True:
                kind, val = self.q.get_nowait()
                if kind == "log":
                    self.logtxt.insert("end", time.strftime("%H:%M:%S ") + val + "\n")
                    self.logtxt.see("end")
                    self.status.configure(text=val[:120])
                elif kind == "prog":
                    d, t, w = val
                    self.pb.configure(maximum=max(1, t), value=d)
                    self.status.configure(text="%s %d%%" % (w, d * 100 // max(1, t)))
                elif kind == "call":
                    val()
        except queue.Empty:
            pass
        self.after(80, self.pump)

    def run_bg(self, fn, done=None):
        def wrap():
            try:
                fn()
                self.log(_("done"))
                if done:
                    self.q.put(("call", done))
            except Exception as e:
                self.log("ERROR: %s" % e)
                self.log(traceback.format_exc())
                self.q.put(("call", lambda: messagebox.showerror(_("error"), str(e))))
        threading.Thread(target=wrap, daemon=True).start()

    def scan_ports(self):
        ports = protocol.list_ports()
        self.portcb.configure(values=[p for p, d in ports])

    def port(self) -> Optional[str]:
        p = self.portv.get().strip()
        if not p:
            messagebox.showwarning(APP_NAME, _("no_port"))
            return None
        self.conf["port"] = p
        save_conf(self.conf)
        return p

    def changed(self, refresh=True):
        self.modified = True
        self.title_update()
        if refresh:
            self.tabs["channels"].refresh()

    def refresh(self):
        for t in self.tabs.values():
            t.refresh()
        self.model.configure(text=self.cp.meta.get("model", ""))
        self.title_update()

    def confirm_discard(self) -> bool:
        return not self.modified or messagebox.askyesno(APP_NAME, _("unsaved"))

    def open_folder(self, path):
        if sys.platform.startswith("win"):
            os.startfile(path)  # type: ignore[attr-defined]
        else:
            webbrowser.open("file://" + path)

    # ----------------------------------------------------------------- file
    def new(self):
        if self.confirm_discard():
            self.cp, self.path, self.modified = C.Codeplug(), None, False
            self.refresh()

    def open(self):
        if not self.confirm_discard():
            return
        p = filedialog.askopenfilename(filetypes=[("RT-950 Toolkit", "*.rt950"), ("*", "*")])
        if p:
            self.cp, self.path, self.modified = C.Codeplug.load(p), p, False
            self.refresh()

    def save(self, as_=False):
        if as_ or not self.path:
            p = filedialog.asksaveasfilename(defaultextension=".rt950", filetypes=[("RT-950 Toolkit", "*.rt950")])
            if not p:
                return
            self.path = p
        self.cp.save(self.path)
        self.modified = False
        self.title_update()
        self.log("saved " + self.path)

    def open_dat(self):
        if not self.confirm_discard():
            return
        p = filedialog.askopenfilename(filetypes=[("CPS .dat", "*.dat"), ("*", "*")])
        if p:
            self.cp, self.path, self.modified = datfile.open_dat(p), None, False
            self.dat_template = p
            self.refresh()
            self.log("opened " + p)

    def save_dat(self):
        tpl = getattr(self, "dat_template", None) or filedialog.askopenfilename(
            title=_("dat_template"), filetypes=[("CPS .dat", "*.dat")])
        if not tpl:
            return
        p = filedialog.asksaveasfilename(defaultextension=".dat", filetypes=[("CPS .dat", "*.dat")])
        if p:
            datfile.save_dat(self.cp, tpl, p)
            self.log("saved " + p)

    def import_editor(self):
        p = filedialog.askopenfilename(filetypes=[("CSV", "*.csv")])
        if p:
            n = csvio.import_editor(self.cp, p)
            self.log("%d channels imported" % n)
            self.changed()

    def export_editor(self):
        p = filedialog.asksaveasfilename(defaultextension=".csv", filetypes=[("CSV", "*.csv")])
        if p:
            csvio.export_editor(self.cp, p)
            self.log("exported " + p)

    def csv_io(self, fn, importing):
        if importing:
            p = filedialog.askopenfilename(filetypes=[("CSV", "*.csv")])
        else:
            p = filedialog.asksaveasfilename(defaultextension=".csv", filetypes=[("CSV", "*.csv")])
        if not p:
            return
        try:
            r = fn(self.cp, p)
        except Exception as e:
            messagebox.showerror(_("error"), str(e))
            return
        self.log(("%s: %s" % (p, r)) if importing else ("exported " + p))
        if importing:
            self.changed()
            self.refresh()

    def import_csv(self):
        p = filedialog.askopenfilename(filetypes=[("CSV", "*.csv")])
        if p:
            n = csvio.import_native(self.cp, p)
            self.log("%d channels imported" % n)
            self.changed()

    def import_chirp(self):
        p = filedialog.askopenfilename(filetypes=[("CHIRP CSV", "*.csv")])
        if not p:
            return
        start = simpledialog.askinteger(APP_NAME, "1-990 (0 = CHIRP 'Location')", initialvalue=0, minvalue=0, maxvalue=990)
        if start is None:
            return
        w = csvio.import_chirp(self.cp, p, start=None if start == 0 else start - 1)
        self.log("%d channels imported" % len(w))
        self.changed()

    def export_csv(self, chirp):
        p = filedialog.asksaveasfilename(defaultextension=".csv", filetypes=[("CSV", "*.csv")])
        if p:
            (csvio.export_chirp if chirp else csvio.export_native)(self.cp, p)
            self.log("exported " + p)

    def import_raw(self):
        p = filedialog.askopenfilename()
        if p:
            self.cp, self.modified = C.Codeplug.import_raw(p), True
            self.refresh()

    def export_raw(self):
        p = filedialog.asksaveasfilename(defaultextension=".bin")
        if p:
            self.cp.export_raw(p)

    def add_stock(self, name):
        chans = stock.STOCK[name]()
        start = simpledialog.askinteger(APP_NAME, "1-990", initialvalue=(self.cp.first_free() or 0) + 1, minvalue=1, maxvalue=990)
        if not start:
            return
        idx = start - 1
        for ch in chans:
            if idx >= C.CHANNELS:
                break
            ch.index = idx
            self.cp.set_channel(ch)
            idx += 1
        self.changed()

    # ---------------------------------------------------------------- radio
    def read_radio(self):
        port = self.port()
        if not port or not self.confirm_discard():
            return
        res = {}

        def job():
            ser = protocol.open_serial(port)
            try:
                link = protocol.detect(ser, self.log)
                self.log("%s (%s)" % (link.model, link.kind))
                res["cp"] = protocol.read_codeplug(link, self.progress)
                link.end(False)
            finally:
                ser.close()
            self.log("backup: " + protocol.save_backup(res["cp"], "read"))

        def done():
            self.cp, self.path, self.modified = res["cp"], None, False
            self.refresh()
        self.run_bg(job, done)

    def write_radio(self, regions=None):
        port = self.port()
        if not port or not X.ensure_disclaimer(self) or not messagebox.askyesno(APP_NAME, _("write_confirm")):
            return
        cp = self.cp

        def job():
            ser = protocol.open_serial(port)
            try:
                link = protocol.detect(ser, self.log)
                self.log("%s (%s): backup..." % (link.model, link.kind))
                backup = protocol.read_codeplug(link, self.progress)
                self.log("backup: " + protocol.save_backup(backup, "before-write"))
                written = protocol.write_codeplug(link, cp, regions, verify=True, progress=self.progress, base=backup)
                self.log("written: " + ", ".join(written))
                link.end(True)
            finally:
                ser.close()
        self.run_bg(job)

    def restore_backup(self):
        p = filedialog.askopenfilename(initialdir=protocol.backup_dir(), filetypes=[("RT-950 Toolkit", "*.rt950")])
        if p:
            self.cp, self.path, self.modified = C.Codeplug.load(p), None, True
            self.refresh()
            self.write_radio()

    def about(self):
        X.show_about(self)

    def quit_app(self):
        if self.confirm_discard():
            self.destroy()


def run_gui(start_tab: Optional[str] = None):
    App(start_tab).mainloop()


if __name__ == "__main__":
    run_gui()
