#pragma once
// KitEngine — SappKit's product policy wrapped around the generic
// sapp::sounds::PlaybackEngine.
//
// SappSounds owns: SFZ, samples, voices, chokes (group/off_by/off_mode),
// round robins, velocity layers, lorand/hirand, note_polyphony.
// SappKit owns (here): the kit bus — punch (transient emphasis), crush
// (vintage-sampler decimator), squash (bus compressor), width, tight
// small-room ambience, humanize amount, master output policy.
//
// Framework-independent: no JUCE. The JUCE plugin and the CLI both drive this.

#include <atomic>
#include <cstdint>
#include <vector>

#include <sapp/sounds/InstrumentDefinition.h>
#include <sapp/sounds/PlaybackEngine.h>

#include "KitFx.h"

namespace sapp::kit {

struct KitParams {
    // Kit bus character
    float punch = 0.35f;       // 0..1 transient emphasis
    float squash = 0.25f;      // 0..1 bus compression
    float crush = 0.0f;        // 0..1 bit/rate decimator character
    // Space
    float roomLevel = 0.18f;   // 0..1 small-room mix
    float roomSize = 0.4f;     // 0..1 closet → live room
    float width = 1.0f;        // 0 mono .. 2 wide
    // Feel
    float humanize = 0.15f;    // 0..1 → per-hit random tune breadth
    // Output
    float masterGainDb = 0.0f; // -24..+12
    bool limiter = true;
    int quality = 1;           // 0 draft (linear), 1 normal (cubic)
};

class KitEngine {
public:
    KitEngine();

    // --- control thread -----------------------------------------------------
    void prepare(double sampleRate, int maxBlockFrames);
    void setInstrument(sapp::sounds::InstrumentPtr instrument);
    void collectRetired();
    sapp::sounds::InstrumentPtr currentInstrument() const;

    void setParams(const KitParams& params);   // copied atomically
    KitParams params() const;

    void resetSequences();                     // round-robin counters
    void reseed(uint32_t seed);

    const sapp::sounds::PlaybackEngine& sampler() const { return sampler_; }
    sapp::sounds::PlaybackEngine& sampler() { return sampler_; }

    // --- audio thread -------------------------------------------------------
    // Replaces buffer contents (not additive). Events sorted by frame.
    void process(const sapp::sounds::MidiEvent* events, int eventCount,
                 float* outL, float* outR, int frames) noexcept;

private:
    void applyEngineHooks(const KitParams& p) noexcept;

    sapp::sounds::PlaybackEngine sampler_;
    TransientShaper punch_;
    Crusher crusher_;
    BusCompressor squash_;
    SmallRoom room_;

    // Double-buffered params: control writes inactive slot then flips.
    KitParams paramSlots_[2];
    std::atomic<int> paramIndex_{0};

    // Smoothed audio-thread state.
    float smRoom_ = 0.18f, smMaster_ = 1.0f;

    // Scratch buffers (allocated in prepare).
    std::vector<float> dryL_, dryR_, roomL_, roomR_;

    double sampleRate_ = 48000.0;
    int maxBlock_ = 0;
    int lastQuality_ = -1;
    float lastHumanize_ = -1.0f, lastRoomSize_ = -1.0f;
};

} // namespace sapp::kit
