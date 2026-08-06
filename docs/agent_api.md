# SappKit Agent API

The `sappkit` CLI is the stable machine interface for external software —
MIDI-generation agents in particular. Every command prints exactly one JSON
document to stdout; diagnostics go to stderr; exit codes are `0` ok,
`1` ok-with-warnings, `2` failure.

Binary: `build/sappkit` (CMake target `sappkit-cli`).

## Typical agent workflow

```text
1. sappkit pads       → learn the pad map: note → sound name, chokes, layers
2. compose MIDI       → GM-style drum notes + SappLink CCs for kit moves
3. sappkit render     → deterministic WAV (fixed --seed)
4. judge / iterate
```

## pads

```bash
sappkit pads (--sfz <file.sfz> | --diagnostic)
```

The pad-map discovery call — everything a MIDI agent needs to write for a kit:

```json
{
  "ok": true,
  "name": "Black Pearl 5pc",
  "pads": [
    {"pad": 1, "note": 36, "noteName": "C2", "name": "Kick", "gmName": "Kick",
     "chokeGroup": 0, "chokedBy": 0, "velocityLayers": 5, "roundRobins": 1,
     "oneShot": true, "regions": 5},
    {"pad": 10, "note": 46, "noteName": "A#2", "name": "Open Hat",
     "chokeGroup": 2, "chokedBy": 1, "velocityLayers": 5, "roundRobins": 1,
     "oneShot": true, "regions": 5}
  ],
  "padCount": 16,
  "soundCount": 29
}
```

Composition rules an agent should follow:

- Trigger pads by their `note`. Note-offs are optional — drum regions are
  one-shots.
- `chokeGroup`/`chokedBy` describe hi-hat-style behavior: a pad whose
  `chokedBy` equals another pad's `chokeGroup` silences that pad when hit
  (closed hat chokes open hat). Use it — an open hat ringing through a
  closed-hat pattern sounds wrong.
- Velocity matters twice: it picks the velocity layer *and* scales level.
  Ghost notes ≈ 30–50, backbeats ≈ 110–127.
- Repeated hits cycle round robins automatically; vary `--seed` for a
  different humanized take of the same MIDI.

## inspect

```bash
sappkit inspect (--sfz <file.sfz> | --diagnostic) [--regions]
```

Superset of `pads`: adds `source`, `regions`, `missingSamples`,
`estimatedRamBytes`, `capabilities` (max velocity layers / round robins /
choke-group count), `controllers` (CC 64 sustain + the SappLink CC list),
parser `diagnostics`, and with `--regions` a per-region dump.

## validate

```bash
sappkit validate --sfz <file.sfz>
```

`{"ok":bool, "errors":N, "warnings":N, "missingSamples":N, "regions":N,
"unsupportedOpcodes":[...], "diagnostics":[{severity,file,line,message}]}`

## params

```bash
sappkit params
```

Returns the kit-wide parameter schema (`params`) and the per-pad override
schema (`padParams`). Use these names with `render --param`.

| name | id | range | default | MIDI CC | meaning |
|---|---|---|---|---|---|
| punch | punch | 0–1 | 0.35 | 14 | transient emphasis |
| squash | squash | 0–1 | 0.25 | 15 | one-knob bus compressor |
| crush | crush | 0–1 | 0 | 16 | bit/rate vintage-sampler character |
| room_level | roomLevel | 0–1 | 0.18 | 91 | tight small-room mix |
| room_size | roomSize | 0–1 | 0.4 | 92 | closet → live room |
| width | width | 0–2 | 1 | 17 | stereo width |
| humanize | humanize | 0–1 | 0.15 | 18 | per-hit tune scatter |
| master_gain_db | masterGain | −24–12 | 0 | 7 | output gain |
| pad\<1-16\>_tune | pad\<N\>Tune | −12–12 | 0 | — | pad tune, semitones |
| pad\<1-16\>_decay | pad\<N\>Decay | 0–1 | 1 | — | 1 = natural, <1 imposed decay |
| pad\<1-16\>_pan | pad\<N\>Pan | −1–1 | 0 | — | pad pan |
| pad\<1-16\>_level | pad\<N\>Level | −24–12 | 0 | — | pad trim dB |
| quality | quality | enum | 1 | — | 0 draft (linear) · 1 normal (cubic) |

**SappLink CC-in:** the MIDI CC column is a live contract — CCs embedded in
a rendered `.mid` (or played into the plugin) move these parameters, with
slew smoothing, on any channel. See [sapplink.md](sapplink.md) and the
manifest at `~/apps/sapptune/sapplink/manifests/sappkit.json`. Pad numbers in
`pad<N>_*` params are 1-based and match the `pad` field from `sappkit pads`.

## render

```bash
sappkit render (--sfz <file.sfz> | --diagnostic) \
    --midi <file.mid> --out <file.wav> \
    [--sr 48000] [--seed N] [--tail seconds] [--param NAME=VALUE ...]
```

- Input: SMF format 0/1. Notes, CCs, pitch bend are honored; SappLink CCs
  steer kit parameters mid-clip.
- Output: stereo float32 WAV through the full chain (sampler → punch →
  crush → squash → room → width → limiter).
- **Deterministic:** identical inputs + `--seed` ⇒ bit-identical WAV. Vary
  the seed for new round-robin/humanize takes.

Result: `{"ok":true, "out":..., "frames":N, "durationSeconds":s,
"peak":p, "rms":r, "midiEvents":N, "seed":N}`.

## scan

```bash
sappkit scan <library-dir> [--all]
```

Walks a library folder for `.sfz` instruments (skipping `includes/` partials
unless `--all`) and returns `{"instruments":[{path,name,category,regions,
sounds,lowKey,highKey}], "count":N}` — parse-only, fast. This is how an
agent discovers which kits it can write for.

## Kits

Fetch free SFZ drum kits with the SappSounds registry
(`~/apps/sappsounds/scripts/fetch-library.sh`): `avl-drumkits`, `sm-drums`,
`big-rusty-drums`, plus `vsco2-ce` orchestral percussion.
[scripts/make_demo.py](../scripts/make_demo.py) composes a full groove
(ghost notes, hat chokes, SappLink automation) and renders it via the CLI.

## Stability

Command names, field names, exit codes, and parameter names are contracts.
New fields may be added; existing ones are not renamed or repurposed.
