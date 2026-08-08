# CURRENT STATE — sappkit

<!-- UPDATE WHEN: a feature ships, a deploy happens, something breaks, or something gets fixed. This file answers "what's the project like *right now*?" -->

**Last verified:** 2026-08-06

---

## Shipped 2026-08-07 — v0.3.0 (in-plugin updater)

- Footer version button checks GitHub daily (or on click); UPDATE
  button downloads + installs the newest release (macOS: plug-in
  folders + quarantine clear; Windows: rename-trick swap of the
  loaded .vst3). Throttle key `lastUpdateCheck-sappkit` in the shared
  Sapp settings file.
- v0.3.0 GitHub release carries CI-built Windows-x64 and
  macOS-universal zips (SappKit VST3/AU/Standalone). Same code path
  end-to-end verified in sappkeys 2026-08-07.
- CMake `project()` VERSION must be bumped with every release tag
  (the updater compares JucePlugin_VersionString to the tag).
- Build dirs (`build/`, `build-plugin/`) no longer tracked in git.

## What's built and working

- Core (`sappkit_core`): KitModel pad mapping (GM-aware, choke/layer/RR
  reporting), per-pad overrides at the region-policy layer, KitEngine bus
  (punch/crush/squash/room/width/limiter), deterministic KitRender,
  SappLink CC map, generated DiagnosticKit.
- CLI (`build/sappkit`): pads / inspect / validate / params / scan / render —
  JSON out, seeded determinism. Verified against AVL Black Pearl 5pc
  (133 regions, 0 missing, correct chokes).
- Plugin: Standalone/VST3/AU build green; dark-club editor with 4×4 pad grid
  (names, choke badges, hit flash), pad edit strip (tune/decay/pan/level),
  kit-bus knobs, meters; async SFZ load; debounced pad rebuild; SappLink
  CC-in with slew.
- GET SOUNDS overlay (ported from sapporchestra's SoundsPanel): one-click
  download+extract of curated drum libraries (AVL Drumkits, Big Rusty Drums,
  SM MegaReaper, VSCO 2 CE percussion) into the shared `~/Samples` root, plus
  an installed-kit browser (recursive .sfz scan, filter/category,
  double-click loads through the normal pad-mapping path).
- UiShot: offscreen PNG + `--cctest` (CC 7 sweep through processBlock) — PASS.
- Tests: 28 Catch2 cases green (chokes, RR, velocity layers, pad mapping,
  overrides, FX audibility, determinism, SappLink drift guard).
- Demo: `scripts/make_demo.py` renders an 8-bar groove (ghost notes, hat
  chokes, SappLink room/crush/squash automation) through the CLI with the
  AVL kit.

## What's deployed

- **Environment:** local only (macOS). Plugin copied to user plugin dirs by
  the JUCE copy step on build.
- **Version / commit:** v0.1.0 initial.

## What's in progress

- Nothing mid-flight; v0.1.0 is a complete vertical slice.

## What's known broken / flaky

- None known. (SM Drums / Big Rusty registry entries are registered but not
  yet load-tested through the pad mapper — AVL and the diagnostic kit are.)

## Half-finished or abandoned

- None.

## Shipped 2026-08-08 — v0.4.0 (persistent mixes + kit expansion)

- Per-kit mix persistence: core/KitMix.{h,cpp} (JSON, note-keyed pads +
  bus), plugin auto-load/auto-save (suppression covers load/restore churn),
  CLI `mix show|set|clear` + render mix apply. 34 unit tests green.
- 12 drum libraries in GET SOUNDS; ARIA mixer-gate normalization in
  core/KitModel (normalizeAriaMixerGates + loadKitSfz — both the plugin
  and the CLI load kits through it).
- Requires sappsounds >= v0.3.3 (ARIA parser fixes).
