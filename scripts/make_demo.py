#!/usr/bin/env python3
"""Compose a real drum groove and render it through the sappkit CLI.

Uses the AVL Black Pearl kit if fetched (sappsounds fetch-library.sh get
avl-drumkits), otherwise falls back to the built-in diagnostic kit.

  scripts/make_demo.py [cli-path] [out.wav]
"""
import json
import os
import struct
import subprocess
import sys

CLI = sys.argv[1] if len(sys.argv) > 1 else "./build/sappkit"
OUT = sys.argv[2] if len(sys.argv) > 2 else "/tmp/sappkit-demo.wav"
AVL = os.path.expanduser(
    "~/Samples/avl-drumkits/AVL_Drumkits_1.0/Black_Pearl_5pc.sfz")
TPQ = 480
BPM = 102

KICK, STICK, SNARE, CLAP = 36, 37, 38, 39
CHH, PHH, OHH = 42, 44, 46
CRASH, RIDE, TAMB = 49, 51, 54


def write_midi(path, events, tempo_bpm=BPM):
    events = sorted(events, key=lambda e: e[0])
    track = b""
    last = 0
    us = int(60_000_000 / tempo_bpm)
    track += bytes([0x00, 0xFF, 0x51, 0x03]) + us.to_bytes(3, "big")
    for tick, data in events:
        delta = tick - last
        last = tick
        vlq = [delta & 0x7F]
        d = delta >> 7
        while d:
            vlq.append(0x80 | (d & 0x7F))
            d >>= 7
        track += bytes(reversed(vlq)) + bytes(data)
    track += bytes([0x00, 0xFF, 0x2F, 0x00])
    header = b"MThd" + struct.pack(">IHHH", 6, 0, 1, TPQ)
    with open(path, "wb") as f:
        f.write(header + b"MTrk" + struct.pack(">I", len(track)) + track)


def beats(b):
    return int(b * TPQ)


events = []


def hit(t, key, vel, dur=0.1):
    events.append((beats(t), [0x90, key, vel]))
    events.append((beats(t + dur), [0x80, key, 0]))


def cc(t, num, val):
    events.append((beats(t), [0xB0, num, max(0, min(127, val))]))


# ---- 8-bar groove: 4 bars tight, then the SappLink CCs open it up ----------
# Bars 1-4: dry punchy backbeat with hat 8ths and ghost snares.
for bar in range(8):
    t = bar * 4
    # kick: four-on-variants
    hit(t + 0.0, KICK, 118)
    hit(t + 1.75, KICK, 96)
    hit(t + 2.5, KICK, 110)
    # backbeat
    hit(t + 1.0, SNARE, 116)
    hit(t + 3.0, SNARE, 120)
    # ghosts
    hit(t + 2.25, SNARE, 38)
    hit(t + 3.75, SNARE, 44)
    # hats: 8ths with accents, open hat at the end of bars 2/4/6/8
    for eighth in range(8):
        tt = t + eighth * 0.5
        if eighth == 7 and bar % 2 == 1:
            hit(tt, OHH, 104)          # rings...
            hit(t + 4.0, CHH, 92)      # ...choked by the next downbeat hat
        else:
            hit(tt, CHH, 96 if eighth % 2 == 0 else 68)
    # colour
    if bar % 4 == 3:
        hit(t + 3.5, CLAP, 90)
        hit(t + 3.75, STICK, 70)
    if bar >= 4:
        hit(t + 0.5, TAMB, 64)
        hit(t + 2.5, TAMB, 58)

# Crash the phrase boundaries.
hit(0, CRASH, 112)
hit(16, CRASH, 118)

# SappLink automation rendered into the clip: dry start, then the room and
# crush open through bars 5-8 (CC 91 room, CC 16 crush, CC 15 squash).
cc(0, 91, 8)
cc(0, 16, 0)
cc(0, 15, 30)
for i in range(17):
    t = 16 + i / 2.0
    cc(t, 91, 8 + int(i * 4.5))       # room up to ~half
    cc(t, 16, int(i * 3.2))           # crush eases in
cc(28, 15, 84)                         # squash leans in for the last bars

# Outro: open hat + crash, let the room ring.
hit(32, KICK, 120)
hit(32, CRASH, 122)
hit(32, OHH, 96)

mid = "/tmp/sappkit-demo.mid"
write_midi(mid, events)

sfz_args = ["--sfz", AVL] if os.path.exists(AVL) else ["--diagnostic"]
cmd = [CLI, "render", *sfz_args, "--midi", mid, "--out", OUT,
       "--tail", "2.5", "--seed", "42",
       "--param", "punch=0.55", "--param", "humanize=0.25",
       "--param", "room_size=0.5", "--param", "master_gain_db=-1"]
result = json.loads(subprocess.check_output(cmd).decode())
print(json.dumps(result, indent=2))
print(f"groove -> {OUT} ({result['durationSeconds']:.1f}s, "
      f"peak {result['peak']:.3f}, rms {result['rms']:.3f}, "
      f"instrument {'AVL Black Pearl 5pc' if os.path.exists(AVL) else 'diagnostic kit'})")
