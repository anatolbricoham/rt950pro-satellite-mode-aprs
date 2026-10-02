"""
gui_extra.py - BricoHams branding, help, disclaimer, country codeplug dialog
and firmware update tab for the desktop application.
"""

from __future__ import annotations

import os
import sys
import threading
import webbrowser
from typing import Optional

import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from . import APP_NAME, HOMEPAGE, ORG, REPO_URL, TAGLINE, __version__
from . import country, firmware, flasher, legal, protocol
from .i18n import _
from . import i18n

HERE = os.path.dirname(os.path.abspath(__file__))
BRAND = os.path.join(HERE, "data", "brand")
HELP = os.path.join(HERE, "help")
DARK, ORANGE, LIGHT = "#23272B", "#F26B1D", "#FFFFFF"


def lang() -> str:
    return i18n.LANG


def image(name: str, master=None) -> Optional[tk.PhotoImage]:
    try:
        return tk.PhotoImage(master=master, file=os.path.join(BRAND, name))
    except Exception:
        return None


def set_window_icon(win):
    img = image("icon_64.png", win)
    if img is not None:
        win.iconphoto(True, img)
        win._bh_icon = img
    if sys.platform.startswith("win"):
        try:
            win.iconbitmap(default=os.path.join(BRAND, "icon.ico"))
        except Exception:
            pass


def apply_style(root):
    st = ttk.Style(root)
    try:
        if "clam" in st.theme_names() and not sys.platform.startswith("win"):
            st.theme_use("clam")
    except Exception:
        pass
    st.configure("Brand.TFrame", background=LIGHT)
    st.configure("Brand.TLabel", background=LIGHT, foreground=DARK)
    st.configure("Tag.TLabel", background=LIGHT, foreground=ORANGE, font=("TkDefaultFont", 9, "bold"))
    st.configure("Accent.TButton", foreground=LIGHT, background=ORANGE)
    st.map("Accent.TButton", background=[("active", "#D85A10"), ("disabled", "#C8C8C8")])
    st.configure("TNotebook.Tab", padding=(10, 4))
    st.map("TNotebook.Tab", foreground=[("selected", ORANGE)])


class BrandHeader(ttk.Frame):
    """White strip with the BricoHams logo, app name and version."""

    def __init__(self, parent):
        super().__init__(parent, style="Brand.TFrame", padding=(10, 6))
        self.logo = image("bricohams_logo_360.png", self)
        if self.logo is not None:
            self.logo = self.logo.subsample(2, 2)
            ttk.Label(self, image=self.logo, style="Brand.TLabel").pack(side="left")
        box = ttk.Frame(self, style="Brand.TFrame")
        box.pack(side="left", padx=14)
        ttk.Label(box, text="RT-950 Toolkit  %s" % __version__, style="Brand.TLabel",
                  font=("TkDefaultFont", 13, "bold")).pack(anchor="w")
        ttk.Label(box, text=TAGLINE[lang()], style="Tag.TLabel").pack(anchor="w")
        tk.Frame(parent, height=3, bg=ORANGE).pack(fill="x", side="top")


# ------------------------------------------------------------------ help

def help_path(name: str = "index") -> str:
    p = os.path.join(HELP, lang(), name + ".html")
    return p if os.path.exists(p) else os.path.join(HELP, "es", name + ".html")


def open_help(name: str = "index"):
    p = help_path(name)
    if os.path.exists(p):
        webbrowser.open("file:///" + p.replace("\\", "/").lstrip("/"))
    else:
        webbrowser.open(HOMEPAGE + "help/%s/%s.html" % (lang(), name))


class TextDialog(tk.Toplevel):
    """Scrollable text with the logo; optional accept/decline buttons."""

    def __init__(self, master, title: str, text: str, accept: bool = False):
        super().__init__(master)
        self.title(title)
        self.result = False
        self.transient(master)
        self.configure(bg=LIGHT)
        BrandHeader(self).pack(fill="x")
        body = ttk.Frame(self, padding=10)
        body.pack(fill="both", expand=True)
        t = tk.Text(body, wrap="word", width=82, height=24, relief="flat", padx=8, pady=6)
        sb = ttk.Scrollbar(body, command=t.yview)
        t.configure(yscrollcommand=sb.set)
        t.insert("1.0", text)
        t.configure(state="disabled")
        t.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")
        bar = ttk.Frame(self, padding=8)
        bar.pack(fill="x")
        if accept:
            ttk.Button(bar, text=_("decline"), command=self.destroy).pack(side="right", padx=4)
            ttk.Button(bar, text=_("accept"), style="Accent.TButton", command=self._ok).pack(side="right")
            self.protocol("WM_DELETE_WINDOW", self.destroy)
        else:
            ttk.Button(bar, text=_("ok"), command=self.destroy).pack(side="right")
        self.grab_set()

    def _ok(self):
        self.result = True
        self.destroy()


def ensure_disclaimer(app) -> bool:
    if app.conf.get("disclaimer") == legal.DISCLAIMER_VERSION:
        return True
    d = TextDialog(app, _("help_disclaimer"), legal.DISCLAIMER[lang()], accept=True)
    app.wait_window(d)
    if d.result:
        app.conf["disclaimer"] = legal.DISCLAIMER_VERSION
        from .gui import save_conf
        save_conf(app.conf)
    return d.result


def show_disclaimer(app):
    TextDialog(app, _("help_disclaimer"), legal.DISCLAIMER[lang()])


def show_backup_guide(app):
    TextDialog(app, _("help_backup"), legal.BACKUP_GUIDE[lang()])


def show_about(app):
    credits = {
        "es": "%s %s\nRadtel RT-950 / RT-950 Pro: codeplug, codeplugs por país, firmware, satélites y APRS.\n\n"
              "Un proyecto de %s — %s.\n\n"
              "Créditos:\n"
              "• Firmware abierto de base: Hertzz58/Radtel-RT950-Pro-Firmware (GPL-3.0)\n"
              "• Referencia del protocolo de programación: RT-950/950Pro Editor de KK4OXN (MIT)\n"
              "• Aeropuertos: OurAirports (dominio público)\n"
              "• Repetidores: URE (España) y ukrepeater.net / RSGB ETCC (Reino Unido)\n\n"
              "Licencia GPL-3.0. Sin garantía: ver Ayuda → Aviso de responsabilidad.\n%s",
        "en": "%s %s\nRadtel RT-950 / RT-950 Pro: codeplug, country codeplugs, firmware, satellites and APRS.\n\n"
              "A %s project — %s.\n\n"
              "Credits:\n"
              "• Base open firmware: Hertzz58/Radtel-RT950-Pro-Firmware (GPL-3.0)\n"
              "• Programming protocol reference: RT-950/950Pro Editor by KK4OXN (MIT)\n"
              "• Airports: OurAirports (public domain)\n"
              "• Repeaters: URE (Spain) and ukrepeater.net / RSGB ETCC (United Kingdom)\n\n"
              "GPL-3.0 licence. No warranty: see Help → Disclaimer.\n%s",
    }[lang()] % (APP_NAME, __version__, ORG, TAGLINE[lang()], REPO_URL)
    TextDialog(app, _("about"), credits)


# --------------------------------------------------------- country dialog

class CountryDialog(tk.Toplevel):
    def __init__(self, app):
        super().__init__(app)
        self.app = app
        self.title(_("country_title"))
        self.transient(app)
        set_window_icon(self)
        f = ttk.Frame(self, padding=12)
        f.pack(fill="both", expand=True)
        self.countries = country.available()
        ttk.Label(f, text=_("country_title"), font=("TkDefaultFont", 12, "bold")).grid(row=0, column=0, columnspan=2, sticky="w")
        self.cv = tk.StringVar(value=self.countries[0][1][lang()] if self.countries else "")
        cb = ttk.Combobox(f, textvariable=self.cv, state="readonly", width=30,
                          values=[t[lang()] for c, t in self.countries])
        cb.grid(row=1, column=0, sticky="w", pady=6)
        cb.bind("<<ComboboxSelected>>", lambda e: self.describe())
        self.desc = ttk.Label(f, text="", wraplength=520, foreground="#555")
        self.desc.grid(row=2, column=0, columnspan=2, sticky="w")
        ttk.Label(f, text=_("country_mode")).grid(row=3, column=0, sticky="w", pady=(10, 2))
        self.mode = tk.StringVar(value="append" if app.cp.meta.get("source") == "radio" else "overwrite")
        ttk.Radiobutton(f, text=_("mode_overwrite"), variable=self.mode, value="overwrite").grid(row=4, column=0, sticky="w")
        ttk.Radiobutton(f, text=_("mode_append"), variable=self.mode, value="append").grid(row=5, column=0, sticky="w")
        self.rename = tk.BooleanVar(value=True)
        ttk.Checkbutton(f, text=_("rename_zones"), variable=self.rename).grid(row=6, column=0, sticky="w", pady=4)
        ttk.Label(f, text=_("include")).grid(row=7, column=0, sticky="w", pady=(8, 2))
        self.kinds = {}
        for i, k in enumerate(country.KINDS):
            v = tk.BooleanVar(value=True)
            ttk.Checkbutton(f, text=_("k_" + k), variable=v).grid(row=8 + i, column=0, sticky="w")
            self.kinds[k] = v
        r = 8 + len(country.KINDS)
        ttk.Label(f, text=_("my_locator")).grid(row=r, column=0, sticky="w", pady=(8, 2))
        self.loc = tk.StringVar(value=app.conf.get("locator", ""))
        ttk.Entry(f, textvariable=self.loc, width=10).grid(row=r + 1, column=0, sticky="w")
        ttk.Label(f, text=_("country_hint"), wraplength=520, foreground="#805000").grid(row=r + 2, column=0, columnspan=2, sticky="w", pady=8)
        self.out = tk.Text(f, height=12, width=72)
        self.out.grid(row=r + 3, column=0, columnspan=2, sticky="nsew")
        bar = ttk.Frame(f)
        bar.grid(row=r + 4, column=0, columnspan=2, sticky="e", pady=(8, 0))
        ttk.Button(bar, text=_("preview"), command=self.preview).pack(side="left", padx=4)
        ttk.Button(bar, text=_("apply"), style="Accent.TButton", command=self.apply).pack(side="left", padx=4)
        ttk.Button(bar, text=_("cancel"), command=self.destroy).pack(side="left", padx=4)
        self.describe()
        self.grab_set()

    def code(self) -> str:
        name = self.cv.get()
        return next(c for c, t in self.countries if t[lang()] == name)

    def describe(self):
        d = country.load(self.code())
        self.desc.configure(text="%s\n(%s: %s)" % (d["description"][lang()], d.get("generated", ""),
                                                  "; ".join(d.get("sources", []))))

    def _run(self, cp):
        kinds = [k for k, v in self.kinds.items() if v.get()]
        loc = self.loc.get().strip() or None
        return country.apply(cp, self.code(), self.mode.get(), loc, kinds, self.rename.get())

    def preview(self):
        from .codeplug import Codeplug
        tmp = Codeplug.from_json(self.app.cp.to_json())
        try:
            rep = self._run(tmp)
        except Exception as e:
            messagebox.showerror(_("error"), str(e), parent=self)
            return
        self.out.delete("1.0", "end")
        self.out.insert("1.0", rep.text())

    def apply(self):
        try:
            rep = self._run(self.app.cp)
        except Exception as e:
            messagebox.showerror(_("error"), str(e), parent=self)
            return
        if self.loc.get().strip():
            self.app.conf["locator"] = self.loc.get().strip()
        self.app.log(rep.text())
        self.app.changed()
        self.app.refresh()
        self.destroy()


# ------------------------------------------------------------ firmware tab

class FirmwareTab(ttk.Frame):
    def __init__(self, parent, app):
        super().__init__(parent, padding=10)
        self.app = app
        self.assets = []
        self.path: Optional[str] = None
        ttk.Label(self, text=_("fw_note"), wraplength=900, foreground="#805000").pack(anchor="w", pady=(0, 8))
        top = ttk.Frame(self)
        top.pack(fill="x")
        ttk.Label(top, text=_("fw_published"), font=("TkDefaultFont", 10, "bold")).pack(side="left")
        ttk.Button(top, text=_("fw_refresh"), command=self.refresh_online).pack(side="left", padx=8)
        ttk.Button(top, text=_("fw_from_file"), command=self.pick_file).pack(side="left")
        ttk.Button(top, text=_("fw_web"), command=lambda: webbrowser.open(HOMEPAGE + "flasher/")).pack(side="right")
        self.lb = tk.Listbox(self, height=8)
        self.lb.pack(fill="both", expand=True, pady=6)
        self.lb.bind("<<ListboxSelect>>", lambda e: self.pick_online())
        self.sel = ttk.Label(self, text="%s: %s" % (_("fw_selected"), _("fw_none")))
        self.sel.pack(anchor="w")
        self.inbl = tk.BooleanVar(value=False)
        ttk.Checkbutton(self, text=_("fw_in_bootloader"), variable=self.inbl).pack(anchor="w", pady=4)
        bar = ttk.Frame(self)
        bar.pack(fill="x", pady=6)
        ttk.Button(bar, text=_("help_backup"), command=lambda: show_backup_guide(app)).pack(side="left")
        ttk.Button(bar, text=_("fw_flash"), style="Accent.TButton", command=self.flash).pack(side="right")

    def refresh(self):
        pass

    def refresh_online(self):
        self.lb.delete(0, "end")

        def job():
            try:
                assets = firmware.list_published()
            except Exception as e:
                self.app.log(_("fw_offline") % e)
                return
            self.app.q.put(("call", lambda: self._fill(assets)))
        threading.Thread(target=job, daemon=True).start()

    def _fill(self, assets):
        self.assets = assets
        for a in assets:
            self.lb.insert("end", a.label)
        if not assets:
            self.lb.insert("end", "-")

    def _show(self, path):
        try:
            info = flasher.FirmwareInfo(open(path, "rb").read())
            info.check()
        except Exception as e:
            messagebox.showerror(_("error"), str(e))
            self.path = None
            return
        self.path = path
        self.sel.configure(text="%s: %s  [%s]  SHA-256 %s…" % (_("fw_selected"), os.path.basename(path), info,
                                                               firmware.sha256_file(path)[:16]))

    def pick_file(self):
        p = filedialog.askopenfilename(filetypes=[("Firmware", "*.BTF *.btf"), ("*", "*")])
        if p:
            self._show(p)

    def pick_online(self):
        i = self.lb.curselection()
        if not i or i[0] >= len(self.assets):
            return
        a = self.assets[i[0]]

        def job():
            try:
                p = firmware.download(a)
            except Exception as e:
                self.app.log("ERROR: %s" % e)
                return
            self.app.q.put(("call", lambda: self._show(p)))
        threading.Thread(target=job, daemon=True).start()

    def flash(self):
        if not self.path:
            messagebox.showwarning(APP_NAME, _("fw_none"))
            return
        port = self.app.port()
        if not port or not ensure_disclaimer(self.app):
            return
        info = flasher.FirmwareInfo(open(self.path, "rb").read())
        if not messagebox.askyesno(APP_NAME, _("fw_confirm") % ("%s\n%s" % (os.path.basename(self.path), info))):
            return
        data = open(self.path, "rb").read()
        inbl = self.inbl.get()

        def job():
            ser = protocol.open_serial(port, baud=flasher.BAUD, timeout=3.0)
            try:
                flasher.flash(ser, data, enter_bootloader=not inbl, progress=self.app.progress, log=self.app.log)
            finally:
                ser.close()
        self.app.run_bg(job)
