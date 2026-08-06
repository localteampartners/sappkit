# DECISIONS — sappkit

<!-- UPDATE WHEN: you make a non-obvious choice (library pick, architectural pattern, tradeoff). One entry per decision, newest at top. -->

The *why* behind choices that aren't self-evident from the code.

---

## 2026-08-06 — Pad overrides rebuild the instrument, not the voice path

**Decision:** per-pad tune/decay/pan/level are baked into a copied
`InstrumentDefinition` (region tune_cents / volume / pan / imposed ampeg
decay) and swapped in via `setInstrument`, debounced at 8 Hz in the plugin.
**Context:** SappSounds regions are immutable snapshots; the engine has no
per-note gain/tune hooks beyond global humanize.
**Alternatives considered:** engine API additions (rejected — SappSounds
stays product-neutral and is shared with sapporchestra); per-pad post-mix
processing (impossible — voices mix inside the engine).
**Tradeoffs:** rebuild copies sample data (tens of MB for drum kits, ~ms);
interactive knob drags land on the debounce tick, not per-sample.
**Revisit if:** kits get orchestral-sized or pad params need host automation
at audio rate — then add a region-gain hook to SappSounds.

## 2026-08-06 — Kit-wide SappLink surface only (8 CCs)

**Decision:** the manifest maps masterGain/punch/squash/crush/width/
humanize/roomLevel/roomSize; per-pad params are plugin-automation only.
CC 64 stays engine-native; CC 1/11 stay free for library SFZ conditions.
**Context:** 16 pads × 4 params = 64 CCs would exhaust the free-CC space and
mean nothing on other instruments.
**Revisit if:** sapptune needs per-pad moves — 14-bit CC pairs or an OSC v2
side-channel are the reserved paths.

## 2026-08-06 — Decay knob imposes an envelope instead of scaling one

**Decision:** pad decay < 1 sets `ampeg hold=0, sustain=0, decay=0.02+d²·1.98s`
on the pad's regions; decay = 1 leaves the library's natural envelope.
**Context:** drum SFZs mostly ship no meaningful ampeg (one-shots play out);
"scale the existing decay" would do nothing on most kits.
**Tradeoffs:** can shorten but not lengthen a sound. That's the honest
physical limit of one-shots anyway.

## 2026-08-06 — Room is ER + tiny FDN, capped at T60 ≈ 0.55 s

**Decision:** `SmallRoom` = 8 early taps (3–26 ms, size-scaled) + 4-line FDN
with T60 0.12–0.55 s. No hall mode.
**Context:** product brief: punchy club drums. A hall smears transients; a
DAW send covers anyone who wants wash. Mirrors sapporchestra's "algorithmic
first" reasoning at the opposite room size.

## 2026-08-06 — GM-priority pad selection

**Decision:** when a kit has >16 sounds, pads keep a ranked GM core
(kick, snare, hats, crash, ride, …) before exotic sounds; unmapped notes are
still playable — pads are a view, not a filter.
**Context:** agents and humans both expect kick/snare/hats to always be on
the grid (AVL 5pc has 29 sounds).

## 2026-08-06 — Product/engine split, JUCE pin, CLI-as-agent-API

Same rationale as sapporchestra (see its DECISIONS.md): generic sampler code
lives in SappSounds; JUCE 8.0.15 pinned to match sappsynth/sapporchestra;
the agent API is a JSON CLI, not a socket; the SappLink table lives in core
with a vendored-manifest drift test.
