"""
python -m rt950_toolkit            -> desktop application
python -m rt950_toolkit read  --port COM5 -o radio.rt950
python -m rt950_toolkit write --port COM5 radio.rt950 [--channels-only] [--no-verify]
python -m rt950_toolkit export-chirp radio.rt950 out.csv
python -m rt950_toolkit import-chirp radio.rt950 in.csv [--start 1] [-o new.rt950]
python -m rt950_toolkit info radio.rt950
python -m rt950_toolkit country ES -o espana.rt950 [-i radio.rt950 --mode append] [--locator IM98IB]
python -m rt950_toolkit flash --port COM5 firmware.BTF   -> firmware update (bootloader)
python -m rt950_toolkit sat ...     -> satellite tool (same as tools/rt950_sat.py)
"""
import argparse
import sys

from . import APP_NAME, __version__


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv or argv[0] in ("gui",):
        from .gui import run_gui
        run_gui()
        return 0
    if argv[0] == "sat":
        from .sat import main as sat_main
        return sat_main(argv[1:])

    from . import codeplug as C, csvio, protocol
    ap = argparse.ArgumentParser(prog="rt950_toolkit", description="%s %s" % (APP_NAME, __version__))
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("read"); r.add_argument("--port", required=True); r.add_argument("-o", "--out", required=True)
    w = sub.add_parser("write"); w.add_argument("--port", required=True); w.add_argument("file")
    w.add_argument("--channels-only", action="store_true"); w.add_argument("--no-verify", action="store_true")
    e = sub.add_parser("export-chirp"); e.add_argument("file"); e.add_argument("csv")
    i = sub.add_parser("import-chirp"); i.add_argument("file"); i.add_argument("csv")
    i.add_argument("--start", type=int, default=0); i.add_argument("-o", "--out")
    n = sub.add_parser("info"); n.add_argument("file")
    c = sub.add_parser("country", help="preloaded country codeplug (ES, GB)")
    c.add_argument("code"); c.add_argument("-o", "--out", required=True)
    c.add_argument("-i", "--input", help="codeplug to add to / overwrite (default: empty)")
    c.add_argument("--mode", choices=["overwrite", "append"], default="overwrite")
    c.add_argument("--locator", help="your locator: nearest channels are kept when a zone is full")
    c.add_argument("--no-repeaters", action="store_true"); c.add_argument("--no-airports", action="store_true")
    c.add_argument("--no-pmr-cb", action="store_true")
    c.add_argument("--chirp", help="also export a CHIRP CSV"); c.add_argument("--editor-csv", help="also export RT-950 Editor CSV")
    fl = sub.add_parser("flash", help="update the firmware (.BTF) through the bootloader")
    fl.add_argument("--port", required=True); fl.add_argument("file")
    fl.add_argument("--in-bootloader", action="store_true", help="radio already in bootloader mode")
    a = ap.parse_args(argv)

    last = {"p": None}

    def prog(d, t, w):
        p = (w, d * 100 // max(1, t))
        if p != last["p"] and sys.stdout.isatty():
            print("\r  %-16s %3d%%" % p, end="", flush=True)
        last["p"] = p
    if a.cmd == "read":
        ser = protocol.open_serial(a.port)
        try:
            link = protocol.detect(ser)
            print("%s (%s)" % (link.model, link.kind))
            cp = protocol.read_codeplug(link, prog)
            link.end(False)
        finally:
            ser.close()
        print()
        cp.save(a.out)
        print("saved", a.out, "| backup", protocol.save_backup(cp, "read"))
    elif a.cmd == "write":
        cp = C.Codeplug.load(a.file)
        ser = protocol.open_serial(a.port)
        try:
            link = protocol.detect(ser)
            print("%s (%s) - reading backup" % (link.model, link.kind))
            backup = protocol.read_codeplug(link, prog)
            print("backup", protocol.save_backup(backup, "before-write"))
            written = protocol.write_codeplug(link, cp, ["channels", "zones"] if a.channels_only else None,
                                              verify=not a.no_verify, progress=prog, base=backup)
            print("\nregions written:", ", ".join(written))
            link.end(True)
        finally:
            ser.close()
        print("\nwritten")
    elif a.cmd == "export-chirp":
        csvio.export_chirp(C.Codeplug.load(a.file), a.csv)
    elif a.cmd == "import-chirp":
        cp = C.Codeplug.load(a.file)
        wr = csvio.import_chirp(cp, a.csv, start=None if a.start == 0 else a.start - 1)
        cp.save(a.out or a.file)
        print("%d channels" % len(wr))
    elif a.cmd == "country":
        from . import country
        kinds = [k for k in country.KINDS if not (
            (k == "repeater" and a.no_repeaters) or (k == "airport" and a.no_airports)
            or (k in ("pmr", "cb") and a.no_pmr_cb))]
        if a.input:
            cp = C.Codeplug.load(a.input)
            rep = country.apply(cp, a.code, a.mode, a.locator, kinds)
        else:
            cp, rep = country.build(a.code, a.locator, kinds)
        cp.save(a.out)
        print(rep.text())
        if a.chirp:
            csvio.export_chirp(cp, a.chirp)
        if a.editor_csv:
            csvio.export_editor(cp, a.editor_csv)
    elif a.cmd == "flash":
        from . import flasher
        ser = protocol.open_serial(a.port, baud=flasher.BAUD)
        try:
            flasher.flash(ser, open(a.file, "rb").read(), enter_bootloader=not a.in_bootloader,
                          progress=prog, log=print)
        finally:
            ser.close()
        print("\nfirmware written")
    elif a.cmd == "info":
        cp = C.Codeplug.load(a.file)
        used = [c for c in cp.channels() if not c.empty]
        print("%s | %d channels used | meta %s" % (a.file, len(used), cp.meta))
        for c in used[:40]:
            print("  %3d %-12s %s %s %s/%s" % (c.number, c.name, csvio.mhz(c.rx_hz), csvio.mhz(c.tx_hz), c.rx_tone, c.tx_tone))
    return 0


if __name__ == "__main__":
    sys.exit(main())
