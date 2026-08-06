# RUNBOOK — sappkit

<!-- UPDATE WHEN: any command here stops working, or a new operational task becomes routine enough to document -->

The authoritative source for "how do I operate this thing?"

---

## Run locally

### One-time setup

```bash
# Needs the sibling engine checkout at ~/apps/sappsounds (or it FetchContents
# from GitHub). Reuse the shared JUCE checkout to skip a 300 MB clone:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_SOURCE_DIR_JUCE="$HOME/apps/sappsynth/build/_deps/juce-src"
```

### Build + run the standalone app

```bash
cmake --build build -j8 --target SappKitPlugin_Standalone
open "build/SappKitPlugin_artefacts/Release/Standalone/SappKit.app"
```

VST3/AU: `cmake --build build -j8 --target SappKitPlugin_VST3 SappKitPlugin_AU`
(the JUCE copy step installs them to the user plugin folders).

### Run tests / fast loop

```bash
./verify.sh          # core+CLI+tests, under a minute
```

### UI screenshot (no screen session needed)

```bash
cmake --build build -j8 --target SappKitUiShot
./build/SappKitUiShot_artefacts/Release/SappKitUiShot.app/Contents/MacOS/SappKitUiShot /tmp/sappkit-ui.png
# SappLink end-to-end smoke through the plugin path:
./build/SappKitUiShot_artefacts/Release/SappKitUiShot.app/Contents/MacOS/SappKitUiShot --cctest
```

### Fetch kits + render the demo groove

```bash
~/apps/sappsounds/scripts/fetch-library.sh get avl-drumkits
python3 scripts/make_demo.py            # → /tmp/sappkit-demo.wav
```

---

## Deploy

Not deployed anywhere — local instrument. Publishing = pushing to
`localteampartners/sappkit` on GitHub.

### Rollback

```bash
git revert <sha>   # or /rollback if a suite-wide snapshot exists
```

---

## Debug checklist

1. `./verify.sh` — build or test failure narrows it immediately.
2. `./build/sappkit inspect --sfz <kit.sfz>` — parser diagnostics,
   missing-sample list, pad map for a misbehaving library.
3. `sappkit render --diagnostic` vs `--sfz` — engine issue vs library issue.
4. UiShot PNG for UI regressions; `--cctest` for SappLink regressions.
5. Choke/RR oddities: `inspect --regions` and compare group/off_by/seq fields.
