#!/bin/sh
# build_deb.sh [OUTDIR] - bricohams-rt950-toolkit_<version>_all.deb for
# Debian 11+ / Ubuntu 22.04+ (pure Python: depends on python3 and python3-tk).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
PC="$(cd "$HERE/../.." && pwd)"
OUT="${1:-$PC/dist}"
VER="$(python3 -c "import sys; sys.path.insert(0, '$PC'); import rt950_toolkit as t; print(t.__version__)")"
ROOT="$(mktemp -d)/pkg"
sh "$HERE/stage.sh" "$ROOT"
mkdir -p "$ROOT/DEBIAN"
SIZE="$(du -sk "$ROOT" | cut -f1)"
cat > "$ROOT/DEBIAN/control" <<CTRL
Package: bricohams-rt950-toolkit
Version: $VER
Section: hamradio
Priority: optional
Architecture: all
Depends: python3 (>= 3.9), python3-tk
Recommends: python3-pil
Installed-Size: $SIZE
Maintainer: BricoHams <maitchovcow@gmail.com>
Homepage: https://anatolbricoham.github.io/rt950pro-satellite-mode/
Description: Programmer for the Radtel RT-950 / RT-950 Pro (BricoHams)
 Codeplug editor for the Radtel RT-950 and RT-950 Pro over the programming
 cable: channels, zones, settings, APRS, CPS .dat and CHIRP / RT-950 Editor
 CSV files, preloaded country codeplugs (Spain, United Kingdom), firmware
 update through the bootloader, satellite passes and Doppler channels.
CTRL
cat > "$ROOT/DEBIAN/postinst" <<'POST'
#!/bin/sh
set -e
command -v udevadm >/dev/null 2>&1 && udevadm control --reload-rules 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q /usr/share/icons/hicolor 2>/dev/null || true
exit 0
POST
chmod 755 "$ROOT/DEBIAN/postinst"
cat > "$ROOT/DEBIAN/prerm" <<'PRERM'
#!/bin/sh
set -e
find /usr/lib/bricohams-rt950-toolkit -name __pycache__ -type d -prune -exec rm -rf {} + 2>/dev/null || true
exit 0
PRERM
chmod 755 "$ROOT/DEBIAN/prerm"
mkdir -p "$OUT"
dpkg-deb --root-owner-group --build "$ROOT" "$OUT/bricohams-rt950-toolkit_${VER}_all.deb"
