#pragma once
// KitModel — SappKit's 16-pad view over a generic SFZ instrument.
//
// SappSounds owns: SFZ, samples, voices, chokes (group/off_by), round robins,
// velocity layers, lorand/hirand humanize, note_polyphony.
// SappKit owns (here): the pad abstraction — which MIDI note is which sound,
// GM-drum-map-aware naming, choke-group reporting, and per-pad performance
// overrides (tune/decay/pan/level) baked into a rebuilt region set.
//
// Framework-independent: no JUCE. The plugin, the CLI, and the tests all
// consume this.

#include <array>
#include <filesystem>
#include <string>

#include <sapp/sounds/InstrumentDefinition.h>
#include <sapp/sounds/InstrumentLoader.h>

namespace sapp::kit {

inline constexpr int kNumPads = 16;

struct PadInfo {
    int note = -1;              // MIDI trigger note (-1 = empty pad)
    std::string name;           // GM drum name, or cleaned sample name
    int chokeGroup = 0;         // SFZ group= of this pad's regions (0 = none)
    int chokedBy = 0;           // SFZ off_by= (which group this pad silences)
    int regionCount = 0;
    int velocityLayers = 1;
    int roundRobins = 1;
    bool oneShot = false;       // loop_mode=one_shot (ignores note-off)
};

struct KitModel {
    std::array<PadInfo, kNumPads> pads{};
    int padCount = 0;
    int soundCount = 0;         // distinct trigger keys in the instrument

    int padIndexForNote(int note) const
    {
        for (int i = 0; i < padCount; ++i)
            if (pads[size_t(i)].note == note) return i;
        return -1;
    }
};

// GM percussion name for a MIDI note (35..81), or nullptr when the note has
// no GM meaning.
const char* gmDrumName(int note);

// Build the pad map from an instrument definition: one pad per distinct
// trigger key. When the instrument has more sounds than pads, GM-priority
// selection keeps the musically essential ones (kick/snare/hats first).
// Pads are ordered by ascending note.
KitModel buildKitModel(const sapp::sounds::InstrumentDefinition& def);

// ARIA multi-mic kits (DrumGizmo ports and friends) gate every region on
// mixer-slider CCs (locc>=1) that a drum plugin never sends, so out of the
// box the whole kit is silent. Normalize to a sensible default mix: a
// region whose gate includes a per-drum channel CC (one that gates only a
// few trigger notes across the kit) is a CLOSE MIC — its unsatisfiable
// gates are stripped so it plays. Regions gated only by kit-wide CCs
// (bleed, overheads, room) stay muted — SappKit has its own room. CCs with
// an explicit set_cc default are host-meaningful and left untouched.
// Returns the number of regions un-gated; 0 means not an ARIA mixer kit.
int normalizeAriaMixerGates(sapp::sounds::InstrumentDefinition& def);

// Kit-aware SFZ load: parse, normalize ARIA mixer gates, then decode
// samples. The plugin and the CLI both load kits through this so a
// DrumGizmo-style kit sounds the same everywhere.
sapp::sounds::LoadResult loadKitSfz(const std::filesystem::path& sfzPath);

// Per-pad performance overrides, applied at the region-policy layer.
struct PadOverride {
    float tuneSemis = 0.0f;   // -12..+12 semitones
    float decay = 1.0f;       // 0..1; 1 = natural envelope, <1 imposes decay
    float pan = 0.0f;         // -1..+1
    float levelDb = 0.0f;     // -24..+12 dB

    bool isDefault() const
    {
        return tuneSemis == 0.0f && decay >= 0.999f && pan == 0.0f && levelDb == 0.0f;
    }
};
using PadOverrides = std::array<PadOverride, kNumPads>;

// Rebuild an instrument with pad overrides baked into its regions
// (tune → tune_cents, level → volume_db, pan → region pan, decay → imposed
// ampeg decay-to-zero). Copies the definition and shares nothing mutable;
// sample audio is copied too (LoadedInstrument is a value snapshot), which is
// fine for drum-kit-sized libraries — callers debounce interactive rebuilds.
// Returns `base` unchanged when every override is default.
sapp::sounds::InstrumentPtr applyPadOverrides(const sapp::sounds::InstrumentPtr& base,
                                              const KitModel& model,
                                              const PadOverrides& overrides);

} // namespace sapp::kit
