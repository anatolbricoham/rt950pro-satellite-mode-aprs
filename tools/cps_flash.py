#!/usr/bin/env python3
"""
cps_flash.py - CPS serial programming tool for the Radtel RT-950 Pro

Communicates with the radio over UART using the real CPS protocol
(reverse engineered from BT-RT950PRO_CPS.exe).

Protocol: raw byte commands with XOR encryption, NO framing or CRC.
Handshake: "PROGRAMBT9000U" followed by encryption key negotiation.

Usage:
    python3 cps_flash.py info    /dev/ttyUSB0
    python3 cps_flash.py regions
    python3 cps_flash.py read    /dev/ttyUSB0 output.bin --region channels
    python3 cps_flash.py read    /dev/ttyUSB0 output.bin --addr 0x0000 --len 0x10000
    python3 cps_flash.py write   /dev/ttyUSB0 input.bin  --region vfo_config
    python3 cps_flash.py write   /dev/ttyUSB0 input.bin  --addr 0x8000
    python3 cps_flash.py dump    /dev/ttyUSB0 output_dir/

Requires: pyserial (pip install pyserial)
"""

import argparse
import os
import random
import sys
import time

try:
    import serial
except ImportError:
    print("Error: pyserial required. Install with: pip install pyserial",
          file=sys.stderr)
    sys.exit(1)


# Protocol constants
HANDSHAKE_STRING = b"PROGRAMBT9000U"
ACK_BYTE         = 0x06
CMD_READ         = 0x52   # 'R'
CMD_WRITE        = 0x57   # 'W'
CMD_WRITE_END    = 0x58   # 'X'
CMD_TERMINATE    = 0x45   # 'E' - end write session

BLOCK_SIZE       = 128    # max bytes per read/write transfer (0x80)

DEFAULT_BAUD     = 115200
DEFAULT_TIMEOUT  = 2.0

# 20-entry XOR key table (from CPS tblEncrySymbol)
XOR_KEY_TABLE = [
    b"BHT ", b"CO 7", b"A ES", b" EIY", b"M PQ",
    b"XN Y", b"RVB ", b" HQP", b"W RC", b"MS N",
    b" SAT", b"K DH", b"ZO R", b"C SL", b"6RB ",
    b" JCG", b"PN V", b"J PK", b"EK L", b"I LZ",
]

# Known CPS address regions (name -> (addr, size, description))
REGION_MAP = {
    "channels":        (0x0000, 31680, "Channel data (990 x 32 bytes)"),
    "vfo_config":      (0x8000,   128, "VFO config (3 VFOs x 32 + reserved)"),
    "system_config":   (0x9000,   128, "System config (3 parts)"),
    "ext_config_1":    (0xA000,   384, "Extended config 1"),
    "ext_config_2":    (0xB000,   256, "Extended config 2"),
    "dtmf_modulation": (0xC000,   256, "DTMF / modulation config"),
    "aprs_misc":       (0xD000,   768, "APRS / misc"),
    "cal_0xE000":      (0xE000,   128, "Calibration probe (0xE000)"),
    "cal_0xF000":      (0xF000,   128, "Calibration probe (0xF000)"),
    "cal_0xF200":      (0xF200,   128, "Calibration probe (0xF200)"),
}

# Ordered list for dump operations (name, addr, size)
CPS_REGIONS = [
    (0x0000, 31680, "channels"),
    (0x8000,   128, "vfo_config"),
    (0x9000,   128, "system_config"),
    (0xA000,   384, "ext_config_1"),
    (0xB000,   256, "ext_config_2"),
    (0xC000,   256, "dtmf_modulation"),
    (0xD000,   768, "aprs_misc"),
]

# Extended probe addresses (calibration, unknown)
PROBE_REGIONS = [
    (0xE000, 128, "cal_0xE000"),
    (0xF000, 128, "cal_0xF000"),
    (0xF200, 128, "cal_0xF200"),
]

REGION_NAMES = list(REGION_MAP.keys())


def xor_decrypt(data: bytes, key: bytes) -> bytes:
    """Decrypt data using the CPS XOR scheme.

    Rules (per byte):
      - Skip if key_byte == 0x20 (space)
      - Skip if byte == 0x00, 0xFF, key_byte, or key_byte ^ 0xFF
      - Otherwise: decrypted = byte ^ key[i % 4]
    """
    result = bytearray(len(data))
    for i, b in enumerate(data):
        k = key[i % 4]
        if k == 0x20 or b == 0x00 or b == 0xFF or b == k or b == (k ^ 0xFF):
            result[i] = b
        else:
            result[i] = b ^ k
    return bytes(result)


def xor_encrypt(data: bytes, key: bytes) -> bytes:
    """Encrypt data using the CPS XOR scheme.

    Rules (per byte):
      - Skip if key_byte == 0x20 (space)
      - Skip if byte == 0x00, 0xFF, key_byte, or key_byte ^ 0xFF
      - Otherwise: encrypted = byte ^ key[i % 4]
    """
    result = bytearray(len(data))
    for i, b in enumerate(data):
        k = key[i % 4]
        if k == 0x20 or b == 0x00 or b == 0xFF or b == k or b == (k ^ 0xFF):
            result[i] = b
        else:
            result[i] = b ^ k
    return bytes(result)


class RT950CPS:
    """RT-950 Pro CPS protocol handler.

    Implements the real protocol reverse engineered from the OEM CPS
    software (BT-RT950PRO_CPS.exe .NET assembly).
    """

    def __init__(self, port: str, baudrate: int = DEFAULT_BAUD,
                 timeout: float = DEFAULT_TIMEOUT, verbose: bool = False):
        self.ser = serial.Serial(
            port, baudrate=baudrate, timeout=timeout,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE
        )
        self.verbose = verbose
        self.model = None
        self.identity = None
        self.xor_key = None

    def close(self):
        """Close serial port and end session gracefully."""
        if self.ser and self.ser.is_open:
            try:
                self.ser.write(bytes([ACK_BYTE]))
                time.sleep(0.05)
            except Exception:
                pass
            self.ser.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def _log(self, msg: str):
        if self.verbose:
            print(f"  [dbg] {msg}", file=sys.stderr)

    def _read_exact(self, n: int, what: str = "data") -> bytes:
        """Read exactly n bytes or raise TimeoutError."""
        data = self.ser.read(n)
        if len(data) < n:
            raise TimeoutError(
                f"Timeout reading {what}: got {len(data)}/{n} bytes"
            )
        return data

    def handshake(self) -> str:
        """Perform the 5-state CPS handshake with encryption negotiation.

        Returns the model string (e.g. "RT-950").
        """
        self.ser.reset_input_buffer()
        self.ser.reset_output_buffer()

        # State 0: Send handshake string
        self._log(f"TX handshake: {HANDSHAKE_STRING}")
        self.ser.write(HANDSHAKE_STRING)

        # State 1: Expect ACK (0x06)
        ack = self._read_exact(1, "handshake ACK")
        if ack[0] != ACK_BYTE:
            raise RuntimeError(
                f"Handshake failed: expected ACK (0x06), got 0x{ack[0]:02X}"
            )
        self._log("RX ACK")

        # Send 'F' to request identity
        self.ser.write(b"F")

        # State 2: Receive 16-byte identity
        self.identity = self._read_exact(16, "identity")
        self._log(f"RX identity: {self.identity.hex()}")

        # Send 'M' to request model
        self.ser.write(b"M")

        # State 3: Receive 12-byte model string (null-padded)
        model_raw = self._read_exact(12, "model")
        self.model = model_raw.rstrip(b"\x00").decode("ascii", errors="replace")

        # Build encryption key negotiation packet
        self._negotiate_encryption()

        return self.model

    def _negotiate_encryption(self):
        """Send SEND + key selector + padding, derive XOR key.

        Packet format (25 bytes):
          [0:4]   "SEND"
          [4]     selector = (rand(1,2) << 4) | rand(0,4)
          [5:24]  19 random bytes, each in range 0-19 (table indices)
          [24]    0x00 (implicit zero)
        """
        # Key selector: (random(1,2) << 4) | random(0,4)
        hi = random.randint(1, 2)
        lo = random.randint(0, 4)
        selector = (hi << 4) | lo

        # 19 random padding bytes (range 0-19, used as table indices)
        # plus trailing zero byte = 20 padding bytes total
        padding = bytes([random.randint(0, 19) for _ in range(19)]) + b'\x00'

        # Build 25-byte packet: "SEND" (4) + selector (1) + padding (20)
        packet = b"SEND" + bytes([selector]) + padding
        assert len(packet) == 25

        # Derive key index from selector
        if selector & 0x20:
            index_encry = (selector - 0x20) * 2 + 1
        else:
            index_encry = (selector - 0x10) * 2
        index_encry += 1

        # The actual key table index is the byte at position
        # (4 + index_encry) in the packet (a direct table index, 0-19)
        key_idx = packet[4 + index_encry]
        self.xor_key = XOR_KEY_TABLE[key_idx]

        self._log(f"TX SEND packet, selector=0x{selector:02X}, "
                  f"index_encry={index_encry}, key_idx={key_idx}")

        self.ser.write(packet)

        # Expect ACK
        ack = self._read_exact(1, "encryption ACK")
        if ack[0] != ACK_BYTE:
            raise RuntimeError(
                f"Encryption handshake failed: expected ACK, got 0x{ack[0]:02X}"
            )
        self._log("Encryption established")

    def read_block(self, addr: int, length: int = BLOCK_SIZE) -> bytes:
        """Read a single block (up to 128 bytes) from flash.

        TX: {0x52, addr_hi, addr_lo, length}
        RX: {0x52, addr_hi, addr_lo, length, data[length]}
        """
        if length > BLOCK_SIZE:
            length = BLOCK_SIZE

        cmd = bytes([CMD_READ, (addr >> 8) & 0xFF, addr & 0xFF, length])
        self.ser.write(cmd)

        # Read response header (4 bytes) + data
        resp = self._read_exact(4 + length, f"read @0x{addr:04X}")

        # Verify echo
        if resp[0] != CMD_READ:
            raise RuntimeError(
                f"Read response cmd mismatch: 0x{resp[0]:02X} != 0x{CMD_READ:02X}"
            )
        resp_addr = (resp[1] << 8) | resp[2]
        if resp_addr != addr:
            raise RuntimeError(
                f"Read response addr mismatch: 0x{resp_addr:04X} != 0x{addr:04X}"
            )

        raw_data = resp[4:]

        # Decrypt if encryption is active
        if self.xor_key:
            return xor_decrypt(raw_data, self.xor_key)
        return raw_data

    def write_block(self, addr: int, data: bytes) -> None:
        """Write a single block (up to 128 bytes) to flash.

        TX: {0x57, addr_hi, addr_lo, length, encrypted_data[length]}
        RX: 0x06 (ACK)
        """
        length = len(data)
        if length > BLOCK_SIZE:
            raise ValueError(f"Write block too large: {length} > {BLOCK_SIZE}")

        # Encrypt data
        enc_data = xor_encrypt(data, self.xor_key) if self.xor_key else data

        cmd = bytes([CMD_WRITE, (addr >> 8) & 0xFF, addr & 0xFF, length])
        self.ser.write(cmd + enc_data)

        # Wait for ACK
        ack = self._read_exact(1, f"write ACK @0x{addr:04X}")
        if ack[0] != ACK_BYTE:
            raise RuntimeError(
                f"Write failed at 0x{addr:04X}: expected ACK, got 0x{ack[0]:02X}"
            )

    def write_end(self) -> None:
        """Send write-end sequence (0x45 termination)."""
        self.ser.write(bytes([CMD_TERMINATE]))
        time.sleep(0.1)
        self._log("Write session terminated")

    def read_flash(self, addr: int, length: int,
                   progress: bool = True) -> bytes:
        """Read an arbitrary length of flash memory in 128-byte blocks."""
        result = bytearray()
        offset = addr
        remaining = length

        while remaining > 0:
            chunk = min(remaining, BLOCK_SIZE)
            data = self.read_block(offset, chunk)
            result.extend(data)
            offset += chunk
            remaining -= chunk
            if progress:
                done = length - remaining
                pct = done * 100 // length
                bar_len = 30
                filled = done * bar_len // length
                bar = "#" * filled + "-" * (bar_len - filled)
                print(f"\r  [{bar}] {pct:3d}%  "
                      f"0x{addr:04X}-0x{addr + length - 1:04X}  "
                      f"{done}/{length}",
                      end="", flush=True)

        if progress:
            print()
        return bytes(result)

    def write_flash(self, addr: int, data: bytes,
                    progress: bool = True) -> None:
        """Write data to flash in 128-byte blocks.

        WARNING: This writes directly to the radio's configuration memory.
        Incorrect writes can misconfigure the radio.
        """
        total = len(data)
        offset = 0

        while offset < total:
            chunk_size = min(BLOCK_SIZE, total - offset)
            chunk = data[offset:offset + chunk_size]
            # Pad to BLOCK_SIZE if needed
            if len(chunk) < BLOCK_SIZE:
                chunk = chunk + b"\xff" * (BLOCK_SIZE - len(chunk))
            self.write_block(addr + offset, chunk)
            offset += chunk_size
            if progress:
                pct = offset * 100 // total
                bar_len = 30
                filled = offset * bar_len // total
                bar = "#" * filled + "-" * (bar_len - filled)
                print(f"\r  [{bar}] {pct:3d}%  {offset}/{total}",
                      end="", flush=True)

        if progress:
            print()

    def verify_flash(self, addr: int, data: bytes,
                     progress: bool = True) -> bool:
        """Read back flash and compare against expected data."""
        readback = self.read_flash(addr, len(data), progress=progress)
        if readback == data:
            return True
        # Find first mismatch
        for i in range(len(data)):
            if i < len(readback) and readback[i] != data[i]:
                print(f"  Verify FAILED at offset 0x{addr + i:04X}: "
                      f"expected 0x{data[i]:02X}, got 0x{readback[i]:02X}")
                break
        return False


def parse_int(s: str) -> int:
    """Parse an integer, supporting 0x hex prefix."""
    s = s.strip()
    if s.lower().startswith("0x"):
        return int(s, 16)
    return int(s)


def cmd_info(args):
    """Connect and display radio information."""
    with RT950CPS(args.port, baudrate=args.baud, timeout=args.timeout,
                  verbose=args.verbose) as cps:
        model = cps.handshake()
        print(f"Model:     {model}")
        print(f"Identity:  {cps.identity.hex()}")
        print(f"Port:      {args.port}")
        print(f"Baud:      {args.baud}")

        # Quick sanity read
        header = cps.read_block(0x0000, 16)
        print(f"Flash[0]:  {header.hex().upper()}")

        # Read model config area
        sys_cfg = cps.read_block(0x9000, 16)
        print(f"SysCfg[0]: {sys_cfg.hex().upper()}")

    print("Session closed.")


def cmd_read(args):
    """Read flash memory to a file."""
    if args.region:
        name = args.region
        if name not in REGION_MAP:
            print(f"Error: unknown region '{name}'. "
                  f"Use 'regions' command to list.", file=sys.stderr)
            sys.exit(1)
        addr, length, desc = REGION_MAP[name]
        print(f"Region: {name} - {desc}")
    else:
        addr = parse_int(args.addr)
        length = parse_int(args.length)

    if addr < 0 or addr > 0xFFFF:
        print(f"Error: address 0x{addr:04X} out of range (0x0000-0xFFFF)",
              file=sys.stderr)
        sys.exit(1)

    print(f"Reading {length} bytes (0x{length:X}) from 0x{addr:04X}...")

    with RT950CPS(args.port, baudrate=args.baud, timeout=args.timeout,
                  verbose=args.verbose) as cps:
        model = cps.handshake()
        print(f"Connected: {model}")

        data = cps.read_flash(addr, length)

    with open(args.output, "wb") as f:
        f.write(data)
    print(f"Saved {len(data)} bytes to {args.output}")


def cmd_write(args):
    """Write a file to flash memory."""
    with open(args.input, "rb") as f:
        data = f.read()

    if args.region:
        name = args.region
        if name not in REGION_MAP:
            print(f"Error: unknown region '{name}'. "
                  f"Use 'regions' command to list.", file=sys.stderr)
            sys.exit(1)
        addr, rsize, desc = REGION_MAP[name]
        print(f"Region: {name} - {desc}")
        if len(data) > rsize:
            print(f"WARNING: file ({len(data)} bytes) exceeds region "
                  f"size ({rsize} bytes)")
            if not args.force:
                resp = input("Continue anyway? [y/N] ")
                if resp.lower() != "y":
                    print("Aborted.")
                    return
    else:
        addr = parse_int(args.addr)

    # Safety: default write address is 0x8000 to avoid channel area
    print(f"Writing {len(data)} bytes (0x{len(data):X}) to 0x{addr:04X}...")
    end_addr = addr + len(data) - 1
    print(f"Target range: 0x{addr:04X} - 0x{end_addr:04X}")

    if not args.force:
        resp = input(f"This will overwrite flash at "
                     f"0x{addr:04X}-0x{end_addr:04X}. "
                     f"Continue? [y/N] ")
        if resp.lower() != "y":
            print("Aborted.")
            return

    with RT950CPS(args.port, baudrate=args.baud, timeout=args.timeout,
                  verbose=args.verbose) as cps:
        model = cps.handshake()
        print(f"Connected: {model}")

        print("Writing...")
        cps.write_flash(addr, data)

        if args.verify:
            print("Verifying...")
            if cps.verify_flash(addr, data):
                print("Verify OK.")
            else:
                sys.exit(1)

        cps.write_end()

    print("Write complete.")


def cmd_dump(args):
    """Dump all known CPS regions to individual files."""
    outdir = args.output
    os.makedirs(outdir, exist_ok=True)

    # Build region list from --region filter or defaults
    if args.region:
        regions = []
        for name in args.region:
            if name not in REGION_MAP:
                print(f"Error: unknown region '{name}'. "
                      f"Use 'regions' command to list.", file=sys.stderr)
                sys.exit(1)
            raddr, rsize, _ = REGION_MAP[name]
            regions.append((raddr, rsize, name))
    else:
        regions = CPS_REGIONS + (PROBE_REGIONS if args.probe else [])

    with RT950CPS(args.port, baudrate=args.baud, timeout=args.timeout,
                  verbose=args.verbose) as cps:
        model = cps.handshake()
        print(f"Connected: {model}")
        print(f"Identity:  {cps.identity.hex()}")
        print()

        # Save identity info
        with open(os.path.join(outdir, "identity.txt"), "w") as f:
            f.write(f"Model: {model}\n")
            f.write(f"Identity: {cps.identity.hex()}\n")

        # Build combined 64 KB image (0xFF fill for gaps)
        flash_image = bytearray(b"\xff" * 65536)

        # Read all known regions
        for raddr, rsize, rname in regions:
            fname = f"{rname}_0x{raddr:04X}.bin"
            fpath = os.path.join(outdir, fname)
            print(f"Reading {rname} (0x{raddr:04X}, {rsize} bytes)...")

            try:
                data = cps.read_flash(raddr, rsize)
                with open(fpath, "wb") as f:
                    f.write(data)
                flash_image[raddr:raddr + rsize] = data
                # Check if region is all 0xFF
                if all(b == 0xFF for b in data):
                    print(f"  -> {fname} (empty/0xFF)")
                else:
                    print(f"  -> {fname} ({rsize} bytes)")
            except (TimeoutError, RuntimeError) as e:
                print(f"  -> FAILED: {e}")

        # Save combined image
        combo_path = os.path.join(outdir, "flash_full.bin")
        with open(combo_path, "wb") as f:
            f.write(flash_image)
        print(f"\nCombined image: {combo_path} (65536 bytes)")

    print("Dump complete.")


def cmd_regions(args):
    """List all known flash regions."""
    print("Known CPS flash regions:\n")
    print(f"  {'Name':<18s} {'Address':>10s} {'Size':>7s}  Description")
    print(f"  {'-'*18} {'-'*10} {'-'*7}  {'-'*36}")
    for name, (addr, size, desc) in REGION_MAP.items():
        end = addr + size - 1
        print(f"  {name:<18s} 0x{addr:04X}-0x{end:04X} {size:>5d}B  {desc}")
    print()
    print("Use --region <name> with read, write, or dump commands.")
    print("Multiple regions can be given to dump: --region channels vfo_config")


def main():
    parser = argparse.ArgumentParser(
        prog="cps_flash",
        description="RT-950 Pro CPS flash tool - read/write radio memory"
    )

    # Global options
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD,
                        help=f"Serial baud rate (default: {DEFAULT_BAUD})")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT,
                        help=f"Serial timeout in seconds (default: {DEFAULT_TIMEOUT})")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Print protocol debug messages")

    sub = parser.add_subparsers(dest="command", required=True)

    # info
    p_info = sub.add_parser("info", help="Connect and show radio info")
    p_info.add_argument("port", help="Serial port (e.g. /dev/ttyUSB0)")

    # regions
    sub.add_parser("regions", help="List all known flash regions")

    # read
    p_read = sub.add_parser("read", help="Read flash to file")
    p_read.add_argument("port", help="Serial port")
    p_read.add_argument("output", help="Output file path")
    region_group = p_read.add_mutually_exclusive_group()
    region_group.add_argument("--region", choices=REGION_NAMES, metavar="NAME",
                              help="Named region to read (use 'regions' cmd to list)")
    region_group.add_argument("--addr", default="0x0000",
                              help="Start address (default: 0x0000)")
    p_read.add_argument("--len", dest="length", default="0x10000",
                        help="Bytes to read (default: 0x10000 = 64 KB)")

    # write
    p_write = sub.add_parser("write", help="Write file to flash")
    p_write.add_argument("port", help="Serial port")
    p_write.add_argument("input", help="Input file path")
    write_region = p_write.add_mutually_exclusive_group()
    write_region.add_argument("--region", choices=REGION_NAMES, metavar="NAME",
                              help="Named region to write (use 'regions' cmd to list)")
    write_region.add_argument("--addr", default="0x8000",
                              help="Start address (default: 0x8000, above channels)")
    p_write.add_argument("--force", "-f", action="store_true",
                        help="Skip confirmation prompt")
    p_write.add_argument("--verify", action="store_true",
                        help="Read back and verify after writing")

    # dump
    p_dump = sub.add_parser("dump", help="Dump all known regions to files")
    p_dump.add_argument("port", help="Serial port")
    p_dump.add_argument("output", help="Output directory")
    p_dump.add_argument("--region", nargs="+", choices=REGION_NAMES, metavar="NAME",
                        help="Specific region(s) to dump (default: all standard)")
    p_dump.add_argument("--probe", action="store_true",
                        help="Also read extended probe addresses (calibration)")

    args = parser.parse_args()

    try:
        if args.command == "info":
            cmd_info(args)
        elif args.command == "regions":
            cmd_regions(args)
        elif args.command == "read":
            cmd_read(args)
        elif args.command == "write":
            cmd_write(args)
        elif args.command == "dump":
            cmd_dump(args)
    except serial.SerialException as e:
        print(f"Serial error: {e}", file=sys.stderr)
        sys.exit(1)
    except TimeoutError as e:
        print(f"Timeout: {e}", file=sys.stderr)
        print("Check that the radio is powered on and in programming mode.",
              file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nAborted by user.")
        sys.exit(130)


if __name__ == "__main__":
    main()
