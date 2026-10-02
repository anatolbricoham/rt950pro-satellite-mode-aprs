# Tests — Satellite Mode, APRS messaging, RT-950 Toolkit, country codeplugs, firmware and Android app

*[Versión en español](TESTING.md)*

## Running

```bash
pip install sgp4 skyfield pyserial pillow aprslib
make            # ARM firmware: must build with -Werror and no warnings
make test-host  # = sh tests/run_tests.sh (tests 1 to 5)
python -m unittest discover -s pc/tests -v       # RT-950 Toolkit only
xvfb-run python pc/tests/gui_smoke.py /tmp en     # graphical interface (screenshots)
node tests/web/flasher.test.cjs build/rt950-custom.BTF   # web flasher
node tests/web/native_ble.test.cjs                 # Android app Bluetooth
python tests/web/app_e2e.py                        # web/Android app in Chromium (Playwright)
```

GitHub Actions runs the same on every push (`.github/workflows/build.yml`).

## What is verified on the PC (real firmware code)

| # | Test | Reference | Result |
|---|---|---|---|
| 1 | SGP4, TEME position and velocity (5 element sets, 11 instants, −1 to +7 days; includes the Spacetrack Report #3 test case) | `python-sgp4` 2.27 | error ≤ 1.5·10⁻⁹ km and 2·10⁻¹² km/s |
| 1 | AZ/EL/range/range rate (600 random samples, ISS/SO-50/AO-123, QTH IM98IB) | Skyfield 1.55 | 0.002° azimuth, 0.003° elevation, 36 m, 0.23 m/s (0.3 Hz at 437 MHz) |
| 1 | sin, cos, atan, asin, acos, sqrt, cbrt | libm | relative error ≤ 4.4·10⁻¹⁶ |
| 2 | Pass predictor (24 h, 19 passes, including a pass in progress and a 0° grazer) | Skyfield `find_events` | AOS, TCA and LOS: 0 s difference; max elevation ±0.5°; about 186 propagations per pass |
| 3 | Upload through the real CPS protocol (`cps.c`) over a pseudo terminal | `satdb.bin` contents | identical flash, verify OK, clock = PC, reload = 4 satellites |
| 3 | Next pass computed in the "radio" vs the PC | `rt950_sat.py` | 0 s difference |
| 3 | SO-50 tracking mid-pass | direct computation | RX 436.8029 MHz (+7.9 kHz), TX 145.8473 MHz (−2.6 kHz), VFO A/B and chips match |
| 3 | PTT with arming | — | TX on VFO A, tone index 3 (74.4 Hz) |
| 3 | Leaving satellite mode | previous state | VFO A and B restored exactly |
| 3 | LIST / PASSES / TRACK / TX screens | visual inspection | PNG in `build_host/e2e/` |
| 4 | Outgoing message: addresses, path and FCS | the test's own AX.25 decoder and `aprslib` | `EA7ABC-7>APZ950,WIDE1-1::EB5XYZ-7 :Hola desde el RT-950 Pro{601` |
| 4 | Ack received / no ack | — | OK / 1 + 3 retries, then X |
| 4 | Incoming message and its digipeated copy | — | 2 acks, 1 inbox entry |
| 4 | Other stations' traffic, bulletin, text with `{` | — | ignored, stored, rejected |
| 4 | Multi-tap reply and screens | visual inspection | sent; PNG in `build_host/aprs/` |
| 5 | Tones (261), BCD, full channel, FHSS bits, GBK names | reverse encoding | identical |
| 5 | Every option of the 131 select settings, DTMF, signed values | — | read back as written |
| 5 | Native, CHIRP and Editor CSV (channels, zones, FM/AM/SSB) | round trip + Editor templates | identical; 16 PMR channels, 3 template channels, 10 zones |
| 5 | CPS `.dat` | the Editor's `startup_default.dat` | MS-NRBF rewritten byte for byte; channel, zone and call sign changed and read back |
| 5 | Full read against the radio emulator | emulator memory | identical codeplug; APRS via `T` at address 0; key `RVB ` |
| 5 | Write + verify, channels only, wrong model | emulator memory | identical; APRS page untouched; session refused |
| 5 | CLI `read` / `write --channels-only` (also with the PyInstaller executable) | emulator memory | correct, 2 backups |
| 5 | Graphical interface in English and Spanish (Xvfb) | visual inspection | no exceptions; screenshots in `docs/toolkit/img/` |
| 6 | Spain codeplug: zones EA1–EA9 + PMR-CB, 455 channels, PMR/CB/air band without TX, LEMD in EA4, LEAB in EA5, −600 kHz / −7.6 MHz shifts | source data | correct |
| 6 | UK codeplug: 10 zones ≤ 99 channels, distance-based trimming (Heathrow kept with IO91WM), PMR + CB 27/81 + CEPT | source data | correct |
| 6 | Add mode: keeps channels and zone names, skips duplicates | — | correct |
| 6 | Partial codeplug on the emulated radio: only channels and zone names; settings, rest of the zone block and APRS untouched; refused without a fresh read | emulator memory | correct |
| 6 | Copied source lists (URE 144/432, ukrepeater.net) | checksum computed on the source web site | identical |
| 7 | Flashing from the firmware and in bootloader mode, wrong model, foreign file | bootloader emulator (Python and Node) | received image identical; refusals correct |
| 7 | Python and JavaScript flasher packets | byte comparison | identical |
| 8 | JavaScript core (Android/web app) vs Python: decoding, re-encoding, ES/GB codeplugs and add mode | files written by Python | byte-for-byte identical |
| 8 | JavaScript protocol: full read, verified differential write, partial codeplug, no changes = no writes | emulated radio | correct |
| 8 | Native Bluetooth transport (Capacitor): 0xFF31 token, hex encoding, handshake and read | simulated plugin | correct |
| 8 | App UI in Chromium with simulated Web Bluetooth: disclaimer, connect, read, country (add), write | emulated radio | radio holds EA1… and the user's channel; settings untouched |
| 9 | `.deb` installed and removed; application run with the bundled dependencies only | dpkg | correct |
| 9 | PyInstaller one-folder executable | run | correct |

## On-radio test plan (pending: needs hardware)

**Always use a 50 Ω dummy load** and, if possible, an analyser or a second
receiver.

### Satellite mode

1. **Boot**: the firmware boots and, without `satdb.bin`, the icon is hidden
   and the list shows "No satellite data".
2. **Upload**: `rt950_sat.py upload --port …` ends with "verify OK" and
   "N satellites". The grey icon appears.
3. **Time**: without GPS, the header shows the PC time (`UTC:PC`). With a GPS
   fix it switches to `UTC:GPS` and stays within 1 s.
4. **Position**: with GPS, `QTH xxxxxx (GPS)`. Without GPS, the file's
   locator with `(DB)`.
5. **Reception**: during an ISS or SO-50 pass, the VFO A frequency changes
   every second and the transponder is heard. The encoder trims ±100 Hz.
6. **Transmission (dummy load)**: with PTT, the analyser shows the corrected
   uplink frequency and the right CTCSS (67.0 Hz, or 74.4 Hz after pressing
   `*`). On release, RX returns to the downlink.
7. **TX VFO = VFO B**: check that VFO B's chip transmits and record whether
   VFO A keeps hearing (full duplex).
8. **RX only**: with RS-44 selected, PTT does not transmit.
9. **Exit**: `#` in the list returns the VFOs to their previous state.
10. **Robustness**: upload data with satellite mode open, unplug the GPS
    during a pass and power-cycle the radio (settings persist).

### RT-950 Toolkit with a real radio

1. **Read** a radio with stock firmware and save the `.rt950`. Check in the
   Channels tab that they match what the radio shows.
2. **Write without changes**: it must end with a successful verify and the
   radio must stay the same.
3. Change a channel (name, CTCSS tone, inverted DCS, power) and a zone,
   write and check it on the radio.
4. Change the APRS call sign and check that the APRS page write (`X`)
   completes (it may take several seconds).
5. **Save as .dat** (with a CPS `.dat` as template) and check that the file
   opens in the official CPS and in the Editor.

### APRS messaging with a real radio

1. Program call sign, SSID and path. Tune 144.800 MHz.
2. Send a message to another station (or to a service such as an
   auto-replying iGate) and check on aprs.fi that the frame is correct and
   that the `ack` arrives.
3. Receive a message from another radio or from aprs.fi and check the
   automatic ack and the `MSG` indicator.

### Country codeplug, firmware and Android with a real radio

1. New radio (or after a backup): Country codeplug → Overwrite → Write.
   Check zones, a nearby repeater and AM reception of an airport. Check that
   PMR and CB do not transmit.
2. Configured radio: Read → Add → Write. Check that your own channels are
   still there and the settings did not change.
3. Flash the custom firmware from the application and go back to the
   official V0.27 from the web flasher.
4. Android app: connect over Bluetooth, read, edit a channel, write and
   check it on the radio.

Record the results in an issue with the firmware version, the `satdb.bin`
version and screenshots.
