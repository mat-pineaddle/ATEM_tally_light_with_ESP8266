#!/usr/bin/env python3
"""Fake RODECaster Video for testing tally lights without the real device.

Usage:
    python3 tools/rcv_simulator.py [--port 10024]

Point a tally light (switcher type "RODECaster Video") at this computer's IP.
Then type commands:

    p 2      program = input 2          v 3      preview = input 3
    ps B     program = scene B          vs A     preview = scene A
             (scenes: A = inputs 1+2, B = 2+3, C = 1+3, D = 1+2+3)
    pm 1     program = media 1
    cut      swap program and preview
    instant  switch to instant mode     studio   switch to studio mode
    live 1   streaming on/off           rec 1    recording on/off
    q        quit

Standard library only.
"""

import argparse
import socket
import threading

from rcv_osc import FrameReader, decode_packet, encode_message, frame

clients = []
lock = threading.Lock()
state = {"pgm": ("videoIn", 1), "pvw": ("videoIn", 2), "mode": "studioLeft", "live": 0, "record": 0}

# Inputs visible in scenes A-G (picture in picture etc.)
SCENE_LAYOUT = [(1, 2), (2, 3), (1, 3), (1, 2, 3), (), (), ()]

SHOW_XML = (
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    '<RcvShow name="Simulator" frameRate="25" switching_mode="{mode}" PgmScene="{pgm_scene}" PvwScene="{pvw_scene}" '
    'PgmOverlay="-1" PvwOverlay="-1" transition="cut" transition_time="0.5">\n'
    '  <Scenes>\n' + "".join(
        f'    <Scene name="Scene {c}"><VideoLayers>'
        + "".join(f'<Layer source="videoInput" source_file="{i}" video_enabled="true"/>' for i in inputs)
        + '</VideoLayers></Scene>\n'
        for c, inputs in zip("ABCDEFG", SCENE_LAYOUT)) + '  </Scenes>\n'
    '</RcvShow>\n'
)


def broadcast(address, *args):
    data = frame(encode_message(address, *args))
    with lock:
        for c in list(clients):
            try:
                c.sendall(data)
            except OSError:
                clients.remove(c)
    print(f"  -> {address} {list(args)}")


def show_dump():
    pgm_scene = state["pgm"][1] - 1 if state["pgm"][0] == "scene" else 0
    pvw_scene = state["pvw"][1] - 1 if state["pvw"][0] == "scene" else 0
    return SHOW_XML.format(mode=state["mode"], pgm_scene=pgm_scene, pvw_scene=pvw_scene)


def handle_client(conn, addr):
    print(f"Client connected: {addr}")
    reader = FrameReader()
    try:
        while True:
            data = conn.recv(4096)
            if not data:
                break
            for packet in reader.feed(data):
                for address, args in decode_packet(packet):
                    print(f"  <- {addr[0]}: {address} {args}")
                    if address == "/show":
                        conn.sendall(frame(encode_message("/show", show_dump())))
                    elif address == "/remote":
                        conn.sendall(frame(encode_message("/show/pgmcurrent", *state["pgm"])))
                        if state["mode"] != "instant":
                            conn.sendall(frame(encode_message("/show/pvwcurrent", *state["pvw"])))
                        conn.sendall(frame(encode_message("/show/live", state["live"])))
                        conn.sendall(frame(encode_message("/show/record", state["record"])))
    except OSError:
        pass
    with lock:
        if conn in clients:
            clients.remove(conn)
    print(f"Client disconnected: {addr}")


def accept_loop(server):
    while True:
        conn, addr = server.accept()
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        with lock:
            clients.append(conn)
        threading.Thread(target=handle_client, args=(conn, addr), daemon=True).start()


def scene_id(s):
    s = s.strip().upper()
    return ord(s) - ord("A") + 1 if s.isalpha() else int(s)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=10024)
    args = ap.parse_args()

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("0.0.0.0", args.port))
    server.listen(8)
    threading.Thread(target=accept_loop, args=(server,), daemon=True).start()
    print(f"RCV simulator listening on port {args.port}. Type 'help' for commands.")

    while True:
        try:
            line = input("> ").strip()
        except EOFError:
            break
        if not line:
            continue
        cmd, *rest = line.split()
        arg = rest[0] if rest else ""
        try:
            if cmd == "q":
                break
            elif cmd == "p":
                state["pgm"] = ("videoIn", int(arg)); broadcast("/show/pgmcurrent", *state["pgm"])
            elif cmd == "v":
                state["pvw"] = ("videoIn", int(arg)); broadcast("/show/pvwcurrent", *state["pvw"])
            elif cmd == "ps":
                state["pgm"] = ("scene", scene_id(arg)); broadcast("/show/pgmcurrent", *state["pgm"])
            elif cmd == "vs":
                state["pvw"] = ("scene", scene_id(arg)); broadcast("/show/pvwcurrent", *state["pvw"])
            elif cmd == "pm":
                state["pgm"] = ("media", int(arg)); broadcast("/show/pgmcurrent", *state["pgm"])
            elif cmd == "cut":
                state["pgm"], state["pvw"] = state["pvw"], state["pgm"]
                broadcast("/show/pgmcurrent", *state["pgm"])
                broadcast("/show/pvwcurrent", *state["pvw"])
            elif cmd in ("instant", "studio"):
                state["mode"] = "instant" if cmd == "instant" else "studioLeft"
                broadcast("/show/switchingMode", state["mode"])
            elif cmd == "live":
                state["live"] = int(arg or 1); broadcast("/show/live", state["live"])
            elif cmd == "rec":
                state["record"] = int(arg or 1); broadcast("/show/record", state["record"])
            else:
                print(__doc__)
        except (ValueError, IndexError):
            print("Bad argument. Type 'help'.")


if __name__ == "__main__":
    main()
