# TODO — sappkit

<!-- UPDATE WHEN: a task is added, completed, or re-prioritized -->

Short running task list. For "what exists *right now*," see [CURRENT_STATE.md](CURRENT_STATE.md).
For "what's broken," also see CURRENT_STATE.md's known-issues section.

---

## Next up (doing soon, in order)

1. Load-test SM Drums and Big Rusty Drums through the pad mapper (AVL and
   diagnostic are verified; these two are registered but unexercised).
2. Kit presets: save/recall pad-override sets + kit-bus settings per SFZ.
3. VSCO2-CE percussion kit generator (script that writes a GM-mapped SFZ
   over the raw WAVs — repo has samples only, no SFZ).

## Backlog (not prioritized)

- Pad mute/solo on the grid (alt-click).
- Choke-group editing (override the SFZ's groups per pad).
- Multi-out (pad → bus routing) for DAW mixing.
- sapptune end-to-end: groove spec → sappkit render via the manifest tags.
- Windows/Linux CI build of core+CLI (UiShot/AU stay APPLE-only).

## Ideas / maybe

- "Kit morph" — crossfade two SFZ kits per pad.
- Velocity-curve knob per pad.

---

## Done (recent, rolling)

- 2026-08-06 — GET SOUNDS in-plugin downloader (ported from sapporchestra):
  4 curated drum libraries + installed-kit browser.
- 2026-08-06 — v0.1.0: core + plugin + CLI + tests + UiShot + demo groove;
  SappLink manifest in sapptune; 3 drum-kit registry entries in sappsounds.
