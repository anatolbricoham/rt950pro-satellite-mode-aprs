#!/bin/sh
# Host-side verification of the satellite feature. Needs: gcc, python3,
# pip packages sgp4 skyfield pyserial pillow aprslib.
set -e
cd "$(dirname "$0")/.."
B=build_host
mkdir -p $B
CF="-O2 -g -Wall -Wextra -Wshadow -std=c11 -Iinclude -I$B"
SAT="src/app/sat_math.c src/app/sat_sgp4.c src/app/sat_pred.c"

echo "== build host tools"
gcc $CF -o $B/sgp4_driver tests/sgp4_driver.c $SAT -lm
python3 tests/sim/extract.py $B/sim_extract.h
gcc $CF -Wno-unused-parameter -Wno-unused-function -o $B/sim tests/sim/sim.c $SAT \
    src/app/satellite.c src/app/sat_ui.c src/app/cps.c src/drivers/flash_crc.c \
    src/app/aprs_msg_codec.c src/app/aprs_msg.c src/app/aprs_msg_ui.c -lm

echo "== 1. SGP4 / geometry / math vs python-sgp4 + Skyfield"
python3 tests/test_sgp4.py
echo "== 2. pass predictor vs Skyfield"
python3 tests/test_pred.py
echo "== 4. APRS messaging codec + engine + UI"
python3 tests/test_aprs_msg.py
echo "== 3. PC tool -> CPS upload (pty) -> firmware -> UI render"
python3 tests/test_e2e.py
echo "== 5. RT-950 Toolkit: codeplug, CSV, .dat, emulated stock radio"
python3 -m unittest discover -s pc/tests
if command -v node >/dev/null 2>&1; then
    echo "== 7. Web flasher (Node, emulated bootloader)"
    BTF=build/rt950-custom.BTF; [ -f "$BTF" ] || BTF=binary/RT_950Pro_V0.27_260203/RT_950Pro_V0.27_260203.BTF
    node tests/web/flasher.test.cjs "$BTF"
    echo "== 8. Android / web programmer: native Bluetooth transport"
    node tests/web/native_ble.test.cjs
fi
if python3 -c "import playwright" 2>/dev/null; then
    echo "== 8b. Android / web programmer UI in Chromium (simulated Bluetooth radio)"
    python3 tests/web/app_e2e.py
fi
