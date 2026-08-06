#include "SappLinkCCMap.h"

#include <algorithm>
#include <cmath>

namespace sapp::kit::sapplink {

// CC assignment follows the SappLink conventions (PROTOCOL.md): standard MMA
// CCs where one exists (7 volume, 91 reverb send), free CCs 14–31 otherwise.
// Ranges are the plugin's real APVTS ranges — the manifest mirrors these.
const std::array<CCMapping, kNumMappings>& mappings()
{
    static const std::array<CCMapping, kNumMappings> table { {
        { 7,  "masterGain", &KitParams::masterGainDb, -24.0f, 12.0f, Curve::Linear },
        { 14, "punch",      &KitParams::punch,        0.0f,   1.0f,  Curve::Linear },
        { 15, "squash",     &KitParams::squash,       0.0f,   1.0f,  Curve::Linear },
        { 16, "crush",      &KitParams::crush,        0.0f,   1.0f,  Curve::Linear },
        { 17, "width",      &KitParams::width,        0.0f,   2.0f,  Curve::Linear },
        { 18, "humanize",   &KitParams::humanize,     0.0f,   1.0f,  Curve::Linear },
        { 91, "roomLevel",  &KitParams::roomLevel,    0.0f,   1.0f,  Curve::Linear },
        { 92, "roomSize",   &KitParams::roomSize,     0.0f,   1.0f,  Curve::Linear },
    } };
    return table;
}

const CCMapping* findMapping(int cc)
{
    for (const auto& mapping : mappings())
        if (mapping.cc == cc)
            return &mapping;
    return nullptr;
}

float ccToEngineering(const CCMapping& mapping, int ccValue)
{
    const float t = float(std::clamp(ccValue, 0, 127)) / 127.0f;
    if (mapping.curve == Curve::Log)
        return mapping.lo * std::pow(mapping.hi / mapping.lo, t);
    return mapping.lo + (mapping.hi - mapping.lo) * t;
}

bool applyCcToParams(KitParams& params, int cc, int ccValue)
{
    const auto* mapping = findMapping(cc);
    if (mapping == nullptr)
        return false;
    params.*(mapping->field) = ccToEngineering(*mapping, ccValue);
    return true;
}

} // namespace sapp::kit::sapplink
