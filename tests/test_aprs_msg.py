#!/usr/bin/env python3
"""
test_aprs_msg.py - APRS messaging: firmware codec/engine/UI (real C sources in
build_host/sim) checked against an independent AX.25 decoder and aprslib.
"""
import os, re, subprocess, sys
import aprslib

HERE = os.path.dirname(os.path.abspath(__file__))
B = os.path.join(HERE, "..", "build_host")
OUT = os.path.join(B, "aprs")


def crc_x25(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc ^ 0xFFFF


def decode_ax25(frame_hex, fcs_hex):
    f = bytes.fromhex(frame_hex)
    fcs = bytes.fromhex(fcs_hex)
    assert crc_x25(f) == fcs[0] | fcs[1] << 8, "FCS mismatch"
    calls, i = [], 0
    while True:
        a = f[i:i + 7]
        call = "".join(chr(c >> 1) for c in a[:6]).strip()
        ssid = (a[6] >> 1) & 15
        calls.append(call + ("-%d" % ssid if ssid else ""))
        i += 7
        if a[6] & 1:
            break
    assert f[i] == 0x03 and f[i + 1] == 0xF0, "not a UI frame"
    info = f[i + 2:].decode("ascii")
    dest, src, path = calls[0], calls[1], calls[2:]
    return "%s>%s%s:%s" % (src, dest, "".join("," + p for p in path), info)


def main():
    os.makedirs(OUT, exist_ok=True)
    assert crc_x25(b"123456789") == 0x906E   # CRC-16/X-25 check value
    fails = 0
    script = "\n".join([
        "SEND EB5XYZ-7 Hola desde el RT-950 Pro",
        "DUMP",
        "RX EB5XYZ-7 :EA7ABC-7 :ack{ID}",
        "DUMP",
        "SEND EA1QQQ Sin respuesta",
        "WAIT 300000",
        "DUMP",
        "RX EB5XYZ :EA7ABC-7 :Prueba de mensaje{42",
        "WAIT 3500",
        "RX EB5XYZ :EA7ABC-7 :Prueba de mensaje{42",
        "WAIT 3500",
        "RX EA3ZZZ :EA1QQQ   :not for me{7",
        "RX EA4BBB :BLN1     :Net 145.500 tonight 21h",
        "SEND EB5XYZ bad {text",
        "DUMP",
        "UI",
        "DUMP",
    ])
    # run once to learn the first message id, then substitute it
    sim = [os.path.join(B, "sim"), "aprs", os.path.join(OUT, "screen")]
    first = subprocess.run(sim, input="SEND EB5XYZ-7 Hola desde el RT-950 Pro\nDUMP\n", capture_output=True, text=True).stdout
    mid = re.search(r"MSG 0 peer=\S+ id=(\d+)", first).group(1)
    out = subprocess.run(sim, input=script.replace("{ID}", mid) + "\n", capture_output=True, text=True, timeout=120).stdout
    tx = [decode_ax25(a, b) for a, b in re.findall(r"^TX ([0-9A-F]+) ([0-9A-F]{4})$", out, re.M)]
    parsed = [aprslib.parse(t) for t in tx]
    for t in tx:
        print("  TX", t)

    def check(cond, what):
        nonlocal fails
        print("  %-62s %s" % (what, "ok" if cond else "FAIL"))
        fails += 0 if cond else 1

    p0 = parsed[0]
    check(p0["from"] == "EA7ABC-7" and p0["to"] == "APZ950" and p0["path"] == ["WIDE1-1"], "outgoing frame addresses (src, tocall, path)")
    check(p0["format"] == "message" and p0["addresse"] == "EB5XYZ-7" and p0["message_text"] == "Hola desde el RT-950 Pro" and p0["msgNo"] == mid,
          "outgoing message decoded by aprslib (text + id)")
    dumps = out.split("UNREAD")
    check(re.search(r"MSG 0 peer=EB5XYZ-7 id=%s flags=04" % mid, dumps[1]) is not None, "ack received -> message marked DELIVERED")
    retr = [p for p in parsed if p.get("addresse") == "EA1QQQ"]
    check(len(retr) == 4, "unanswered message: 1 + 3 retries transmitted (got %d)" % len(retr))
    check(re.search(r"peer=EA1QQQ id=\d+ flags=08 tries=4", dumps[2]) is not None, "retries exhausted -> NOT ACKED")
    acks = [p for p in parsed if p.get("response") == "ack" and p.get("addresse") == "EB5XYZ"]
    check(len(acks) == 2 and all(a["msgNo"] == "42" for a in acks), "incoming message acked automatically (also the duplicate)")
    d3 = dumps[3]
    check(d3.count("text=Prueba de mensaje") == 1, "duplicate (digipeated) copy stored only once")
    check("not for me" not in d3, "traffic for other stations ignored")
    check(re.search(r"peer=EA4BBB id= flags=13 .*text=Net 145.500 tonight 21h", d3) is not None, "bulletin stored")
    check("RET -1" in out, "invalid text ('{') rejected")
    replies = [p for p in parsed if p.get("message_text") == "QSL 73 DE EA7ABC"]
    check(len(replies) == 1, "UI: reply typed with multi-tap and sent")
    check(os.path.exists(os.path.join(OUT, "screen_3_compose.ppm")), "UI screens rendered")
    try:
        from PIL import Image
        for f in sorted(os.listdir(OUT)):
            if f.endswith(".ppm"):
                im = Image.open(os.path.join(OUT, f))
                im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(os.path.join(OUT, f[:-4] + ".png"))
    except ImportError:
        pass
    print("RESULT:", "PASS" if fails == 0 else "FAIL (%d)" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
