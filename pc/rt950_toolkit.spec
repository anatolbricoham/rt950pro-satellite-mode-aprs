# PyInstaller spec - BricoHams RT-950 Toolkit (Windows and Linux)
#   pyinstaller --clean --noconfirm rt950_toolkit.spec              -> one folder (installer, fewer AV false positives)
#   BH_ONEFILE=1 pyinstaller --clean --noconfirm rt950_toolkit.spec -> single portable executable
import os
import sys

block_cipher = None
onefile = bool(os.environ.get("BH_ONEFILE"))
win = sys.platform.startswith("win")

a = Analysis(
    ["run_toolkit.py"],
    pathex=["."],
    datas=[("rt950_toolkit/data", "rt950_toolkit/data"), ("rt950_toolkit/help", "rt950_toolkit/help")],
    hiddenimports=["serial.tools.list_ports", "PIL.Image", "sgp4.api"],
    excludes=["numpy", "matplotlib", "scipy", "pandas", "pytest"],
    cipher=block_cipher,
)
pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)
version = os.path.join("packaging", "windows", "version_info.txt")
common = dict(name="RT950Toolkit", icon="rt950_toolkit/data/brand/icon.ico", upx=False,
              console=not win, version=version if win and os.path.exists(version) else None)
if onefile:
    exe = EXE(pyz, a.scripts, a.binaries, a.zipfiles, a.datas, [], **common)
else:
    exe = EXE(pyz, a.scripts, [], exclude_binaries=True, **common)
    coll = COLLECT(exe, a.binaries, a.zipfiles, a.datas, upx=False, name="RT950Toolkit")
