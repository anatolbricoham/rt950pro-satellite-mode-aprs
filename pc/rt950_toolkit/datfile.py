"""
datfile.py - Open / save the maker's CPS codeplug files (.dat)

The CPS stores a .NET BinaryFormatter graph (KDH.RadioData with channel
list, VFOs, function settings, DTMF, FM/AM/SSB memories and APRS).  Reading
maps every member onto the radio memory image; saving patches the values of
an existing .dat (the opened file, or any CPS .dat used as a template), so
the structure the CPS expects is kept exactly.

Member -> memory mapping verified against RT-950/950Pro Editor (KK4OXN).
"""

from __future__ import annotations

from typing import List, Optional

from . import codeplug as C
from .nrbf import Stream

# FunConfigData member -> byte offset in the 0x9000 block
FUN_CONFIG = ["cbB_SQL", "cbB_SaveMode", "cbB_VOX", "cbB_AutoBacklight", "cbB_TDR", "cbB_TOT",
              "cbB_BeepPrompt", "cbB_VoicePrompt", "cbB_Language", "cbB_DTMF", "cbB_Scan", "cbB_PTTID",
              "cbB_SendIDDelay", "cbB_DisplayModeA", "cbB_DisplayModeB", "cbB_DisplayModeC", "cbB_AutoKeyLock",
              "cbB_AlarmMode", "cbB_AlarmSound", None, "cbB_TailNoiseClear", "cbB_PassRepetNoiseClear",
              "cbB_PassRepetNoiseDetect", "cbB_SoundTxEnd", None, "cbB_FMRadio", "cbB_WorkModeA", "cbB_WorkModeB",
              "cbB_WorkModeC", "cbB_BTWriteSwitch", "cbB_RTone", None, "cbB_VoxDelay", "cbB_MenuExitTime", None,
              None, "cbB_PowerOnDelayTime", None, None, "cbB_SubaudioScanSave", "cbB_PowerMsg", "cbB_KeySide1",
              "cbB_KeySide1L", "cbB_KeySide2", "cbB_KeySide2L", "cbB_CurWorkZoneA", "cbB_CurWorkZoneB",
              "cbB_CurWorkZoneC", None, None, None, None, None, None, None, None, None, "cbB_ABUVTransfer",
              "cbB_SoundTransfer", "cbB_Key0L", "cbB_Key1L", "cbB_Key2L", "cbB_Key3L", "cbB_Key4L", "cbB_Key5L",
              "cbB_Key6L", "cbB_Key7L", "cbB_Key8L", "cbB_Key9L", "cbB_FMRxInterruption", "cbB_BreathingLight",
              "cbB_NoaaAlarm", "cbB_FMBacklight"]

# APRSData member -> offset in the APRS page (fields at 0xFFFF + offset)
APRS_BYTES = {"cbB_AprsSwitch": 0, "cbB_GpsSwitch": 1, "cbB_LatitudeLongitudeUnit": 2, "cbB_SpeedUnit": 3,
              "cbB_DistanceUnit": 4, "cbB_AltitudeUnit": 5, "cbB_TimeZone": 6, "nUD_LatitudeMinute": 8,
              "nUD_LatitudeDegree": 9, "nUD_LatitudeSecond": 10, "nUD_LongitudeMinute": 12,
              "nUD_LongitudeDegree": 13, "nUD_LongitudeSecond": 14, "cbB_SSID": 23, "cbB_RoutingSelect": 24,
              "cbB_SiteType": 25, "cbB_RadioSymbol": 26, "cbB_UserDefinedIcon": 27, "cbB_AprsWorkingCH": 28,
              "cbB_AprsPriority": 29, "cbB_DataTxDelay": 30, "cbB_AprsCHMute": 31, "cbB_AprsDecodePromptTone": 32,
              "cbB_AprsRxAutoPopUp": 33, "cbB_BeaconTxType": 34, "cbB_TimedBeaconTime": 35, "cbB_MicEType": 36,
              "cbB_TncDataType": 37, "cbB_AprsForwardChannel": 38, "cbB_AprsForwardRouting": 39,
              "cbB_AprsWaitForward": 40, "cbB_CustomRoutingOneSSID": 49, "cbB_CustomRoutingTwoSSID": 56,
              "cbB_SendCustomMessages": 78, "cbB_AprsTxDataReporting": 119, "cbB_BeaconPopUpTime": 120}
APRS_TEXT = {"tB_CallSign": (17, 6), "tB_CustomRoutingOne": (43, 6), "tB_CustomRoutingTwo": (50, 6),
             "tB_CustomMessages": (79, 40)}
APRS_BASE = 0xFFFF

SCALAR = (int, float, bool)


def _list_items(s: Stream, obj) -> list:
    """List<T> or T[] -> python list of member objects."""
    obj = s.deref(obj) if isinstance(obj, dict) else obj
    if obj is None:
        return []
    if obj.get("rt") in (7, 15, 16, 17):
        return [s.deref(x) for x in obj["items"]]
    names = s.members(obj)
    if "_items" in names:
        items = s.get(obj, "_items")
        size = s.get(obj, "_size")
        return [s.deref(x) for x in items["items"][:size]]
    raise ValueError("not a list")


def _int(v, default=0):
    try:
        return int(str(v).strip() or default)
    except (TypeError, ValueError):
        return default


class DatFile:
    def __init__(self, path: str):
        with open(path, "rb") as f:
            self.raw = f.read()
        self.s = Stream(self.raw)
        roots = self.s.find_class("RadioData")
        if not roots:
            raise ValueError("not an RT-950 CPS file (KDH.RadioData missing)")
        self.root = roots[0]
        self.path = path

    def part(self, name):
        return self.s.get(self.root, name)

    # ------------------------------------------------------------ reading
    def channels(self) -> List[dict]:
        cd = self.part("channelData")
        out = []
        for o in _list_items(self.s, self.s.get(cd, "channelList")):
            out.append({k: self.s.get(o, k) for k in self.s.members(o)} if o else {})
        return out

    def zone_names(self) -> List[str]:
        cd = self.part("channelData")
        arr = self.s.get(cd, "arrayZoneName")
        return [self.s.value(x) or "" for x in arr["items"]] if arr else []

    def to_codeplug(self) -> C.Codeplug:
        cp = C.Codeplug()
        cp.meta = {"model": "RT-950 Pro", "source": "dat", "file": self.path}
        cp.mark_valid()
        for i, d in enumerate(self.channels()[:C.CHANNELS]):
            rx = str(d.get("rxFreq") or "").strip()
            if not rx:
                continue
            try:
                rxhz = int(round(float(rx) * 1e6))
                tx = str(d.get("txFreq") or "").strip()
                txhz = int(round(float(tx) * 1e6)) if tx else rxhz
            except ValueError:
                continue
            ch = C.Channel(i, rxhz, txhz, str(d.get("rxQT") or "OFF"), str(d.get("txQT") or "OFF"),
                           C.POWER[_int(d.get("txPower")) % 3], C.BANDWIDTH[_int(d.get("bandWide")) & 1],
                           C.SCRAMBLE[min(8, _int(d.get("scram")))], C.OFF_ON[_int(d.get("busyLockout")) & 1],
                           C.OFF_ON[_int(d.get("scanAdd"), 1) & 1], C.ENCRYPTION[_int(d.get("encrypt")) & 3],
                           C.OFF_ON[_int(d.get("enableTx"), 1) & 1], "AM" if _int(d.get("rxModulation")) else "FM",
                           _int(d.get("signallingGroup")) + 1, C.PTT_ID[_int(d.get("pttId")) & 3],
                           str(d.get("chName") or "")[:12])
            cp.set_channel(ch)
            if _int(d.get("learnFHSS")):
                b = cp.peek(i * 32 + 15)
                cp.poke(i * 32 + 15, b | 0x80)
        for z, name in enumerate(self.zone_names()[:C.ZONES]):
            cp.set_zone_name(z, name)
        # VFO frequencies / offsets
        fm = self.part("freqModeData")
        for v, key in enumerate(("vfoA", "vfoB", "vfoC")):
            o = self.s.get(fm, key)
            if not o:
                continue
            try:
                cp.set_vfo_freq(v, int(round(float(self.s.get(o, "tB_RxFreq")) * 1e6)))
                cp.set_vfo_offset(v, int(round(float(self.s.get(o, "tB_OffsetFreq")) * 1e6)))
            except (TypeError, ValueError):
                pass
        # function settings
        fc = self.part("funConfigData")
        names = self.s.members(fc)
        for off, key in enumerate(FUN_CONFIG):
            if key and key in names:
                v = self.s.get(fc, key)
                if isinstance(v, SCALAR):
                    cp.poke(0x9000 + off, int(v) & 0xFF)
        if "cbB_LockKeyBoard" in names:
            cp.poke(0x9000 + 27, int(self.s.get(fc, "cbB_LockKeyBoard")) & 0xFF)
        # APRS
        ap = self.part("aprsData")
        an = self.s.members(ap)
        for key, off in APRS_BYTES.items():
            if key in an:
                cp.poke(APRS_BASE + off, _int(self.s.get(ap, key)) & 0xFF)
        for key, (off, ln) in APRS_TEXT.items():
            if key in an:
                cp.write(APRS_BASE + off, C.ascii_put(str(self.s.get(ap, key) or ""), ln))
        if "cbB_NorthSouthLatitude" in an:
            cp.poke(APRS_BASE + 7, ord("S") if _int(self.s.get(ap, "cbB_NorthSouthLatitude")) else ord("N"))
        if "cbB_EastWestLongitude" in an:
            cp.poke(APRS_BASE + 11, ord("E") if _int(self.s.get(ap, "cbB_EastWestLongitude")) else ord("W"))
        if "nUD_Altitude" in an:
            alt = _int(self.s.get(ap, "nUD_Altitude")) & 0xFFFF
            cp.write(APRS_BASE + 15, bytes([alt & 0xFF, alt >> 8]))
        # DTMF codes
        dt = self.part("dtmfData")
        fields = C.dtmf_fields()
        if dt:
            try:
                cp.set_dtmf_value(fields[0], str(self.s.get(dt, "tB_DTMFCurId") or ""))
                codes = _list_items(self.s, self.s.get(dt, "dtmfCodeGroup"))
                for f, code in zip(fields[1:], codes):
                    cp.set_dtmf_value(f, str(self.s.value(code) if not isinstance(code, dict) or code.get("rt") == 6 else ""))
            except Exception:
                pass
        cp.dirty_regions.clear()
        return cp

    # ------------------------------------------------------------ writing
    def update_from(self, cp: C.Codeplug):
        """Patch this .dat with the channels, zones, VFOs and APRS of cp."""
        cd = self.part("channelData")
        objs = _list_items(self.s, self.s.get(cd, "channelList"))
        for i, o in enumerate(objs[:C.CHANNELS]):
            if not o:
                continue
            ch = cp.channel(i)
            names = self.s.members(o)
            vals = {}
            if ch.empty:
                vals = {"rxFreq": "", "txFreq": "", "rxQT": "OFF", "txQT": "OFF", "chName": ""}
            else:
                vals = {"rxFreq": "%.5f" % (ch.rx_hz / 1e6), "txFreq": "%.5f" % ((ch.tx_hz or ch.rx_hz) / 1e6),
                        "rxQT": ch.rx_tone, "txQT": ch.tx_tone, "signallingGroup": ch.signal_code - 1,
                        "pttId": C.PTT_ID.index(ch.ptt_id), "txPower": C.POWER.index(ch.power),
                        "scram": C.SCRAMBLE.index(ch.scramble) if ch.scramble in C.SCRAMBLE else 0,
                        "bandWide": C.BANDWIDTH.index(ch.bandwidth), "encrypt": C.ENCRYPTION.index(ch.encryption),
                        "busyLockout": 1 if ch.busy_lock == "ON" else 0, "scanAdd": 1 if ch.scan_add == "ON" else 0,
                        "rxModulation": 1 if ch.rx_am == "AM" else 0, "enableTx": 1 if ch.tx_enable == "ON" else 0,
                        "chName": ch.name}
            for k, v in vals.items():
                if k in names:
                    cur = self.s.get(o, k)
                    if isinstance(cur, SCALAR) and not isinstance(cur, str):
                        self.s.set(o, k, int(v))
                    elif (cur or "") != str(v):
                        self.s.set(o, k, str(v))
        arr = self.s.get(cd, "arrayZoneName")
        if arr:
            for z in range(min(C.ZONES, len(arr["items"]))):
                nid = max(self.s.objects) + 1
                rec = {"rt": 6, "id": nid, "v": cp.zone_name(z)}
                self.s.objects[nid] = rec
                arr["items"][z] = rec
        ap = self.part("aprsData")
        an = self.s.members(ap)
        for key, off in APRS_BYTES.items():
            if key in an and not isinstance(self.s.get(ap, key), str):
                self.s.set(ap, key, cp.peek(APRS_BASE + off))
        for key, (off, ln) in APRS_TEXT.items():
            if key in an:
                new = C.ascii_get(cp.read(APRS_BASE + off, ln))
                if (self.s.get(ap, key) or "") != new:
                    self.s.set(ap, key, new)
        fc = self.part("funConfigData")
        names = self.s.members(fc)
        for off, key in enumerate(FUN_CONFIG):
            if key and key in names and isinstance(self.s.get(fc, key), int):
                self.s.set(fc, key, cp.peek(0x9000 + off))

    def save(self, path: str):
        with open(path, "wb") as f:
            f.write(self.s.dump())


def open_dat(path: str) -> C.Codeplug:
    return DatFile(path).to_codeplug()


def save_dat(cp: C.Codeplug, template: str, path: str):
    d = DatFile(template)
    d.update_from(cp)
    d.save(path)
