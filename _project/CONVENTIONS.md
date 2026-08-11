# CONVENTIONS — sappkit

<!-- UPDATE WHEN: you learn (or are corrected on) a non-obvious workflow fact — a deploy quirk, version pin, build gotcha, naming rule, or "never do X here." If a session had to rediscover it, it belongs in this file. -->

How we work on this project — facts that aren't derivable from the code and
that every new session would otherwise relearn the hard way. One or two lines
per entry: the rule, then the why (when the why isn't obvious).

## Deploy & operations

<!-- FILL IN: e.g., "Deploy = commit, push, then `vps-proxy run 'git pull && pm2 reload'`. Never rsync/scp files to the server." -->
-

## Toolchain & versions

- JUCE is pinned to 8.0.15. A worktree or fresh plugin build can reuse the
  shared checkout: `-DFETCHCONTENT_SOURCE_DIR_JUCE=/Users/michael/apps/sappsynth/build/_deps/juce-src`.
- The version lives in exactly one place: `project(SappKit VERSION ...)` in
  CMakeLists.txt. The CI release guard reads that and nothing else, and the
  in-plugin updater compares it against the newest GitHub tag.

## Build / test gotchas

- Two build trees: `build/` (core + CLI + tests, plugin OFF) is the fast
  inner loop; `build-plugin/` carries the plugin, the headless harness and
  UiShot. `verify.sh` drives both — never verify with the plugin target off
  (sappkeys#1: tests green while the installed binary stayed stale).
- The headless station regression is `sappkit-headless selftest` (CTest
  target `headless`). It links the REAL processor, so it only exists in the
  plugin tree.
- Its fixture is `tests/data/kit-headless/`, shaped like a GET SOUNDS samples
  root; `$SAPP_SAMPLES_ROOT` points the plugin at it so nothing depends on an
  installed sample library. Regenerate with
  `python3 scripts/make_headless_fixture.py`.
- Run `auval -v aumu Skit Ltpr` after touching parameters. A parameter the
  PLUGIN rewrites (like `libraryReady`) must be declared `withMeta(true)` or
  auval fails its "parameter values across initialization" check.

## Code & naming rules

<!-- FILL IN: repo-specific rules Claude has been taught, e.g., "sync Xcode file groups; never hand-edit project.pbxproj" -->
-

## Never do

- **Never install an instrument from `MessageManager::callAsync`, and never
  drive loading from a `juce::Timer`.** A VST3 plug-in in a non-JUCE
  headless host (sappradio) has a MessageManager that nobody pumps: both
  fire never, silently. That was issue #1 — the plugin rendered -200 dBFS.
  Every install goes through the loader thread's LoadJob queue.
- Never detach a `std::thread` whose closure captures `this`. It outlives
  the processor; the loader thread is joined in the destructor instead.
- Never ship macOS artifacts. macOS builds are local verification only —
  releases are Windows x64 (suite rule).
- Never renumber or reuse a parameter id, and never insert a new parameter
  anywhere but the END of the layout: ids and indices are a contract with
  saved DAW sessions and the SappLink manifest.
