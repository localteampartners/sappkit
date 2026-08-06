#include "KitEngine.h"

#include <algorithm>
#include <cmath>

namespace sapp::kit {

using sapp::sounds::MidiEvent;

namespace {
inline float dbToGain(float db) noexcept { return std::pow(10.0f, db * 0.05f); }

inline float smoothCoef(double sampleRate, float ms) noexcept
{
    return 1.0f - std::exp(-1.0f / (float(sampleRate) * ms * 0.001f));
}
} // namespace

KitEngine::KitEngine() = default;

void KitEngine::prepare(double sampleRate, int maxBlockFrames)
{
    sampleRate_ = sampleRate;
    maxBlock_ = maxBlockFrames;
    sampler_.prepare(sampleRate, maxBlockFrames);
    punch_.prepare(sampleRate);
    crusher_.prepare(sampleRate);
    squash_.prepare(sampleRate);
    room_.prepare(sampleRate);

    const size_t n = size_t(maxBlockFrames);
    dryL_.assign(n, 0.0f);
    dryR_.assign(n, 0.0f);
    roomL_.assign(n, 0.0f);
    roomR_.assign(n, 0.0f);

    lastQuality_ = -1;
    lastHumanize_ = lastRoomSize_ = -1.0f;
}

void KitEngine::setInstrument(sapp::sounds::InstrumentPtr instrument)
{
    sampler_.setInstrument(std::move(instrument));
}
void KitEngine::collectRetired() { sampler_.collectRetired(); }
sapp::sounds::InstrumentPtr KitEngine::currentInstrument() const
{
    return sampler_.currentInstrument();
}

void KitEngine::setParams(const KitParams& params)
{
    const int inactive = 1 - paramIndex_.load(std::memory_order_acquire);
    paramSlots_[inactive] = params;
    paramIndex_.store(inactive, std::memory_order_release);
}

KitParams KitEngine::params() const
{
    return paramSlots_[paramIndex_.load(std::memory_order_acquire)];
}

void KitEngine::resetSequences() { sampler_.resetSequences(); }
void KitEngine::reseed(uint32_t seed) { sampler_.reseed(seed); }

void KitEngine::applyEngineHooks(const KitParams& p) noexcept
{
    if (p.quality != lastQuality_) {
        lastQuality_ = p.quality;
        sampler_.setInterpolationQuality(p.quality == 0 ? 0 : 1);
    }
    // Humanize: subtle per-hit tune scatter on top of whatever lorand/hirand
    // and *_random opcodes the library itself ships.
    if (p.humanize != lastHumanize_) {
        lastHumanize_ = p.humanize;
        sampler_.setRandomTuneCents(p.humanize * 10.0f);
    }
    if (p.roomSize != lastRoomSize_) {
        lastRoomSize_ = p.roomSize;
        room_.setSize(p.roomSize);
    }
}

void KitEngine::process(const MidiEvent* events, int eventCount,
                        float* outL, float* outR, int frames) noexcept
{
    const KitParams p = paramSlots_[paramIndex_.load(std::memory_order_acquire)];
    applyEngineHooks(p);

    // --- dry sampler render -------------------------------------------------
    const int n = std::min(frames, maxBlock_);
    std::fill(dryL_.begin(), dryL_.begin() + n, 0.0f);
    std::fill(dryR_.begin(), dryR_.begin() + n, 0.0f);
    sampler_.process(events, eventCount, dryL_.data(), dryR_.data(), n);

    // --- kit bus: punch → crush → squash ------------------------------------
    punch_.process(dryL_.data(), dryR_.data(), n, p.punch);
    crusher_.process(dryL_.data(), dryR_.data(), n, p.crush);
    squash_.process(dryL_.data(), dryR_.data(), n, p.squash);

    // --- room (fed post-bus so the ambience carries the kit's character) ----
    room_.process(dryL_.data(), dryR_.data(), roomL_.data(), roomR_.data(), n);

    const float roomTarget = std::clamp(p.roomLevel, 0.0f, 1.0f);
    const float masterTarget = dbToGain(p.masterGainDb);
    const float widthAmt = std::clamp(p.width, 0.0f, 2.0f);
    const float sm = smoothCoef(sampleRate_, 25.0f);

    for (int f = 0; f < n; ++f) {
        smRoom_ += sm * (roomTarget - smRoom_);
        smMaster_ += sm * (masterTarget - smMaster_);

        float l = dryL_[size_t(f)] + roomL_[size_t(f)] * smRoom_;
        float r = dryR_[size_t(f)] + roomR_[size_t(f)] * smRoom_;

        // Width (mid/side).
        const float mid = 0.5f * (l + r);
        const float side = 0.5f * (l - r) * widthAmt;
        l = (mid + side) * smMaster_;
        r = (mid - side) * smMaster_;

        if (p.limiter) {
            // Continuous soft saturation: ~transparent at low level, caps at ±1.
            l = std::tanh(l);
            r = std::tanh(r);
        }
        outL[f] = l;
        outR[f] = r;
    }
    for (int f = n; f < frames; ++f) { outL[f] = 0.0f; outR[f] = 0.0f; }
}

} // namespace sapp::kit
