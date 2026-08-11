#!/usr/bin/env python3
"""Regenerate the headless-regression fixture kit (sappkit #1).

The fixture is shaped like a GET SOUNDS samples root so the FACTORY KIT
programs resolve against it: <root>/avl-drumkits/Black_Pearl_5pc.sfz is
program 2 and <root>/gogodze-phu/Kit.sfz is program 8. tools/headless points
SAPP_SAMPLES_ROOT at a copy of it, so the regression never depends on a real
sample library being installed.

    python3 scripts/make_headless_fixture.py
"""

import math
import pathlib
import random
import struct
import wave

ROOT = pathlib.Path(__file__).resolve().parent.parent / "tests" / "data" / "kit-headless"
SR = 48000


def write_wav(path, samples):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32000))
                               for s in samples))


def drum_hit(seconds=0.35, tone_hz=180.0, noise=0.35, seed=7):
    rng = random.Random(seed)
    n = int(SR * seconds)
    out = []
    for i in range(n):
        t = i / SR
        env = math.exp(-t * 9.0)
        body = math.sin(2.0 * math.pi * tone_hz * t * math.exp(-t * 2.0))
        crack = (rng.random() * 2.0 - 1.0) * math.exp(-t * 45.0)
        out.append(env * (body * (1.0 - noise) + crack * noise) * 0.85)
    return out


SFZ_LOUD = """\
// Headless-regression fixture (sappkit #1): the LOUD reference kit, shaped
// like AVL Black Pearl so factory program 2 resolves against it.
// Three GM pads is enough to answer "did the named kit sound?" from audio.
<region> sample=../drum-hit.wav key=36 pitch_keycenter=36 volume=0   // Kick
<region> sample=../drum-hit.wav key=38 pitch_keycenter=36 volume=-2 transpose=7   // Snare
<region> sample=../drum-hit.wav key=42 pitch_keycenter=36 volume=-6 transpose=24 group=1 off_by=1   // Closed hat
<region> sample=../drum-hit.wav key=46 pitch_keycenter=36 volume=-6 transpose=22 group=1 off_by=1   // Open hat
"""

SFZ_QUIET = """\
// Headless-regression fixture (sappkit #1): deliberately much quieter than
// the loud kit, so "which kit actually sounded?" is answerable from the audio
// alone. Shaped like gogodze-phu so factory program 8 resolves against it.
<region> sample=../drum-hit.wav key=36 pitch_keycenter=36 volume=-20
<region> sample=../drum-hit.wav key=38 pitch_keycenter=36 volume=-22 transpose=7
<region> sample=../drum-hit.wav key=42 pitch_keycenter=36 volume=-26 transpose=24 group=1 off_by=1
<region> sample=../drum-hit.wav key=46 pitch_keycenter=36 volume=-26 transpose=22 group=1 off_by=1
"""


def main():
    write_wav(ROOT / "drum-hit.wav", drum_hit())
    (ROOT / "avl-drumkits").mkdir(parents=True, exist_ok=True)
    (ROOT / "gogodze-phu").mkdir(parents=True, exist_ok=True)
    (ROOT / "avl-drumkits" / "Black_Pearl_5pc.sfz").write_text(SFZ_LOUD)
    (ROOT / "gogodze-phu" / "Kit.sfz").write_text(SFZ_QUIET)
    print("wrote fixture under", ROOT)


if __name__ == "__main__":
    main()
