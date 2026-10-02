"""
stock.py - Ready-made channel lists

Frequencies are public allocations; transmit is disabled where amateur
licences do not allow it.  Always check your local regulations.
"""

from __future__ import annotations

from typing import Dict, List

from .codeplug import Channel


def _ch(name, rx_mhz, tx_mhz=None, txen="ON", bw="Wide", am="FM", tone_tx="OFF", tone_rx="OFF", power="High"):
    rx = int(round(rx_mhz * 1e6))
    tx = int(round((tx_mhz if tx_mhz is not None else rx_mhz) * 1e6))
    return Channel(0, rx, tx, tone_rx, tone_tx, power, bw, "OFF", "OFF", "ON", "OFF", txen, am, 1, "OFF", name[:12])


def pmr446() -> List[Channel]:
    """PMR446 (EU): receive only on this radio (PMR446 requires an integral antenna)."""
    return [_ch("PMR %d" % (i + 1), 446.00625 + 0.0125 * i, txen="OFF", bw="Narrow") for i in range(16)]


def ham_simplex_eu() -> List[Channel]:
    """IARU Region 1 FM calling / simplex frequencies (2 m and 70 cm)."""
    out = [_ch("2M CALL", 145.500)]
    out += [_ch("S%d" % n, 145.000 + n * 0.025) for n in (16, 17, 18, 19, 21, 22, 23)]
    out += [_ch("APRS EU", 144.800), _ch("ISS APRS", 145.825)]
    out += [_ch("70CM CALL", 433.500)]
    out += [_ch("U%d" % n, 433.000 + n * 0.025) for n in (272, 274, 276, 278, 284, 288)]
    return out


def marine_rx() -> List[Channel]:
    """Marine VHF, receive only."""
    chans = [("M16 DIST", 156.800), ("M06", 156.300), ("M08", 156.400), ("M09", 156.450),
             ("M10", 156.500), ("M12", 156.600), ("M13", 156.650), ("M14", 156.700),
             ("M67", 156.375), ("M72", 156.625), ("M73", 156.675), ("M77", 156.875)]
    return [_ch(n, f, txen="OFF") for n, f in chans]


def air_rx() -> List[Channel]:
    """Aviation emergency / common frequencies, AM receive only."""
    return [_ch("AIR GUARD", 121.500, txen="OFF", am="AM"),
            _ch("AIR 123.45", 123.450, txen="OFF", am="AM"),
            _ch("AIR 122.80", 122.800, txen="OFF", am="AM")]


STOCK: Dict[str, callable] = {
    "PMR446 (RX only)": pmr446,
    "Ham simplex / calling (IARU R1)": ham_simplex_eu,
    "Marine VHF (RX only)": marine_rx,
    "Aviation (AM, RX only)": air_rx,
}
