"""Minimal OSC-over-TCP framing used by the RODECaster Video (port 10024).

Each OSC packet is prefixed with a 4 byte little-endian length.
Standard library only.
"""

import struct


def _pad(b: bytes) -> bytes:
    return b + b"\0" * ((4 - len(b) % 4) % 4)


def _osc_string(s: str) -> bytes:
    return _pad(s.encode("utf-8") + b"\0")


def encode_message(address: str, *args) -> bytes:
    tags = ","
    data = b""
    for a in args:
        if isinstance(a, bool):
            tags += "T" if a else "F"
        elif isinstance(a, int):
            tags += "i"
            data += struct.pack(">i", a)
        elif isinstance(a, float):
            tags += "f"
            data += struct.pack(">f", a)
        elif isinstance(a, bytes):
            tags += "b"
            data += struct.pack(">i", len(a)) + _pad(a)
        else:
            tags += "s"
            data += _osc_string(str(a))
    return _osc_string(address) + _osc_string(tags) + data


def frame(packet: bytes) -> bytes:
    return struct.pack("<i", len(packet)) + packet


def _read_string(data: bytes, off: int):
    end = data.index(b"\0", off)
    s = data[off:end].decode("utf-8", errors="replace")
    off = end + 1
    off += (4 - off % 4) % 4
    return s, off


def decode_packet(data: bytes):
    """Returns a list of (address, [args]) tuples (bundles are flattened)."""
    if data.startswith(b"#bundle\0"):
        out = []
        off = 16
        while off + 4 <= len(data):
            (size,) = struct.unpack(">i", data[off:off + 4])
            off += 4
            out += decode_packet(data[off:off + size])
            off += size
        return out

    address, off = _read_string(data, 0)
    args = []
    if off < len(data) and data[off:off + 1] == b",":
        tags, off = _read_string(data, off)
        for t in tags[1:]:
            if t == "i":
                args.append(struct.unpack(">i", data[off:off + 4])[0]); off += 4
            elif t == "f":
                args.append(struct.unpack(">f", data[off:off + 4])[0]); off += 4
            elif t in "sS":
                s, off = _read_string(data, off); args.append(s)
            elif t == "b":
                (n,) = struct.unpack(">i", data[off:off + 4]); off += 4
                args.append(data[off:off + n]); off += n + (4 - n % 4) % 4
            elif t in "hdt":
                args.append(data[off:off + 8]); off += 8
            elif t == "T":
                args.append(True)
            elif t == "F":
                args.append(False)
            elif t in "NI":
                args.append(None)
            else:
                args.append(f"<unknown type {t}>")
                break
    return [(address, args)]


class FrameReader:
    """Feed raw TCP bytes, get complete OSC packets out."""

    def __init__(self):
        self.buf = b""

    def feed(self, data: bytes):
        self.buf += data
        packets = []
        while len(self.buf) >= 4:
            (n,) = struct.unpack("<i", self.buf[:4])
            if n <= 0:
                self.buf = self.buf[4:]
                continue
            if len(self.buf) < 4 + n:
                break
            packets.append(self.buf[4:4 + n])
            self.buf = self.buf[4 + n:]
        return packets
