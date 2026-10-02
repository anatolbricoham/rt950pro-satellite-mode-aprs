# Preloaded country codeplugs

*[Versión en español](README.md)*

BricoHams RT-950 Toolkit (Windows/Linux), the Android app and the web
programmer load a ready-to-use configuration into the radio's 10 zones. They
work for a new radio or an already configured one:

- **Overwrite**: replaces all channels.
- **Add**: keeps your channels; the country channels go into the free slots
  of each zone and the ones you already have (same RX, TX and tone) are
  skipped.

A country codeplug made from scratch **only writes channels and zone names**:
radio settings, APRS and calibration are not touched (see D-46).

## Spain

| Zone | Name | Contents |
|---|---|---|
| 1–9 | EA1 … EA9 | 2 m and 70 cm analogue FM repeaters of the call district (official URE list, named `ED4YAD V` / `ED4YAN U`), and the frequencies of the district's airports and aerodromes: tower, approach, ATIS, ground and information (`LEMD TWR`, `LEMD ATIS`…), **RX only, AM** |
| 10 | PMR-CB | PMR446 channels 1–16 (446.00625–446.19375 MHz, narrow) and CB-27 channels 1–40 (26.965–27.405 MHz, AM), **RX only** |

Districts (URE / URVAG): EA1 Galicia, Asturias, Cantabria, Castile and León
and La Rioja · EA2 Basque Country, Navarre and Aragon · EA3 Catalonia · EA4
Madrid, Castile-La Mancha (except Albacete) and Extremadura · EA5 Valencia,
Murcia and Albacete · EA6 Balearic Islands · EA7 Andalusia · EA8 Canary
Islands · EA9 Ceuta and Melilla.

Release 0.3.0 contents: 455 channels (EA1 86, EA2 38, EA3 46, EA4 51, EA5 51,
EA6 24, EA7 57, EA8 42, EA9 4, PMR-CB 56). Every zone fits in 99 channels.

## United Kingdom

| Zone | Name | Contents |
|---|---|---|
| 1 | G SE LONDON | ETCC South East region |
| 2 | G SW+CI | South West and Channel Islands (GJ, GU) |
| 3 | G MIDLANDS | Central |
| 4 | G EAST ANGLIA | East Anglia |
| 5 | G NORTH+GD | North and Isle of Man (GD) |
| 6 | GW WALES | Wales & Marches |
| 7 | GM SCOTLAND | Scotland |
| 8 | GI N.IRELAND | Northern Ireland |
| 9 | UK SIMPLEX | 2 m and 70 cm calling and simplex (IARU R1), APRS 144.800, ISS voice and APRS |
| 10 | PMR-CB | PMR446, UK CB 27/81 (27.60125–27.99125 MHz FM) and CEPT CB 1–40 (FM), **RX only** |

Zones 1–8: operational 2 m and 70 cm analogue FM repeaters from the
ukrepeater.net list (RSGB ETCC; digital-only machines, out-of-service ones,
simplex gateways and `-L` link transmitters are left out), named
`GB3AA Brist` (call sign + town), and airports with traffic or a tower (RX
only, AM).

Some regions have more than 99 candidates (SE 113, SW 142, Midlands 113,
North 121). The ones closest to **your locator** are kept; without a locator,
the ones closest to the middle of the region, favouring main airports. The
preview shows which do not fit.

## Use

| Where | How |
|---|---|
| Windows / Linux | **Tools → Country codeplug…** |
| Android / web | **Country** tab |
| Command line | `RT950Toolkit country GB -o uk.rt950 [--locator IO91WM]` · `RT950Toolkit country GB -i radio.rt950 --mode append -o radio+uk.rt950` |
| Ready-made files | In every release: `codeplug-GB-x.y.z.rt950`, `-chirp.csv`, `-rt950editor.csv`, `-zones.csv` (and the same for ES) |

For an already configured radio: **Read from radio → Country codeplug → Add →
Write to radio**.

## Data and updates

| Source | Licence / use | File |
|---|---|---|
| [OurAirports](https://ourairports.com/data/) (airports and frequencies) | Public domain | `tools/country_sources/ourairports_*.csv` |
| [URE — Repetidores y balizas](https://www.ure.es/repetidores/) | Data published by URE | `tools/country_sources/ure_144_*.txt`, `ure_432_*.txt` |
| [ukrepeater.net](https://ukrepeater.net/csvfiles.html) (RSGB ETCC) | Data published by the ETCC | `tools/country_sources/ukrepeater_net_*.txt` |

`tools/build_country_data.py` builds `pc/rt950_toolkit/data/countries/*.json`
from those lists and `tools/export_web_meta.py` copies them to the app. The
lists were copied on 2 October 2026 and checked with a checksum to be
identical to the published ones.

Conversion rules:

- Air band frequencies published as **8.33 kHz channel designators** (ending
  in 5, 10 or 15 kHz within each 25 kHz block) are converted to the real
  carrier frequency.
- Per airport: up to 6 frequencies for large airports, 4 for medium ones and
  2 for aerodromes, in the order tower, information, approach, ATIS, ground.
- VHF air band only (108–137 MHz); no UHF military, operations or emergency
  frequencies.
- Public lists contain errors (e.g. an impossible URE locator); those
  repeaters are still loaded, without a position.

The lists change: check the repeaters and frequencies in your area before
use.
