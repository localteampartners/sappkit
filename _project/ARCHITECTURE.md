# ARCHITECTURE — sappkit

<!-- UPDATE WHEN: tech stack changes, a component is added/removed, data flow changes, or a major directory is renamed -->

## Tech stack

- **Language / runtime:** C++20
- **Framework:** JUCE 8.0.15 (plugin/UI only — core is framework-free)
- **Database:** none
- **Key libraries:** SappSounds (sibling checkout `../sappsounds`, target
  `Sapp::Sounds`), Catch2 v3.7.1 (tests)
- **Frontend:** JUCE editor (vector-drawn, "dark club")
- **Build / package manager:** CMake ≥ 3.24 + FetchContent

## Components

- **sappkit_core** (`src/core/`, no JUCE) — product policy over
  `sapp::sounds::PlaybackEngine`:
  - `KitModel` — 16-pad map from an SFZ definition (GM-aware naming,
    choke/layer/RR reporting) + `applyPadOverrides` (tune/decay/pan/level
    baked into a rebuilt region set).
  - `KitEngine` — kit bus: punch → crush → squash → room → width → limiter;
    humanize/quality hooks into the sampler.
  - `KitFx.h` — TransientShaper, BusCompressor, Crusher, SmallRoom DSP.
  - `KitRender` — deterministic offline render, SappLink CC steering.
  - `SappLinkCCMap` — the CC↔parameter contract (8 kit-bus params).
  - `DiagnosticKit` — generated in-memory GM kit (chokes, RRs, vel layers)
    for tests/CLI/plugin default.
- **sappkit-cli** (`src/cli/`) — agent JSON API: pads / inspect / validate /
  params / scan / render.
- **SappKitPlugin** (`src/plugin/`) — JUCE processor (APVTS, async SFZ load,
  debounced pad-override rebuild, SappLink CC slew) + editor (4×4 pad grid,
  pad edit strip, kit knobs, meters) + GET SOUNDS overlay (`SoundsPanel`:
  curated drum-library downloads + installed-kit browser over `~/Samples`).
- **SappKitUiShot** (`tools/uishot/`) — offscreen editor PNG + `--cctest`
  end-to-end SappLink proof.
- `src/core/KitMix.{h,cpp}` — persistent per-kit mixes: JSON
  serialize/parse (framework-free), note-keyed pad entries + bus values,
  stable mix-file naming (stem + path hash). Shared by plugin, CLI, agents.
- `src/core/KitModel` also owns `normalizeAriaMixerGates()` +
  `loadKitSfz()` — the kit-aware load path (parse, un-gate ARIA mixer
  kits, decode samples) used by the plugin AND the CLI.
- `src/plugin/UpdateManager.h` — in-plugin updater (background
  thread): GitHub latest-release check vs JucePlugin_VersionString,
  platform-asset download, install (SappKit.vst3/.component on
  macOS + xattr -rc; Windows rename-trick swap), standalone
  self-relaunch on macOS. `src/core/VersionCompare.h` does the
  semver-ish tag comparison.

## Data flow

```
MIDI (host / .mid) ──► PlaybackEngine (SappSounds: regions, chokes, RR,
                        vel layers, humanize)
                          │ dry stereo
                          ▼
             punch → crush → squash ──► SmallRoom ──┐
                          │ dry                     │ wet × roomLevel
                          ▼                         ▼
                        width (M/S) ◄───────────────┘
                          ▼
                 master gain → tanh limiter → out

Pad overrides: APVTS pad params ──(debounce timer)──► applyPadOverrides
  (definition copy, regions retuned/panned/trimmed/gated) ──► engine.setInstrument
SappLink CC-in: mapped CCs → APVTS slew (plugin) / KitParams (CLI render)
```

## Key directories

| Path | Purpose |
|---|---|
| `src/core/` | framework-free kit policy (consumed by plugin, CLI, tests) |
| `tests/data/` | vendored SappLink manifest (drift-guarded copy of sapptune's) |
| `tools/uishot/` | offscreen UI screenshot + CC smoke tool |
| `scripts/` | demo-groove composer/renderer |

## External touchpoints

- Sibling repo `~/apps/sappsounds` via `add_subdirectory` (falls back to
  GitHub FetchContent when absent).
- `~/apps/sapptune/sapplink/manifests/sappkit.json` — SappLink source of
  truth; vendored at `tests/data/sapplink-manifest.json`.
- Sample libraries in `~/Samples/` via sappsounds `fetch-library.sh`
  (avl-drumkits, sm-drums, big-rusty-drums, vsco2-ce). Never committed.

## Known sharp edges

- `applyPadOverrides` copies the whole `LoadedInstrument` (samples included).
  Fine for kit-sized libraries; the plugin debounces rebuilds (8 Hz timer) —
  don't call it per-block or per-knob-tick.
- Reuse the JUCE checkout when configuring:
  `-DFETCHCONTENT_SOURCE_DIR_JUCE=~/apps/sappsynth/build/_deps/juce-src`.
- Parameter IDs (kit + `pad<N>Tune/Decay/Pan/Level`) are compatibility
  contracts — never reuse or renumber.
