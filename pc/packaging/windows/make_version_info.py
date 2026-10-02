"""Write version_info.txt (Windows file properties of RT950Toolkit.exe)."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))
import rt950_toolkit as t  # noqa: E402

v = tuple(int(x) for x in t.__version__.split(".")) + (0,)
open(os.path.join(os.path.dirname(__file__), "version_info.txt"), "w", encoding="utf-8").write(f"""VSVersionInfo(
  ffi=FixedFileInfo(filevers={v}, prodvers={v}, mask=0x3f, flags=0x0, OS=0x40004, fileType=0x1, subtype=0x0, date=(0, 0)),
  kids=[StringFileInfo([StringTable('040904B0', [
    StringStruct('CompanyName', 'BricoHams'),
    StringStruct('FileDescription', 'BricoHams RT-950 Toolkit'),
    StringStruct('FileVersion', '{t.__version__}'),
    StringStruct('InternalName', 'RT950Toolkit'),
    StringStruct('LegalCopyright', 'GPL-3.0 - BricoHams'),
    StringStruct('OriginalFilename', 'RT950Toolkit.exe'),
    StringStruct('ProductName', 'BricoHams RT-950 Toolkit'),
    StringStruct('ProductVersion', '{t.__version__}')])]),
  VarFileInfo([VarStruct('Translation', [1033, 1200])])])
""")
