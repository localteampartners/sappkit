# CURRENT STATE — sappkit

<!-- UPDATE WHEN: a feature ships, a deploy happens, something breaks, or something gets fixed. This file answers "what's the project like *right now*?" -->

**Last verified:** 2026-08-11

---

## Shipped 2026-08-11 — v0.8.0 (`libraryReady` drops on a MIDI program change)

- Audit of sappkeys #4 against this repo. The `preset` parameter and
  `setCurrentProgram()` paths were already honest; the MIDI program-change
  branch of `processBlock()` was not — it queued the program on the audio
  thread and left readiness to the loader thread's next pass (~5 ms), so a
  host that sent the program change and polled immediately read the previous
  kit's "ready" and could render into the load. Now cleared on the calling
  thread, right where the program is stored.
- 7 new headless checks cover a mid-session swap through the `preset`
  parameter, `setCurrentProgram()` and MIDI program change; the program-change
  pair fails on the previous build. `./verify.sh` green.
- Not tagged — the release is driven separately. The CI guard checks the tag
  against `project(... VERSION ...)` in CMakeLists.txt, now 0.8.0.

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
- User presets (SappLink format, `sapptune/sapplink/PRESETS.md`): SAVE SOUND
  captures the parameter state plus the loaded kit's `.sfz` path to
  `<Documents>/SappSounds/presets/sappkit/<name>.json`; PRESETS loads a saved
  sound (reloading its kit) or a factory kit. Also drivable from a host lane
  via the `preset` AudioParameterChoice (added last in the layout; no existing
  parameter moved). A preset load suppresses the per-kit mix auto-save so the
  two systems can't fight.
- UiShot: offscreen PNG + `--cctest` (CC 7 sweep through processBlock) — PASS,
  and `--presettest` (user-preset round trip end to end) — PASS, 74/74
  parameters restored with max |diff| exactly 0.
- Tests: 28 Catch2 cases green (chokes, RR, velocity layers, pad mapping,
  overrides, FX audibility, determinism, SappLink drift guard).
- Demo: `scripts/make_demo.py` renders an 8-bar groove (ghost notes, hat
  chokes, SappLink room/crush/squash automation) through the CLI with the
  AVL kit.

## What's deployed

- **Environment:** local only (macOS). Plugin copied to user plugin dirs by
  the JUCE copy step on build.
- **Version / commit:** v0.7.0 (CMake `project(... VERSION ...)` is the single source; the CI release guard reads only that).

## What's in progress

- Nothing mid-flight; v0.1.0 is a complete vertical slice.

## What's known broken / flaky

- (SM Drums / Big Rusty registry entries are registered but not yet
  load-tested through the pad mapper — AVL and the diagnostic kit are.)
- Cosmetic: `sapp::userpresets::capture()` snapshots every parameter with an
  id, so a saved user preset JSON now also carries a `libraryReady` entry.
  Inert on load (the id is not in the APVTS, so `apply()` skips it) and it
  can never reach host state. Not fixed here because `UserPresets.{h,cpp}`
  is byte-identical across sappsynth / sappkeys / sappkit — the right fix
  (skip non-automatable parameters) belongs in all three at once.

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

## 2026-08-08 — Latin percussion + grooves

- 13 libraries in GET SOUNDS; "latin-percussion" is hosted by us as a
  release asset (tag `samples-v1`) because it is a generated SFZ mapping
  over CC0 VCSL samples, not an upstream package.
- `scripts/make_grooves.py` regenerates `demo/grooves/*.mid` (GM map,
  deterministic humanisation).

## Shipped 2026-08-10 — v0.7.0 (headless kit loading, issue #1)

- Kit loading no longer depends on the JUCE message loop. A **loader
  thread** owns a LoadJob queue and performs every install; it also runs the
  8 Hz pad-override / mix-save tick. `juce::Timer` is an editor hook only,
  and the thread is joined in the destructor (closing a latent
  use-after-free in the old detached-thread + callAsync design).
  Headless before: -200.00 dBFS / 0 voices / 0 pads. After: -27.92 dBFS /
  2 voices / 4 pads, byte-identical with and without a pumped loop.
- `libraryReady` read-only host parameter (outside the APVTS, appended
  last): the station polls it instead of a blind settle. Via the VST3
  controller its value lands through `outputParameterChanges`, so one
  `processBlock` must run first.
- `SappKit-build:` / `SappKit-kit:` / `SappKit-audio-source:` log lines
  (tee with `$SAPP_KIT_LOG`).
- `clean` parameter (SappLink CC 3, default 0) scaling `humanize` by
  (1 − clean), in the plugin, the CLI and the offline render.
- `tools/headless/sappkit-headless` + CTest `headless` (19 checks);
  `verify.sh` now builds the plugin target too. 35 unit test cases.
