# CHANGELOG — sappkit

<!-- UPDATE WHEN: you ship a meaningful change — feature, fix, migration, dependency bump that users/operators would care about. Trivial refactors don't belong here. -->

Newest first. Format: `## YYYY-MM-DD — short title`, then bullets.

---

## 2026-08-08 (later)

- **Latin percussion kit + Latin grooves.** New one-click library in GET
  SOUNDS: "Latin Percussion" (6 MB) — bongos, congas (mute/open/low), cajon
  (bass tones + slap), claves, cowbells, agogo, cabasa, shaker, guiro,
  tambourine. 99 samples / 23 mapped voices on the GM percussion map, with
  velocity layers, round robins, and choke groups (mute vs open conga,
  short vs long guiro, bongos, cowbell). Samples from the Versilian
  Community Sample Library (CC0); the SFZ mapping and per-voice level
  calibration were generated for SappKit and are hosted on the repo's
  `samples-v1` release.
- Also selectable as a factory program ("Latin Percussion"), so sapptune
  can reach it by MIDI program change.
- **10 groove MIDI files** in `demo/grooves/` from `scripts/make_grooves.py`:
  bossa nova (3 feels), samba, son montuno, guaguanco, cha-cha, songo,
  baiao, mambo. Written to the GM map so they play on the Latin kit or any
  GM drum kit; humanisation is seeded, so renders are reproducible.

## 2026-08-08 — v0.4.0

- PERSISTENT KIT MIXES: pad tweaks (tune/decay/pan/level) and the kit bus
  now save per kit — auto-loaded whenever that kit loads, auto-saved ~2 s
  after a tweak. Fix the too-loud ride once; it stays fixed. One JSON per
  kit in the shared Sapp dir (Application Support/Sapp/KitMixes), keyed by
  note so mixes survive pad-map changes. Host session state still wins on
  DAW project restore. Loading a kit with no saved mix resets pads to
  defaults (no more mix bleed between kits).
- Agent/CLI access: `sappkit mix show|set|clear` edits the same files —
  `sappkit mix set --sfz kit.sfz ride.level=-6 bus.punch=0.5` (targets by
  pad name substring, padN, or noteN; name matches apply to all hits, so
  "ride" tames Ride + Ride Bell). `render` applies the saved mix by
  default (--no-mix to bypass) — CLI renders sound like the plugin.
- 8 NEW DRUM LIBRARIES in GET SOUNDS (all free, one click): Muldjord Kit
  (Tama rock/metal), DRS Kit (Sonor), Naked Drums (multi-mic, 10 RR),
  Virtuosity Drums (Versilian deep-sampled), Swirly (brushes), Unruly
  (all-snare experimental), Frankensnare, Gogodze Phu II (lo-fi/hi-fi).
- ARIA mixer-gate normalization: DrumGizmo-style multi-mic kits (silent
  out of the box without ARIA's CC sliders) now play a sensible default
  mix — close mics on, or the full natural mic stack with level trim when
  channels are indistinguishable. Validated: Muldjord 4256 regions,
  Virtuosity 1676, Gogodze 777, all rendering.
- Version 0.4.0 (matches the release tag; updater rule).

## 2026-08-07 — v0.3.0
- In-plugin UPDATE button: daily GitHub release check (click the version
  number to check on demand); one click downloads and installs the new
  build (macOS: plug-in folders + quarantine cleared; Windows: loaded
  .vst3 swapped via rename), standalone relaunches itself on macOS.
- Plugin version now tracks release tags (0.3.0).

## Unreleased

-

---

## 2026-08-06 — GET SOUNDS in-plugin downloader

- Ported sapporchestra's SoundsPanel: GET SOUNDS header button opens a
  dark-club overlay with one-click download → extract → rescan for curated
  drum libraries (AVL Drumkits 29 MB, Big Rusty Drums 600 MB, SM MegaReaper
  2.2 GB, VSCO 2 CE percussion 3.3 GB) and an installed-kit browser
  (recursive .sfz scan of the shared `~/Samples` root, filter + category,
  double-click to load). Samples root is shared with the other Sapp
  instruments via `Sapp/SampleLibraries.settings`.

---

## 2026-08-06 — v0.1.0 initial build

- sappkit_core: KitModel (16-pad GM-aware map + region-policy overrides),
  KitEngine (punch/crush/squash/room/width/limiter bus), KitRender
  (deterministic, SappLink CC steering), SappLinkCCMap, DiagnosticKit.
- Agent CLI: pads / inspect / validate / params / scan / render (JSON).
- JUCE plugin (Standalone/VST3/AU, JUCE 8.0.15) with dark-club editor:
  4×4 pad grid, choke badges, pad edit strip, kit knobs, meters.
- SappKitUiShot: offscreen editor PNG + `--cctest` SappLink proof.
- 28 Catch2 tests: chokes, round robins, velocity layers, pad mapping,
  overrides, FX audibility, determinism, manifest drift guard.
- SappLink manifest committed to sapptune (`sapplink/manifests/sappkit.json`)
  with vendored drift-guarded copy.
- sappsounds fetch-library registry: added avl-drumkits, sm-drums,
  big-rusty-drums (pushed to sappsounds).
- Demo: `scripts/make_demo.py` — 8-bar groove with hat chokes, ghost notes,
  and in-clip SappLink automation, rendered with AVL Black Pearl.
