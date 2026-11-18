#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Drives a running EmuDEN through its automation endpoint.

Start the app with EMUDEN_AUTOMATION=<port> (default here: 5577), then:

    drive.py state                      route, focused item, fps as JSON
    drive.py key down                   up down left right confirm back option extra menu view l r home
    drive.py go settings                push a route; "go details id=abc" passes parameters
    drive.py pad start 200              hold game buttons (south east west north up down left right l r start select)
    drive.py tap 640 360                a tap in logical units (the window is 720 units high)
    drive.py finger 1 down 100 500      multi-touch finger down|move|up in logical units
    drive.py screenshot C:/tmp/a.png    the next frame as a PNG
    drive.py quit

The player's test endpoint (app/screens/player/test_endpoint.cpp) listens on the next port;
its commands are sent there:

    drive.py vpad attach xbox           a virtual gamepad (xbox, playstation, nintendo): ok <id>
    drive.py vpad press south 200       hold its buttons (south east west north up down left right
                                        l1 r1 l2 r2 l3 r3 start select guide); "pad=<id>" picks a pad
    drive.py vpad down start select     held until "vpad up start select"
    drive.py vpad stick left 1 0 300    a stick (-1..1), back to the middle after 300 ms
    drive.py vpad trigger right 1 300
    drive.py vpad detach                unplug it
    drive.py player                     the session, its sound counters and the pads as JSON
    drive.py audio reset                start the sound counters again
    drive.py wav C:/tmp/a.wav 3         record 3 s of the device's output; answers with its level
                                        and the gaps in it
    drive.py bfi 240 pixels             the next 240 frames as drawn (G game, g again, B black)
                                        with the times between them and their middle pixels

Several commands run in order when separated by "+":

    drive.py key down + key confirm + screenshot out.png

Options: --port N, --wait S (seconds to wait for the app to listen, default 15),
--pause S (seconds between commands, default 0.25, so the app draws a frame in between).
"""
import socket
import sys
import time


def send(port, line, wait):
    deadline = time.time() + wait
    while True:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=150) as s:
                s.sendall((line + "\n").encode("utf-8"))
                data = b""
                while not data.endswith(b"\n"):
                    chunk = s.recv(65536)
                    if not chunk:
                        break
                    data += chunk
                return data.decode("utf-8").strip()
        except (ConnectionRefusedError, socket.timeout, OSError):
            if time.time() >= deadline:
                raise SystemExit(f"no EmuDEN automation endpoint on port {port}")
            time.sleep(0.2)


def main(argv):
    port, wait, pause = 5577, 15.0, 0.25
    args = []
    i = 0
    while i < len(argv):
        if argv[i] == "--port":
            port = int(argv[i + 1]); i += 2
        elif argv[i] == "--wait":
            wait = float(argv[i + 1]); i += 2
        elif argv[i] == "--pause":
            pause = float(argv[i + 1]); i += 2
        else:
            args.append(argv[i]); i += 1
    if not args:
        raise SystemExit(__doc__)
    commands, current = [], []
    for arg in args:
        if arg == "+":
            commands.append(current); current = []
        else:
            current.append(arg)
    commands.append(current)
    failed = False
    for n, command in enumerate(c for c in commands if c):
        if n:
            time.sleep(pause)
        own = command[0] in ("vpad", "player", "audio", "wav", "bfi", "screens", "touchpad")
        reply = send(port + 1 if own else port, " ".join(command), wait)
        print(reply)
        failed = failed or reply.startswith("error")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
