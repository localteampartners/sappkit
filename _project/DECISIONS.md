# DECISIONS — sappkit

<!-- UPDATE WHEN: you make a non-obvious choice (library pick, architectural pattern, tradeoff). One entry per decision, newest at top. -->

The *why* behind choices that aren't self-evident from the code.

---

## 2026-08-10 — A loader thread installs kits; the message loop is never load-bearing

**Decision:** every instrument install — construction diagnostic, MIDI/host
program change, the `preset` parameter, user presets, host state restore,
the sounds browser — is enqueued as a `LoadJob` and performed by a dedicated
loader thread, which also runs the 8 Hz pad-override / mix-save tick. The
`juce::Timer` is reduced to an editor hook, and the thread is joined in the
destructor.
**Context:** issue #1. A VST3 plug-in inside a non-JUCE headless host has a
MessageManager that nobody pumps, so `MessageManager::callAsync` and
`juce::Timer` never fire — silently. Measured -200.00 dBFS, 0 voices, 0
pads, with the plugin contributing exactly zero samples. Identical to
sapporchestra #2 and sappchoir #1.
**Alternatives considered:** pumping the loop ourselves from `processBlock`
(rejected — a plug-in must never drive the host's message loop, and it would
run SFZ parsing on the audio thread); longer settle windows in the station
(rejected — waiting does not turn a loop nobody is turning).
**Tradeoffs:** parameter writes such as `applySavedMixOrDefaults` now happen
off the message thread. That is already how CC-in works here, and it goes
through the same normalized `setValueNotifyingHost` path host automation
uses. `updateHostDisplay` still needs the message thread, so it is deferred
to the timer via a flag — it is a host-notification nicety, never a
correctness requirement.

## 2026-08-10 — `libraryReady` lives outside the APVTS and is declared meta

**Decision:** a `juce::AudioParameterBool` added last with `addParameter()`,
non-automatable and `withMeta(true)`, never in the parameter tree.
**Context:** the station needs to poll readiness instead of guessing a
settle window (sappradio#3). Keeping it out of the APVTS means host state
can never restore a stale "ready", and it never lands in a saved user
preset. Appended last so no existing automation index moves.
**Alternatives considered:** a meter-category parameter (rejected — the
value still only reaches the VST3 controller through
`outputParameterChanges`, so it buys nothing); an APVTS entry (rejected —
restorable staleness).
**Tradeoffs:** through the VST3 controller the value appears only after a
`processBlock`; a host holding the AudioProcessor sees it immediately.
`withMeta` is required or auval fails its parameter-persistence check,
because the plugin keeps rewriting a value the host wrote.

## 2026-08-06 — Pad overrides rebuild the instrument, not the voice path

**Decision:** per-pad tune/decay/pan/level are baked into a copied
`InstrumentDefinition` (region tune_cents / volume / pan / imposed ampeg
decay) and swapped in via `setInstrument`, debounced at 8 Hz in the plugin.
**Context:** SappSounds regions are immutable snapshots; the engine has no
per-note gain/tune hooks beyond global humanize.
**Alternatives considered:** engine API additions (rejected — SappSounds
stays product-neutral and is shared with sapporchestra); per-pad post-mix
processing (impossible — voices mix inside the engine).
**Tradeoffs:** rebuild copies sample data (tens of MB for drum kits, ~ms);
interactive knob drags land on the debounce tick, not per-sample.
**Revisit if:** kits get orchestral-sized or pad params need host automation
at audio rate — then add a region-gain hook to SappSounds.

## 2026-08-06 — Kit-wide SappLink surface only (8 CCs)

**Decision:** the manifest maps masterGain/punch/squash/crush/width/
humanize/roomLevel/roomSize; per-pad params are plugin-automation only.
CC 64 stays engine-native; CC 1/11 stay free for library SFZ conditions.
**Context:** 16 pads × 4 params = 64 CCs would exhaust the free-CC space and
mean nothing on other instruments.
**Revisit if:** sapptune needs per-pad moves — 14-bit CC pairs or an OSC v2
side-channel are the reserved paths.

## 2026-08-06 — Decay knob imposes an envelope instead of scaling one

**Decision:** pad decay < 1 sets `ampeg hold=0, sustain=0, decay=0.02+d²·1.98s`
on the pad's regions; decay = 1 leaves the library's natural envelope.
**Context:** drum SFZs mostly ship no meaningful ampeg (one-shots play out);
"scale the existing decay" would do nothing on most kits.
**Tradeoffs:** can shorten but not lengthen a sound. That's the honest
physical limit of one-shots anyway.

## 2026-08-06 — Room is ER + tiny FDN, capped at T60 ≈ 0.55 s

**Decision:** `SmallRoom` = 8 early taps (3–26 ms, size-scaled) + 4-line FDN
with T60 0.12–0.55 s. No hall mode.
**Context:** product brief: punchy club drums. A hall smears transients; a
DAW send covers anyone who wants wash. Mirrors sapporchestra's "algorithmic
first" reasoning at the opposite room size.

## 2026-08-06 — GM-priority pad selection

**Decision:** when a kit has >16 sounds, pads keep a ranked GM core
(kick, snare, hats, crash, ride, …) before exotic sounds; unmapped notes are
still playable — pads are a view, not a filter.
**Context:** agents and humans both expect kick/snare/hats to always be on
the grid (AVL 5pc has 29 sounds).

## 2026-08-06 — Product/engine split, JUCE pin, CLI-as-agent-API

Same rationale as sapporchestra (see its DECISIONS.md): generic sampler code
lives in SappSounds; JUCE 8.0.15 pinned to match sappsynth/sapporchestra;
the agent API is a JSON CLI, not a socket; the SappLink table lives in core
with a vendored-manifest drift test.

## 2026-08-07 — Self-update via GitHub releases, versioned by the CMake project

The plugin updates itself from the repo's *latest GitHub release* rather
than a separate update feed: CI already attaches Windows-x64 and
macOS-universal zips to every tag, so the release IS the feed. The
installed version is `JucePlugin_VersionString`, which JUCE derives from
`project(SappKit VERSION ...)` — therefore the CMake version MUST be
bumped with every release tag (RUNBOOK rule) or the updater goes blind.
Daily check throttled through the shared Sapp settings file (one file for
the whole product family, per-product key `lastUpdateCheck-sappkit`).
Windows can't overwrite a loaded DLL but can rename it: old .vst3 is
parked as `.old-<tag>` and the new one copied in, with rollback on failure.
