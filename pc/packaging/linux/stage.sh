#!/bin/sh
# stage.sh DESTDIR - install BricoHams RT-950 Toolkit into DESTDIR (shared by
# the .deb builder and the Arch PKGBUILD). Needs: python3, pip (to fetch the
# pure-Python sgp4 and pyserial sources) unless VENDOR_DIR already holds them.
set -e
DEST="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"
PC="$(cd "$HERE/../.." && pwd)"
LIB="$DEST/usr/lib/bricohams-rt950-toolkit"
mkdir -p "$LIB/vendor" "$DEST/usr/bin" "$DEST/usr/share/applications" \
         "$DEST/usr/share/icons/hicolor/256x256/apps" "$DEST/usr/share/icons/hicolor/64x64/apps" \
         "$DEST/usr/lib/udev/rules.d" "$DEST/usr/share/doc/bricohams-rt950-toolkit"
cp -r "$PC/rt950_toolkit" "$LIB/"
find "$LIB" -name "__pycache__" -type d -prune -exec rm -rf {} +
if [ -z "$VENDOR_DIR" ]; then
    VENDOR_DIR="$(mktemp -d)"
    python3 -m pip download --no-deps --no-binary :all: "sgp4==2.27" "pyserial==3.5" -d "$VENDOR_DIR" -q
fi
for t in "$VENDOR_DIR"/sgp4-*.tar.gz "$VENDOR_DIR"/pyserial-*.tar.gz; do tar xzf "$t" -C "$VENDOR_DIR"; done
cp -r "$VENDOR_DIR"/sgp4-*/sgp4 "$LIB/vendor/"
cp -r "$VENDOR_DIR"/pyserial-*/serial "$LIB/vendor/"
cp "$VENDOR_DIR"/sgp4-*/LICENSE "$DEST/usr/share/doc/bricohams-rt950-toolkit/LICENSE.sgp4" 2>/dev/null || true
cp "$VENDOR_DIR"/pyserial-*/LICENSE.txt "$DEST/usr/share/doc/bricohams-rt950-toolkit/LICENSE.pyserial" 2>/dev/null || true
find "$LIB/vendor" \( -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "tests.py" \) -delete
install -m 755 "$HERE/bricohams-rt950-toolkit" "$DEST/usr/bin/bricohams-rt950-toolkit"
install -m 644 "$HERE/bricohams-rt950-toolkit.desktop" "$DEST/usr/share/applications/"
install -m 644 "$PC/rt950_toolkit/data/brand/icon_256.png" "$DEST/usr/share/icons/hicolor/256x256/apps/bricohams-rt950-toolkit.png"
install -m 644 "$PC/rt950_toolkit/data/brand/icon_64.png" "$DEST/usr/share/icons/hicolor/64x64/apps/bricohams-rt950-toolkit.png"
install -m 644 "$HERE/60-bricohams-rt950.rules" "$DEST/usr/lib/udev/rules.d/"
install -m 644 "$PC/../LICENSE" "$DEST/usr/share/doc/bricohams-rt950-toolkit/copyright"
