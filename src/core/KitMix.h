#pragma once
// KitMix — a saved per-kit mix: pad overrides keyed by MIDI note plus the
// kit-bus settings. One human-editable JSON file per kit under the shared
// Sapp settings dir (KitMixes/), so the plugin, the CLI, and agents all read
// and write the same mix and a kit sounds the same every time it loads.
//
// Pads are keyed by NOTE, not pad index: the pad list is derived from the
// loaded SFZ, so indices can shift between library versions while the
// trigger notes stay put.
//
// Framework-independent: no JUCE.

#include <string>
#include <utility>
#include <vector>

#include "KitModel.h"

namespace sapp::kit {

struct PadMixEntry {
    int note = -1;        // MIDI trigger note this entry applies to
    std::string name;     // pad display name at save time (informational)
    PadOverride mix;      // tune/decay/pan/level
};

struct KitMix {
    static constexpr int kVersion = 1;
    std::string kitPath;   // absolute SFZ path ("" = built-in diagnostic kit)
    std::string kitName;   // informational
    std::vector<PadMixEntry> pads;                    // non-default pads only
    std::vector<std::pair<std::string, double>> bus;  // APVTS param id -> value

    bool empty() const { return pads.empty() && bus.empty(); }
    const double* busValue(const std::string& id) const
    {
        for (const auto& kv : bus)
            if (kv.first == id) return &kv.second;
        return nullptr;
    }
    void setBus(const std::string& id, double v)
    {
        for (auto& kv : bus)
            if (kv.first == id) { kv.second = v; return; }
        bus.emplace_back(id, v);
    }
};

// Pretty, stable JSON (safe to hand-edit and diff).
std::string serializeKitMix(const KitMix& mix);

// Tolerant parse of the format serializeKitMix writes (and hand edits of
// it). Returns false on malformed input; `out` is untouched on failure.
bool parseKitMix(const std::string& json, KitMix& out);

// File name for a kit's mix inside the KitMixes dir: "<stem>-<hash8>.json".
// The stem keeps mixes recognizable; the path hash keeps same-named kits
// from different libraries apart. Empty kitPath -> "diagnostic-kit.json".
std::string kitMixFileName(const std::string& kitPath);

// Apply a mix's pad entries onto an overrides array using the model's
// note->pad binding. Entries whose note is not in the model are ignored.
// Returns the number of pads applied.
int applyMixToOverrides(const KitMix& mix, const KitModel& model, PadOverrides& out);

// Capture the current overrides as a mix (non-default pads only, named from
// the model). Bus values are the caller's business.
KitMix captureMix(const std::string& kitPath, const std::string& kitName,
                  const KitModel& model, const PadOverrides& overrides);

} // namespace sapp::kit
