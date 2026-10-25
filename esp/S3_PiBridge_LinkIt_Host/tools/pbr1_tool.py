#!/usr/bin/env python3
"""PBR1 control-frame tool for the ESP32 PiBridge to Orange Pi link.

Subcommands:
  listen PORT [SECONDS]         decode frames arriving on a serial port
  dump PORT SECONDS             print raw bytes, so log text and noise are visible
  send PORT JSON [options]      send one command frame and show the reply
  verify PORT                   drain, round-trip a command, then reset and listen
  decode FILE                   decode a raw capture file
  encode JSON [options]         print one encoded frame as hex
  selftest                      check the codec against known bytes, no hardware

Serial defaults match the specification: 115200 8N1, no flow control.

Examples:
  pbr1_tool.py listen COM17 8
  pbr1_tool.py dump COM17 8
  pbr1_tool.py send COM17 '{"command":"hello"}'
  pbr1_tool.py verify COM17
  pbr1_tool.py encode '{"command":"ping"}' --direction 0 --kind 2 --seq 1
  pbr1_tool.py decode capture.bin
  pbr1_tool.py selftest
"""

import argparse
import json
import sys
import time

MAGIC = b"PBR1"
HEADER_LEN = 14
MAX_PAYLOAD = 2048

EVENT, COMMAND, ERROR = 1, 2, 3
KIND_NAMES = {EVENT: "event", COMMAND: "command", ERROR: "error"}


def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def encode(direction, kind, sequence, payload):
    if not 0 <= direction <= 1:
        raise ValueError("direction must be 0 or 1")
    if kind not in (EVENT, COMMAND, ERROR):
        raise ValueError("kind must be 1, 2 or 3")
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload exceeds %d bytes" % MAX_PAYLOAD)
    frame = bytearray(MAGIC)
    frame.append(direction)
    frame.append(kind)
    frame += sequence.to_bytes(4, "little")
    frame += len(payload).to_bytes(2, "little")
    frame += payload
    frame += crc16(bytes(frame[4:])).to_bytes(2, "little")
    return bytes(frame)


class Frame:
    __slots__ = ("direction", "kind", "sequence", "payload", "raw", "crc_ok")

    def __init__(self, direction, kind, sequence, payload, raw, crc_ok):
        self.direction = direction
        self.kind = kind
        self.sequence = sequence
        self.payload = payload
        self.raw = raw
        self.crc_ok = crc_ok

    def describe(self):
        try:
            text = self.payload.decode("utf-8")
            json.loads(text)
        except (UnicodeDecodeError, ValueError):
            text = self.payload.decode("utf-8", "replace")
        return ("direction=%d kind=%s seq=%d len=%d crc=%s payload=%s"
                % (self.direction, KIND_NAMES.get(self.kind, self.kind),
                   self.sequence, len(self.payload),
                   "ok" if self.crc_ok else "BAD", text))


def iter_frames(buffer):
    """Yield Frames from a bytearray, consuming what it can.

    Handles sticky packets, leading noise, half frames, and resynchronises
    byte-by-byte after a bad CRC.
    """
    while True:
        start = buffer.find(MAGIC)
        if start < 0:
            keep = len(MAGIC) - 1
            if len(buffer) > keep:
                del buffer[: len(buffer) - keep]
            return
        del buffer[:start]
        if len(buffer) < HEADER_LEN:
            return
        length = buffer[10] | (buffer[11] << 8)
        if length > MAX_PAYLOAD:
            del buffer[:1]
            continue
        total = HEADER_LEN + length
        if len(buffer) < total:
            return
        raw = bytes(buffer[:total])
        expected = raw[12 + length] | (raw[13 + length] << 8)
        ok = crc16(raw[4:12 + length]) == expected
        yield Frame(raw[4], raw[5], int.from_bytes(raw[6:10], "little"),
                    raw[12:12 + length], raw, ok)
        del buffer[:total]


def open_port(path):
    try:
        import serial
    except ImportError:
        sys.exit("pyserial is required: pip install pyserial")
    handle = serial.Serial(path, 115200, timeout=0.1)
    # Do not hold the target in reset.
    handle.dtr = False
    handle.rts = False
    return handle


def drain(handle, seconds):
    deadline = time.time() + seconds
    buffer = bytearray()
    while time.time() < deadline:
        buffer += handle.read(4096)
    return list(iter_frames(buffer))


def print_frames(frames, prefix="  "):
    if not frames:
        print(prefix + "(no frame)")
        return
    for frame in frames:
        print(prefix + frame.describe())
        print(prefix + "  hex=" + frame.raw.hex())


def cmd_listen(args):
    with open_port(args.port) as handle:
        started = time.time()
        buffer = bytearray()
        while time.time() - started < args.seconds:
            buffer += handle.read(4096)
            for frame in iter_frames(buffer):
                print("%7.3fs  %s" % (time.time() - started, frame.describe()))
                print("            hex=" + frame.raw.hex())
    return 0


def cmd_send(args):
    payload = args.json.encode("utf-8")
    frame = encode(args.direction, args.kind, args.seq, payload)
    with open_port(args.port) as handle:
        print("sent: " + frame.hex())
        handle.write(frame)
        handle.flush()
        if args.raw:
            deadline = time.time() + args.wait
            total = 0
            while time.time() < deadline:
                chunk = handle.read(4096)
                if not chunk:
                    continue
                total += len(chunk)
                print(chunk.hex(" "))
                print("|%s|" % "".join(chr(b) if 32 <= b < 127 else "." for b in chunk))
            print("total bytes: %d" % total)
            return 0
        print("reply:")
        print_frames(drain(handle, args.wait))
    return 0


def cmd_dump(args):
    """Raw view of the port. Decoding hides noise by design; this does not."""
    with open_port(args.port) as handle:
        started = time.time()
        total = 0
        while time.time() - started < args.seconds:
            chunk = handle.read(4096)
            if not chunk:
                continue
            total += len(chunk)
            print("%7.3fs  %s" % (time.time() - started, chunk.hex(" ")))
            text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
            print("            |%s|" % text)
    print("total bytes: %d" % total)
    return 0


def cmd_verify(args):
    with open_port(args.port) as handle:
        print("[1] drain existing bytes for %.1fs" % args.wait)
        print_frames(drain(handle, args.wait))

        payload = b'{"command":"hello"}'
        frame = encode(0, COMMAND, 1, payload)
        print("[2] send control frame: " + frame.hex())
        handle.write(frame)
        handle.flush()
        print("    reply (expect kind=error, 'unsupported control command'):")
        print_frames(drain(handle, 1.5))

        if args.no_reset:
            return 0
        print("[3] pulse reset, then listen %.1fs" % args.reset_wait)
        handle.dtr = False
        handle.rts = True
        time.sleep(0.15)
        handle.rts = False
        print("    boot frames:")
        print_frames(drain(handle, args.reset_wait))
    return 0


def cmd_decode(args):
    with open(args.file, "rb") as handle:
        data = handle.read()
    frames = list(iter_frames(bytearray(data)))
    print_frames(frames, prefix="")
    print("total frames: %d, bytes: %d" % (len(frames), len(data)))
    return 0


def cmd_encode(args):
    frame = encode(args.direction, args.kind, args.seq, args.json.encode("utf-8"))
    print(frame.hex())
    return 0


def cmd_selftest(args):
    failures = []

    # Synthetic fixture with the structure of a captured boot detach frame;
    # the instance id and CRC are regenerated placeholders, not real data.
    known = bytes.fromhex(
        "504252310101010000004800"
        "7b226576656e74223a22646574616368222c226d6f64756c655f6964223a313238"
        "2c226d6f64756c655f696e7374616e6365223a22686f73742d3030313132323333"
        "34343535227d"
        "0d49")
    decoded = list(iter_frames(bytearray(known)))
    if len(decoded) != 1:
        failures.append("known frame did not decode to exactly one frame")
    else:
        frame = decoded[0]
        checks = [
            (frame.direction == 1, "direction"),
            (frame.kind == EVENT, "kind"),
            (frame.sequence == 1, "sequence"),
            (len(frame.payload) == 72, "payload length"),
            (frame.crc_ok, "crc"),
            (json.loads(frame.payload)["event"] == "detach", "json event"),
        ]
        for ok, label in checks:
            if not ok:
                failures.append("known frame mismatch: " + label)

    # A command frame must re-encode byte-for-byte.
    hello = encode(0, COMMAND, 1, b'{"command":"hello"}')
    if hello.hex() != "5042523100020100000013007b22636f6d6d616e64223a2268656c6c6f227dc7bd":
        failures.append("command frame encoding mismatch: " + hello.hex())

    # Noise, a bad CRC frame, and a valid frame after it: the decoder must skip
    # the noise, report the bad CRC, and still recover the valid frame.
    bad = bytearray(encode(1, ERROR, 9, b"unsupported control command"))
    bad[-1] ^= 0xFF
    stream = (b"\x00\xff\x13" + hello + b"junk"
              + bytes(bad)
              + encode(1, EVENT, 5, b'{"event":"attach","module_id":128,"module_instance":"host-x"}'))
    recovered = list(iter_frames(bytearray(stream)))
    kinds = [(f.kind, f.crc_ok) for f in recovered]
    if kinds != [(COMMAND, True), (ERROR, False), (EVENT, True)]:
        failures.append("recovery sequence mismatch: %r" % (kinds,))

    if failures:
        print("FAIL")
        for item in failures:
            print("  - " + item)
        return 1
    print("PASS: codec round-trip, known capture, and resync after bad CRC")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("listen", help="decode frames arriving on a serial port")
    p.add_argument("port")
    p.add_argument("seconds", nargs="?", type=float, default=8.0)
    p.set_defaults(func=cmd_listen)

    p = sub.add_parser("send", help="send one command frame and show the reply")
    p.add_argument("port")
    p.add_argument("json")
    p.add_argument("--direction", type=int, default=0)
    p.add_argument("--kind", type=int, default=COMMAND)
    p.add_argument("--seq", type=int, default=1)
    p.add_argument("--wait", type=float, default=1.5)
    p.add_argument("--raw", action="store_true",
                   help="print raw bytes instead of decoded frames")
    p.set_defaults(func=cmd_send)

    p = sub.add_parser("dump", help="print raw port bytes, including any log text")
    p.add_argument("port")
    p.add_argument("seconds", type=float)
    p.set_defaults(func=cmd_dump)

    p = sub.add_parser("verify", help="drain, round-trip, reset, listen")
    p.add_argument("port")
    p.add_argument("--wait", type=float, default=0.6)
    p.add_argument("--reset-wait", type=float, default=2.5)
    p.add_argument("--no-reset", action="store_true",
                   help="skip the reset pulse")
    p.set_defaults(func=cmd_verify)

    p = sub.add_parser("decode", help="decode a raw capture file")
    p.add_argument("file")
    p.set_defaults(func=cmd_decode)

    p = sub.add_parser("encode", help="print one encoded frame as hex")
    p.add_argument("json")
    p.add_argument("--direction", type=int, default=0)
    p.add_argument("--kind", type=int, default=COMMAND)
    p.add_argument("--seq", type=int, default=1)
    p.set_defaults(func=cmd_encode)

    p = sub.add_parser("selftest", help="check the codec without hardware")
    p.set_defaults(func=cmd_selftest)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
