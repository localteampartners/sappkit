# sappkit

<!-- UPDATE WHEN: the one-line description changes, or the repo's top-level layout changes -->

SappKit — punchy, fun drums & percussion instrument (JUCE Standalone/VST3/AU)
built on the [SappSounds](https://github.com/localteampartners/sappsounds)
engine. 16-pad kit model over SFZ drum libraries (GM-drum-map aware), per-pad
tune/decay/pan/level, kit-wide punch/squash/crush/room, dark-club UI, agent
CLI with deterministic renders, SappLink CC-in.

## Quickstart

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_SOURCE_DIR_JUCE="$HOME/apps/sappsynth/build/_deps/juce-src"
cmake --build build -j8 --target SappKitPlugin_Standalone sappkit-cli SappKitTests
./build/SappKitTests                       # 28 tests: chokes, RR, pads, determinism
open "build/SappKitPlugin_artefacts/Release/Standalone/SappKit.app"
```

Fetch a real kit and render a groove:

```bash
~/apps/sappsounds/scripts/fetch-library.sh get avl-drumkits
python3 scripts/make_demo.py               # → /tmp/sappkit-demo.wav
```

Agent API (JSON on stdout, seeded determinism — full contract in
[docs/agent_api.md](docs/agent_api.md)):

```bash
./build/sappkit pads --sfz ~/Samples/avl-drumkits/AVL_Drumkits_1.0/Black_Pearl_5pc.sfz
./build/sappkit render --diagnostic --midi groove.mid --out take.wav --seed 42
```

## Layout

- `src/core/` — framework-free kit policy (pads, overrides, bus FX, render)
- `src/plugin/` — JUCE processor + editor; `tools/uishot/` — offscreen UI PNG
- `src/cli/` — the `sappkit` agent CLI
- `docs/` — [agent_api.md](docs/agent_api.md), [sapplink.md](docs/sapplink.md)

## Project documentation

All orientation docs live in [`_project/`](_project/). Start with
[_project/README.md](_project/README.md) — it's a 1-page index into everything
else (spec, architecture, current state, runbook, decisions, etc.).

If you're an agent opening this repo, read [CLAUDE.md](CLAUDE.md) first.

## Where releases are built

Tags are built by a **self-hosted GitHub Actions runner on the Windows
machine** (`desktop-14886fp`), not by GitHub's hosted runners — hosted minutes
are billed and the account is currently blocked. Windows jobs read:

```yaml
runs-on: ${{ vars.WINDOWS_RUNNER || 'windows-latest' }}
```

so the repo variable `WINDOWS_RUNNER=self-hosted` sends builds to that
machine, and deleting the variable sends them back to GitHub. No workflow
edits either way.

**Every repo needs its own runner.** The account is a GitHub *user*, not an
organisation, and user accounts can't share runners across repos — so each
repo gets its own registration (its own folder and Windows service) on the
same machine. The prerequisites are installed once and shared: Git, CMake
3.24+, and Visual Studio 2022 Build Tools with the "Desktop development with
C++" workload.

Full setup, including the per-repo registration steps:
[sapptune/RUNNER.md](https://github.com/localteampartners/sapptune/blob/master/RUNNER.md).

**Builds are Windows-only** — macOS jobs were removed on 2026-08-08.
