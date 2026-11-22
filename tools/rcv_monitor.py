#!/usr/bin/env python3
"""Connect to a RODECaster Video and print what the tally light would see.

Usage:
    python3 tools/rcv_monitor.py 192.168.1.50 [--all] [--xml show.xml]

- Sends /show and /remote (same as the firmware and the Companion module).
- Prints every received OSC message (meters are hidden unless --all).
- Saves the full /show XML dump to a file, so scene contents / attribute
  names can be inspected (useful for improving the firmware).
- Prints the resulting program / preview after every change.

Standard library only (Python 3.8+).
"""

import argparse
import socket
import sys
import time

from rcv_osc import FrameReader, decode_packet, encode_message, frame

HIDDEN = ("/meters/values",)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ip", help="RODECaster Video IP address")
    ap.add_argument("--port", type=int, default=10024)
    ap.add_argument("--all", action="store_true", help="also print meter messages")
    ap.add_argument("--xml", default="rcv_show.xml", help="file to save the /show XML dump to")
    ap.add_argument("--no-remote", action="store_true", help="do not send /remote")
    args = ap.parse_args()

    print(f"Connecting to {args.ip}:{args.port} ...")
    sock = socket.create_connection((args.ip, args.port), timeout=5)
    sock.settimeout(None)
    print("Connected.")
    sock.sendall(frame(encode_message("/show")))
    if not args.no_remote:
        sock.sendall(frame(encode_message("/remote")))

    reader = FrameReader()
    pgm = pvw = None
    start = time.time()
    while True:
        data = sock.recv(65536)
        if not data:
            print("Connection closed by device.")
            return 1
        for packet in reader.feed(data):
            try:
                messages = decode_packet(packet)
            except Exception as e:  # noqa: BLE001 - diagnostic tool
                print(f"[{time.time() - start:7.2f}] <undecodable packet {len(packet)} bytes: {e}>")
                continue
            for address, a in messages:
                t = f"[{time.time() - start:7.2f}]"
                if address == "/show" and a and isinstance(a[0], str):
                    with open(args.xml, "w", encoding="utf-8") as f:
                        f.write(a[0])
                    head = a[0][:300].replace("\n", " ")
                    print(f"{t} /show XML ({len(a[0])} chars) saved to {args.xml}: {head}...")
                    continue
                if address in HIDDEN and not args.all:
                    continue
                print(f"{t} {address} {a}")
                if address == "/show/pgmcurrent":
                    pgm = a
                elif address == "/show/pvwcurrent":
                    pvw = a
                if address in ("/show/pgmcurrent", "/show/pvwcurrent"):
                    print(f"          => PROGRAM: {pgm}   PREVIEW: {pvw}")


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        pass
