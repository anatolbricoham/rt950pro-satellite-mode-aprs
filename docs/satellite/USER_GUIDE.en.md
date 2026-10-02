# User guide — RT-950 Pro Satellite Mode

*[Versión en español](USER_GUIDE.md)*

Everything `rt950_sat.py` does is also in the **Satellites** tab of
[RT-950 Toolkit](../toolkit/README.en.md), which also adds the Doppler
channels straight into the codeplug and writes the radio.

## 1. Preparing on the PC

```bash
pip install sgp4 pyserial          # pillow/skyfield only for the tests
cd tools
python rt950_sat.py all --locator IM98IB --hours 48 --min-el 0
```

| Option | Default | Meaning |
|---|---|---|
| `--locator` | `IM98IB` | Maidenhead QTH (4, 6 or 8 characters) |
| `--lat --lon --alt` | — | Exact coordinates (override the locator) |
| `--hours` | 48 | Report prediction window |
| `--min-el` | 0 | Horizon mask in degrees |
| `--tz` | `Europe/Madrid` | Report time zone |
| `--start-channel` | 900 | First memory of the channel CSV |
| `--db-hours` | 72 | Hours of passes stored in `satdb.bin` |
| `--tle-file` | — | Use a local TLE file instead of downloading |
| `--offline` | — | Do not download; use `rt950_sat_out/tle_cache.txt` |
| `--port` | — | Serial port: uploads `satdb.bin` and sets the radio clock |

Other commands:

```bash
python rt950_sat.py fetch                       # only download TLEs
python rt950_sat.py passes --min-el 10          # console table + report
python rt950_sat.py chirp --start-channel 900   # only the channel CSV
python rt950_sat.py upload --port COM5          # upload an existing satdb.bin
python rt950_sat.py settime --port COM5         # only set the radio's UTC clock
python rt950_sat.py dump rt950_sat_out/satdb.bin
python ../pc/run_toolkit.py                     # graphical interface (RT-950 Toolkit)
```

To add or edit satellites, change `pc/rt950_toolkit/data/sat_freqs.json`.
Each entry gives the name shown on the radio (12 characters at most), the
NORAD number, the mode (`FM`, `FM_DATA`, `LIN_INV`, `LIN` or `RX_ONLY`), the
frequencies in MHz, the tones in Hz, flags (`scheduled`, `sunlit_only`) and
the `enabled` and `favorite` options. The AOS alert only sounds for
favourites and for the satellite you are tracking.

## 2. Way A — Stock firmware

### With RT-950 Toolkit

1. **Read from radio** (a backup is saved).
2. **Satellites** tab: locator, **Update TLE**, **Calculate passes**.
3. **Add Doppler channels from no.** (900 by default) and **Write to radio**.
4. **Open report** to see when to change channel.

### With RT-950/950Pro Editor

1. Run `python rt950_sat.py all --locator YOUR_LOCATOR`.
2. In RT-950/950Pro Editor, read the radio (**Radio > Read Radio**) and save
   a backup.
3. Open `rt950_sat_out/canales_satelite_chirp.csv` (the Editor opens
   CHIRP-compatible CSVs in their own tab).
4. Copy the channels to the zone you prefer (memories 900–920 by default) and
   write the radio (**Radio > Write Radio**) with verification enabled.
5. Open `pases_satelites.html`. Each pass shows **when** to change channel,
   for example:

   `12:27:24 SO50 AOS · 12:31:14 SO50 A2 · 12:33:24 SO50 TCA · 12:34:44 SO50 L2 · 12:37:04 SO50 LOS`

Each FM satellite takes 5 split channels (RX on the downlink, TX on the
uplink with its CTCSS), stepped according to its orbital height. SO-50 also
gets a `SO50 ARM` channel with 74.4 Hz to arm the repeater: hold PTT for
2 seconds on that channel, then move to the Doppler channels.

## 3. Way B — Custom firmware with satellite mode

### Loading the data

```bash
python rt950_sat.py all --locator IM98IB --port COM5
```

The program checks that the radio runs the firmware with satellite support
(command `I`) and writes nothing if it does not find it. It then erases and
writes the reserved 32 KB, reads them back to verify, sets the radio's UTC
clock and asks it to reload the database. The satellite icon appears in the
status bar.

### Entering satellite mode

There are three ways:

- **Menu → Satellite → Sat Mode → MENU**
- Long press of **D**
- A PF key programmed with function 8 ("Satellite")

### LIST screen

Satellites sorted by next AOS, with those in view at the top (`LIVE`). Each
row shows the AOS time, the time remaining, the maximum elevation, the
entry and exit azimuths and the mode.

| Key | Action |
|---|---|
| Encoder, `*`/`B`, `0`/`A` | Move the cursor |
| `MENU` | Track the satellite (TRACK screen) |
| `1` | Show its next passes |
| `D` | Satellite mode settings |
| `#` | Leave satellite mode (restores the VFOs) |

### TRACK screen

- **Polar plot**: N up, circles at 0°, 30° and 60° elevation, the pass track
  (green = AOS, red = LOS) and the current position (yellow square).
- **Right column**: AZ, EL, range, range rate (km/s; negative = approaching),
  height, AOS/TCA/LOS, maximum elevation and azimuths.
- **RX (VFO A)**: corrected downlink, with the Doppler applied, the fine
  tuning (`trim`) and the squelch tone.
- **TX (VFO A or B)**: corrected uplink, with its Doppler and CTCSS. It turns
  red while transmitting. `RX ONLY` means the radio will not transmit.

| Key | Action |
|---|---|
| `PTT` | Transmit on the corrected uplink (TX is computed when pressed) |
| Encoder | RX fine tuning ±100 Hz (up to ±20 kHz) |
| `5` | Reset the fine tuning |
| `*` | Arm: the **next** transmission uses the arming tone (SO-50) |
| `0` | Doppler on/off |
| `A` / `B` | Previous or next satellite |
| `1` | Pass list of this satellite |
| `MENU` / `D` | Settings |
| `#` | Back to the list (stops controlling the VFOs) |

### Settings (menu → Satellite)

| Option | Values | Use |
|---|---|---|
| Sat Mode | Enter | Enters satellite mode |
| Min Elev | 0–30° | Horizon mask for the radio's predictions |
| Doppler | On/Off | RX and TX Doppler correction |
| TX VFO | Same(A) / VFO B | Chip that transmits (see decision D-14) |
| RX Bandw | Narrow/Wide | RX bandwidth (Wide tolerates Doppler error better) |
| Location | Auto / GPS / DB-Man | QTH source |
| AOS Alert | Off, 1–15 min | Beep before AOS (favourites and active satellite) |
| Sat Data | n sats | Number of satellites loaded (read only) |

If `Min Elev` matches the mask used on the PC and you are less than 25 km
from the file's QTH, the radio uses the PC's pass table directly. Otherwise
it computes the passes itself.

### Time and position

- **Time**: taken from GPS (RMC with date) as soon as there is a fix.
  Without GPS, the PC sets it when uploading data or with `settime`. The
  oscillator drifts a few seconds a day, so resynchronise if you have gone
  days without GPS.
- **Position**: in `Auto`, GPS is used if it has a fix, otherwise the file's
  QTH (the locator you gave on the PC). The bottom bar shows
  `QTH IM98ib (GPS|DB) UTC:GPS|PC`.

## 4. Example: SO-50

1. In the list, SO-50 shows, for example, `10:27 in 3h21m · max 35deg
   187>041`.
2. A few minutes before AOS the alert sounds and the icon blinks. Press
   `MENU` to start tracking.
3. Point the antenna towards the AOS (187°, south). With the satellite in
   view, the downlink moves by itself from +10 kHz to −10 kHz.
4. Press `*` (`CTCSS 74.4 ARM!`) and hold PTT for 2 seconds to arm the
   10-minute timer.
5. Then transmit normally (CTCSS 67.0). Talk little and listen a lot.

## 5. Recommendations

- Use a directional antenna (Arrow or Elk) or at least a good external
  antenna. With the rubber duck only high passes are workable.
- The ISS and PO-101 transponders run on a schedule. Check their status on
  AMSAT.
- Respect local band plans. The 2 m and 70 cm satellite sub-bands are
  reserved for the amateur-satellite service.
