#pragma once
// SappLink v1 CC-in mapping for SappKit (framework-free).
//
// Source of truth: ~/apps/sapptune/sapplink/manifests/sappkit.json —
// the unit test asserts this table matches the vendored copy in tests/data/.
// Parameter identity = the plugin's stable APVTS parameter IDs.
//
// Deliberately ABSENT from this table (engine-native or library-owned):
//   CC 64 → sustain   (real pedal semantics in SappSounds)
//   CC 1 / CC 11      (left free for library-authored SFZ CC conditions)
//   per-pad tune/decay/pan/level (64 params — plugin automation territory,
//   not a 7-bit live-CC surface)

#include <array>

#include "KitEngine.h"

namespace sapp::kit::sapplink {

enum class Curve { Linear, Log };

struct CCMapping {
    int cc;
    const char* paramId;          // stable APVTS parameter ID, verbatim
    float KitParams::* field;     // same parameter in the core struct
    float lo, hi;                 // engineering units at CC 0 and CC 127
    Curve curve;
};

inline constexpr int kNumMappings = 8;
const std::array<CCMapping, kNumMappings>& mappings();

// nullptr if this CC is not part of the SappLink contract.
const CCMapping* findMapping(int cc);

// CC value 0..127 → engineering units through the mapping's curve.
float ccToEngineering(const CCMapping& mapping, int ccValue);

// Offline/CLI path: apply a mapped CC to the params struct.
// Returns true if the CC was part of the mapping.
bool applyCcToParams(KitParams& params, int cc, int ccValue);

} // namespace sapp::kit::sapplink
