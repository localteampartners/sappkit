#!/usr/bin/env python3
"""Generate Latin/Afro-Cuban groove MIDI files for SappKit.

Written against the General MIDI percussion map, so a groove plays on the
Latin Percussion kit, on any GM drum kit, or on a mix of both. Patterns are
traditional (bossa nova, samba, son/rumba clave, cha-cha, songo, baiao,
mambo, guaguanco); the humanisation is deterministic per file so renders
are reproducible.

    python3 scripts/make_grooves.py [out-dir]     # default: demo/grooves
"""
import random
import struct
import sys
from pathlib import Path

TPQ = 480          # ticks per quarter note
BARS = 2

# --- General MIDI percussion notes we use -----------------------------------
KICK, SLAP = 36, 38            # cajon bass tone / slap on the Latin kit
STICK = 37
CLOSED_HAT, OPEN_HAT = 42, 46
TAMB, COWBELL = 54, 56
BONGO_HI, BONGO_LO = 60, 61
CONGA_MUTE, CONGA_OPEN, CONGA_LO = 62, 63, 64
AGOGO_HI, AGOGO_LO = 67, 68
CABASA, MARACAS = 69, 70
GUIRO_SHORT, GUIRO_LONG = 73, 74
CLAVES = 75

S = TPQ // 4       # sixteenth
E = TPQ // 2       # eighth
Q = TPQ            # quarter

# --- patterns ---------------------------------------------------------------
# Each entry: (note, [onset ticks within a 2-bar 4/4 phrase], velocity)
BAR = 4 * TPQ


def sixteenths(*positions):
    """Positions given as 1-based sixteenth indices across two bars."""
    return [(p - 1) * S for p in positions]


GROOVES = {
    # Bossa nova: 3-2 bossa clave on the rim/claves, soft cajon heart-beat,
    # steady shaker, light conga comping.
    "bossa-nova": {
        "bpm": 132,
        "voices": [
            (CLAVES, sixteenths(1, 4, 7, 11, 14, 20, 23, 27, 30), 92),
            (KICK, sixteenths(1, 7, 9, 15, 17, 23, 25, 31), 84),
            (CABASA, sixteenths(*range(1, 33, 2)), 58),
            (CONGA_MUTE, sixteenths(3, 11, 19, 27), 66),
            (CONGA_OPEN, sixteenths(8, 24), 74),
            (STICK, sixteenths(5, 13, 21, 29), 60),
        ],
    },
    # Slower, sparser bossa for ballads.
    "bossa-slow": {
        "bpm": 108,
        "voices": [
            (CLAVES, sixteenths(1, 4, 7, 11, 14, 20, 23, 27, 30), 84),
            (KICK, sixteenths(1, 11, 17, 27), 78),
            (CABASA, sixteenths(*range(1, 33, 4)), 54),
            (CONGA_MUTE, sixteenths(7, 23), 60),
            (CONGA_OPEN, sixteenths(16, 32), 70),
        ],
    },
    # Samba: driving surdo-style low pulse, tamborim/agogo, busy shaker.
    "samba": {
        "bpm": 196,
        "voices": [
            (KICK, sixteenths(3, 7, 11, 15, 19, 23, 27, 31), 92),
            (CONGA_LO, sixteenths(7, 15, 23, 31), 84),
            (AGOGO_HI, sixteenths(1, 6, 9, 14, 17, 22, 25, 30), 76),
            (AGOGO_LO, sixteenths(4, 12, 20, 28), 70),
            (CABASA, sixteenths(*range(1, 33)), 46),
            (TAMB, sixteenths(5, 13, 21, 29), 62),
        ],
    },
    # Son clave 3-2 with tumbao congas and bongo martillo.
    "son-montuno": {
        "bpm": 180,
        "voices": [
            (CLAVES, sixteenths(1, 7, 13, 20, 26), 94),
            (CONGA_MUTE, sixteenths(1, 5, 9, 17, 21, 25), 66),
            (CONGA_OPEN, sixteenths(7, 8, 23, 24), 88),
            (CONGA_LO, sixteenths(13, 29), 78),
            (BONGO_HI, sixteenths(3, 11, 19, 27), 62),
            (BONGO_LO, sixteenths(15, 31), 70),
            (COWBELL, sixteenths(1, 9, 17, 25), 72),
        ],
    },
    # Rumba clave 2-3 variant, guaguanco feel.
    "guaguanco": {
        "bpm": 176,
        "voices": [
            (CLAVES, sixteenths(4, 11, 17, 23, 28), 92),
            (CONGA_LO, sixteenths(1, 9, 17, 25), 82),
            (CONGA_MUTE, sixteenths(5, 6, 13, 21, 22, 29), 60),
            (CONGA_OPEN, sixteenths(15, 16, 31, 32), 86),
            (GUIRO_LONG, sixteenths(1, 17), 58),
            (GUIRO_SHORT, sixteenths(9, 13, 25, 29), 54),
        ],
    },
    # Cha-cha-cha: the four-and-one on the cowbell, guiro on the backbeat.
    "cha-cha": {
        "bpm": 128,
        "voices": [
            (COWBELL, sixteenths(1, 5, 9, 13, 15, 17, 21, 25, 29, 31), 78),
            (KICK, sixteenths(1, 9, 17, 25), 84),
            (CONGA_MUTE, sixteenths(5, 13, 21, 29), 64),
            (CONGA_OPEN, sixteenths(15, 31), 80),
            (GUIRO_LONG, sixteenths(1, 9, 17, 25), 56),
            (MARACAS, sixteenths(*range(1, 33, 2)), 50),
        ],
    },
    # Songo: Cuban/funk hybrid, syncopated cajon and conga conversation.
    "songo": {
        "bpm": 168,
        "voices": [
            (KICK, sixteenths(1, 4, 11, 17, 20, 27), 88),
            (SLAP, sixteenths(7, 15, 23, 31), 92),
            (CLOSED_HAT, sixteenths(*range(1, 33, 2)), 54),
            (CONGA_OPEN, sixteenths(6, 14, 22, 30), 76),
            (CONGA_LO, sixteenths(9, 25), 80),
            (COWBELL, sixteenths(3, 19), 66),
        ],
    },
    # Baiao: north-eastern Brazilian, zabumba-style low/high pairing.
    "baiao": {
        "bpm": 152,
        "voices": [
            (KICK, sixteenths(1, 7, 9, 17, 23, 25), 90),
            (CONGA_LO, sixteenths(4, 12, 20, 28), 74),
            (SLAP, sixteenths(11, 27), 84),
            (TAMB, sixteenths(*range(1, 33, 2)), 52),
            (AGOGO_HI, sixteenths(5, 13, 21, 29), 62),
        ],
    },
    # Mambo: bell-driven, dense, for horn sections to sit on.
    "mambo": {
        "bpm": 190,
        "voices": [
            (COWBELL, sixteenths(1, 5, 7, 9, 13, 15, 17, 21, 23, 25, 29, 31), 80),
            (CONGA_LO, sixteenths(1, 17), 84),
            (CONGA_MUTE, sixteenths(5, 9, 21, 25), 62),
            (CONGA_OPEN, sixteenths(7, 8, 23, 24), 88),
            (BONGO_HI, sixteenths(3, 11, 19, 27), 58),
            (CLAVES, sixteenths(1, 7, 13, 20, 26), 86),
        ],
    },
    # Bossa with brushed-kit feel: hats instead of shaker, for pop/jazz beds.
    "bossa-kit": {
        "bpm": 138,
        "voices": [
            (STICK, sixteenths(1, 4, 7, 11, 14, 20, 23, 27, 30), 80),
            (KICK, sixteenths(1, 7, 9, 15, 17, 23, 25, 31), 82),
            (CLOSED_HAT, sixteenths(*range(1, 33, 2)), 56),
            (OPEN_HAT, sixteenths(15, 31), 62),
            (BONGO_HI, sixteenths(5, 21), 58),
        ],
    },
}


# --- MIDI writing -----------------------------------------------------------
def varlen(value):
    out = bytearray([value & 0x7F])
    value >>= 7
    while value:
        out.insert(0, 0x80 | (value & 0x7F))
        value >>= 7
    return bytes(out)


def write_midi(path, bpm, events):
    """events: list of (tick, note, velocity, duration)"""
    track = bytearray()
    tempo = int(60_000_000 / bpm)
    track += b"\x00\xff\x51\x03" + tempo.to_bytes(3, "big")
    track += b"\x00\xff\x58\x04\x04\x02\x18\x08"          # 4/4

    # note-on / note-off pairs sorted into one stream
    stream = []
    for tick, note, vel, dur in events:
        stream.append((tick, 0x99, note, vel))            # channel 10
        stream.append((tick + dur, 0x89, note, 0))
    stream.sort(key=lambda e: (e[0], e[1]))

    now = 0
    for tick, status, note, vel in stream:
        track += varlen(tick - now) + bytes([status, note, vel])
        now = tick
    track += b"\x00\xff\x2f\x00"

    header = b"MThd" + struct.pack(">IHHH", 6, 0, 1, TPQ)
    path.write_bytes(header + b"MTrk" + struct.pack(">I", len(track)) + bytes(track))


def build(name, spec, out_dir, repeats=2):
    rng = random.Random(sum(ord(c) for c in name))       # deterministic per groove
    events = []
    phrase = 2 * BAR
    for rep in range(repeats):
        base = rep * phrase
        for note, onsets, vel in spec["voices"]:
            for onset in onsets:
                # gentle human feel: +-6 ticks (~3 ms at these tempos) and
                # +-7 velocity, seeded so every render is identical
                tick = max(0, base + onset + rng.randint(-6, 6))
                velocity = max(1, min(127, vel + rng.randint(-7, 7)))
                events.append((tick, note, velocity, S))
    out = out_dir / f"{name}.mid"
    write_midi(out, spec["bpm"], events)
    return out, len(events)


def main():
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent.parent / "demo/grooves"
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, spec in GROOVES.items():
        path, count = build(name, spec, out_dir)
        print(f"{path.name:<18} {spec['bpm']:>4} bpm  {count:>3} hits")
    print(f"\n{len(GROOVES)} grooves -> {out_dir}")


if __name__ == "__main__":
    main()
