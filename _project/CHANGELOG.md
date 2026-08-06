# CHANGELOG — sappkit

<!-- UPDATE WHEN: you ship a meaningful change — feature, fix, migration, dependency bump that users/operators would care about. Trivial refactors don't belong here. -->

Newest first. Format: `## YYYY-MM-DD — short title`, then bullets.

---

## Unreleased

-

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
