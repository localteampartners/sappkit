#pragma once
// Deterministic offline render through the full kit chain
// (sampler → punch → crush → squash → room → width → limiter).

#include <cstdint>
#include <vector>

#include <sapp/sounds/MidiFile.h>

#include "KitEngine.h"
#include "KitModel.h"

namespace sapp::kit {

struct KitRenderOptions {
    double sampleRate = 48000.0;
    int blockFrames = 512;
    double tailSeconds = 2.0;
    uint32_t seed = 0x5A9F00D5;
    KitParams params;
    PadOverrides padOverrides{};   // applied at the region-policy layer
};

struct KitRenderOutput {
    std::vector<float> left, right;
    double sampleRate = 48000.0;
    float peak = 0.0f;
    float rms = 0.0f;
};

KitRenderOutput renderKit(const sapp::sounds::InstrumentPtr& instrument,
                          const std::vector<sapp::sounds::TimedMidiEvent>& events,
                          const KitRenderOptions& options);

} // namespace sapp::kit
