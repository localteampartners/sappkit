# SappKit × SappLink

SappKit implements SappLink v1 CC-in (see
`~/apps/sapptune/sapplink/PROTOCOL.md`): a fixed MIDI CC → parameter mapping,
identical in the plugin and the offline CLI render.

## The contract

Source of truth: `~/apps/sapptune/sapplink/manifests/sappkit.json`.
Vendored copy: `tests/data/sapplink-manifest.json` — the `[sapplink]` unit
tests fail if the C++ table in `src/core/SappLinkCCMap.cpp` drifts from the
vendored manifest. If the sapptune manifest changes, update the vendored copy
and the table together.

| CC | id | range | curve |
|---|---|---|---|
| 7 | masterGain | −24..12 dB | linear |
| 14 | punch | 0..1 | linear |
| 15 | squash | 0..1 | linear |
| 16 | crush | 0..1 | linear |
| 17 | width | 0..2 | linear |
| 18 | humanize | 0..1 | linear |
| 91 | roomLevel | 0..1 | linear |
| 92 | roomSize | 0..1 | linear |

Deliberately absent:

- **CC 64** — real sustain-pedal semantics live in SappSounds (drum one-shots
  ignore note-off anyway).
- **CC 1 / CC 11** — left free for library-authored SFZ CC conditions
  (dynamics crossfades some kits ship).
- **Per-pad tune/decay/pan/level** — 64 parameters; that's host-automation
  territory (`pad<N>Tune` …), not a 7-bit live-CC surface.

## How CC-in lands

- **Plugin:** mapped CCs become slew targets; each block moves the APVTS
  parameter a fraction of the way through the same normalized path host
  automation uses (~15 ms approach), so 7-bit steps don't zipper. Any channel.
  The CC event still reaches the sampler (SFZ `locc/hicc` conditions).
- **CLI render:** `renderKit` applies the same table to `KitParams` as events
  stream in — a `.mid` with CC automation renders exactly like the live
  plugin behaves. `tools/uishot --cctest` proves the plugin path end to end
  (CC 7 sweep through `processBlock`).
