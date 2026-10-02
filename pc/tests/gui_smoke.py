"""gui_smoke.py - build the main window, fill it, visit every tab, take
screenshots (run under xvfb-run).  Fails on any Tk exception."""
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tkinter as tk  # noqa: E402
os.environ["BH_SKIP_DISCLAIMER"] = "1"
from rt950_toolkit import gui  # noqa: E402
from test_toolkit import sample_codeplug  # noqa: E402

out = sys.argv[1] if len(sys.argv) > 1 else "."
lang = sys.argv[2] if len(sys.argv) > 2 else "en"
errors = []
tk.Tk.report_callback_exception = lambda self, e, v, tb: errors.append(v)
app = gui.App()
app.lang(lang)
app.cp = sample_codeplug()
app.refresh()
steps = list(app.tabs) + ["dialog"]


def shot(name):
    app.update()
    subprocess.run(["import", "-window", "root", os.path.join(out, "%s-%s.png" % (lang, name))], check=True)


def step(i=0):
    if i < len(steps) - 1:
        app.nb.select(app.tabs[steps[i]])
        app.after(400, lambda: (shot(steps[i]), step(i + 1)))
    else:
        app.nb.select(app.tabs["channels"])
        d = gui.ChannelDialog(app, app.cp.channel(1))
        app.after(500, lambda: (shot("dialog"), d.destroy(), country()))

def country():
    from rt950_toolkit import gui_extra as X
    d = X.CountryDialog(app)
    d.preview()
    app.after(600, lambda: (shot("country"), d.destroy(), about()))

def about():
    from rt950_toolkit import gui_extra as X
    X.show_about(app)
    app.after(500, lambda: (shot("about"), disclaimer()))

def disclaimer():
    from rt950_toolkit import gui_extra as X
    for w in app.winfo_children():
        if isinstance(w, tk.Toplevel):
            w.destroy()
    X.show_disclaimer(app)
    app.after(500, lambda: (shot("disclaimer"), app.destroy()))


app.after(800, step)
app.mainloop()
if errors:
    print("ERRORS:", errors)
    sys.exit(1)
print("GUI OK")
