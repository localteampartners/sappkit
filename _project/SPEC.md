# SPEC — sappkit

<!-- UPDATE WHEN: goals change, scope changes, non-goals change, or the target user changes -->

## What this is

SappKit is a punchy, fun drums & percussion instrument (JUCE Standalone /
VST3 / AU) built on the SappSounds engine, following the sapporchestra
architecture. It maps any SFZ drum library onto a 16-pad kit (GM-drum-map
aware), adds per-pad tune/decay/pan/level and a kit-wide character bus
(punch / squash / crush / tight room), and exposes everything to
MIDI-generation agents through a JSON CLI with deterministic renders.

## Why it exists

The sapp* instrument family (sappsynth, sapporchestra) needs drums: sapptune
writes grooves but had nothing to play them on. SappSounds already implements
everything drums require — one-shots, group/off_by chokes, off_mode=time,
round robins, velocity layers, lorand/hirand humanize, note_polyphony — so
SappKit is a thin product-policy layer over a proven engine.

## Users

- Michael, playing/producing in a DAW (Standalone/VST3/AU).
- sapptune and other MIDI-generation agents, via the `sappkit` CLI +
  SappLink manifest.

## Goals (in scope)

- 16-pad kit model auto-mapped from any SFZ instrument, GM-priority selection,
  pad-map discovery for agents (`sappkit pads` — note → sound name + chokes).
- Per-pad tune/decay/pan/level overrides applied at the region-policy layer.
- Kit-wide punch (transient emphasis), squash (bus compressor), crush
  (bit/rate decimator), room (tight small-room ER — not a hall).
- Dark-club UI: 4×4 pad grid with names + choke badges, per-pad edit strip,
  level meters.
- Agent CLI with seeded, bit-deterministic renders; SappLink CC-in identical
  in plugin and CLI.
- Catch2 coverage for chokes, round robins, pad mapping, determinism, and
  SappLink manifest drift.

## Non-goals (explicitly out of scope)

- No sequencer/groove engine — sapptune owns composition.
- No sample editing/slicing; SappKit plays SFZ libraries, it doesn't make them.
- No per-pad output routing or multi-out (stereo only for v0.x).
- No hall reverb — the room is deliberately small; use a send in the DAW.
- No bundled sample content in the repo — kits fetch via sappsounds
  `fetch-library.sh`.

## Success criteria

- `verify.sh` green in under a minute; all Catch2 suites pass.
- A real free kit (AVL Black Pearl) loads with 0 missing samples and a
  correct GM pad map, chokes included.
- `scripts/make_demo.py` renders a musical groove through the CLI with
  SappLink automation, deterministically per seed.
- UiShot screenshot verifies the UI without a screen session.

## Constraints

- Budget: $0 — free/CC-licensed sample libraries only.
- Platform: macOS-first (AU + UiShot are APPLE-gated); core/CLI portable C++20.
- Engine changes belong in sappsounds, not here (no JUCE in core).
