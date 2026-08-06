#pragma once
// SappKit's kit-bus effects: punch (transient emphasis), squash (bus
// compressor), crush (vintage-sampler decimator), and a tight small-room
// early-reflection space (deliberately not a hall).
// Framework-independent, realtime-safe after prepare().

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sapp::kit {

namespace detail {
inline float envCoef(double sampleRate, float ms) noexcept
{
    return 1.0f - std::exp(-1.0f / (float(sampleRate) * std::max(0.01f, ms) * 0.001f));
}
} // namespace detail

// ----------------------------------------------------------------- punch ---
// Differential-envelope transient shaper, stereo-linked. amount 0..1 lifts
// attacks by up to ~9 dB without touching the sustain.
class TransientShaper {
public:
    void prepare(double sampleRate)
    {
        fastA_ = detail::envCoef(sampleRate, 0.4f);
        fastR_ = detail::envCoef(sampleRate, 28.0f);
        slowA_ = detail::envCoef(sampleRate, 22.0f);
        slowR_ = detail::envCoef(sampleRate, 140.0f);
        gainSm_ = detail::envCoef(sampleRate, 1.2f);
        envFast_ = envSlow_ = 0.0f;
        gain_ = 1.0f;
    }

    void process(float* l, float* r, int frames, float amount) noexcept
    {
        if (amount <= 0.0001f) return;
        for (int f = 0; f < frames; ++f) {
            const float x = std::max(std::abs(l[f]), std::abs(r[f]));
            envFast_ += (x > envFast_ ? fastA_ : fastR_) * (x - envFast_);
            envSlow_ += (x > envSlow_ ? slowA_ : slowR_) * (x - envSlow_);
            const float excess = std::max(0.0f, envFast_ / (envSlow_ + 1.0e-6f) - 1.0f);
            const float target = 1.0f + amount * 1.8f * std::min(excess, 1.0f);
            gain_ += gainSm_ * (target - gain_);
            l[f] *= gain_;
            r[f] *= gain_;
        }
    }

private:
    float fastA_ = 0, fastR_ = 0, slowA_ = 0, slowR_ = 0, gainSm_ = 0;
    float envFast_ = 0, envSlow_ = 0, gain_ = 1.0f;
};

// ---------------------------------------------------------------- squash ---
// Feed-forward stereo-linked bus compressor. amount 0..1 drives threshold
// down, ratio up, and adds matching makeup gain — one-knob glue.
class BusCompressor {
public:
    void prepare(double sampleRate)
    {
        attack_ = detail::envCoef(sampleRate, 4.0f);
        release_ = detail::envCoef(sampleRate, 130.0f);
        env_ = 0.0f;
        grDb_ = 0.0f;
    }

    void process(float* l, float* r, int frames, float amount) noexcept
    {
        if (amount <= 0.0001f) return;
        const float thresholdDb = -8.0f - 18.0f * amount;
        const float slope = 1.0f - 1.0f / (2.0f + 5.0f * amount);  // 1 - 1/ratio
        const float makeup = std::pow(10.0f, amount * 6.0f * 0.05f);
        for (int f = 0; f < frames; ++f) {
            const float x = std::max(std::abs(l[f]), std::abs(r[f]));
            env_ += (x > env_ ? attack_ : release_) * (x - env_);
            const float envDb = 20.0f * std::log10(env_ + 1.0e-6f);
            const float targetGr = std::min(0.0f, (thresholdDb - envDb) * slope);
            // Instant attack on gain reduction, smoothed release.
            grDb_ = targetGr < grDb_ ? targetGr : grDb_ + release_ * (targetGr - grDb_);
            const float g = std::pow(10.0f, grDb_ * 0.05f) * makeup;
            l[f] *= g;
            r[f] *= g;
        }
    }

private:
    float attack_ = 0, release_ = 0;
    float env_ = 0.0f, grDb_ = 0.0f;
};

// ----------------------------------------------------------------- crush ---
// Vintage-sampler character: sample-and-hold rate reduction + bit-depth
// quantization, mixed against the clean path. amount 0..1.
class Crusher {
public:
    void prepare(double) { holdCount_ = 0; heldL_ = heldR_ = 0.0f; }

    void process(float* l, float* r, int frames, float amount) noexcept
    {
        if (amount <= 0.0001f) return;
        const float bits = 16.0f - amount * 12.5f;              // 16 → 3.5 bits
        const float step = std::pow(2.0f, -(bits - 1.0f));
        const int hold = 1 + int(amount * amount * 7.0f);        // ÷1 → ÷8 rate
        const float mix = std::min(1.0f, amount * 1.6f);
        for (int f = 0; f < frames; ++f) {
            if (holdCount_ <= 0) {
                heldL_ = std::round(l[f] / step) * step;
                heldR_ = std::round(r[f] / step) * step;
                holdCount_ = hold;
            }
            --holdCount_;
            l[f] += mix * (heldL_ - l[f]);
            r[f] += mix * (heldR_ - r[f]);
        }
    }

private:
    int holdCount_ = 0;
    float heldL_ = 0.0f, heldR_ = 0.0f;
};

// ------------------------------------------------------------------ room ---
// Tight small room: early-reflection taps (3–26 ms, size-scaled) plus a very
// short 4-line FDN tail (T60 ≈ 0.12–0.55 s). Punchy ambience, not a hall.
class SmallRoom {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = sampleRate;
        buffer_.assign(size_t(sampleRate * 0.09) + 8, 0.0f);
        writePos_ = 0;
        erLpL_ = erLpR_ = 0.0f;

        static constexpr float baseMs[kLines] = {11.3f, 15.1f, 19.7f, 24.9f};
        for (int i = 0; i < kLines; ++i) {
            baseSamples_[i] = float(baseMs[i] * 0.001 * sampleRate);
            lines_[i].assign(size_t(baseSamples_[i] * 2.2f) + 32, 0.0f);
            linePos_[i] = 0;
            damp_[i] = 0.0f;
        }
        setSize(0.4f);
    }

    // size 0..1: closet → live room. Updates tap spread, tail length, damping.
    void setSize(float size)
    {
        size_ = std::clamp(size, 0.0f, 1.0f);
        const float spread = 0.55f + size_ * 0.9f;
        for (int t = 0; t < kTaps; ++t)
            tapSamples_[t] = float(kTapMs[t] * spread * 0.001 * sampleRate_);
        const float t60 = 0.12f + size_ * 0.43f;
        for (int i = 0; i < kLines; ++i) {
            delaySamples_[i] = std::min(baseSamples_[i] * (0.7f + size_ * 1.1f),
                                        float(lines_[i].size()) - 4.0f);
            feedback_[i] = std::pow(10.0f, -3.0f * delaySamples_[i] /
                                                (t60 * float(sampleRate_)));
        }
        dampCoef_ = 0.35f - size_ * 0.12f;
    }

    void process(const float* inL, const float* inR, float* outL, float* outR,
                 int frames) noexcept
    {
        const int size = int(buffer_.size());
        for (int f = 0; f < frames; ++f) {
            buffer_[size_t(writePos_)] = 0.5f * (inL[f] + inR[f]);

            float l = 0.0f, r = 0.0f;
            for (int t = 0; t < kTaps; ++t) {
                int idx = writePos_ - int(tapSamples_[t]);
                while (idx < 0) idx += size;
                const float v = buffer_[size_t(idx)] * kTapGain[t];
                if (t % 2 == 0) { l += v; r += v * 0.55f; }
                else            { r += v; l += v * 0.55f; }
            }
            erLpL_ += dampCoef_ * (l - erLpL_);
            erLpR_ += dampCoef_ * (r - erLpR_);
            l = erLpL_;
            r = erLpR_;

            // Tiny FDN tail fed by the ER field.
            const float inMono = 0.4f * (l + r);
            float read[kLines];
            float sum = 0.0f;
            for (int i = 0; i < kLines; ++i) {
                int idx = linePos_[i] - int(delaySamples_[i]);
                while (idx < 0) idx += int(lines_[i].size());
                read[i] = lines_[i][size_t(idx)];
                sum += read[i];
            }
            const float k = 0.5f;  // 2/kLines
            for (int i = 0; i < kLines; ++i) {
                float v = feedback_[i] * (read[i] - k * sum) + inMono;
                damp_[i] += 0.4f * (v - damp_[i]);
                lines_[i][size_t(linePos_[i])] = damp_[i];
                if (++linePos_[i] >= int(lines_[i].size())) linePos_[i] = 0;
            }

            outL[f] = l * 0.7f + (read[0] - read[2]) * 0.45f;
            outR[f] = r * 0.7f + (read[1] - read[3]) * 0.45f;

            if (++writePos_ >= size) writePos_ = 0;
        }
    }

private:
    static constexpr int kTaps = 8;
    static constexpr float kTapMs[kTaps] = {3.1f, 5.9f, 8.3f, 11.7f,
                                            14.9f, 18.1f, 21.7f, 25.9f};
    static constexpr float kTapGain[kTaps] = {0.9f, 0.76f, 0.63f, 0.52f,
                                              0.42f, 0.33f, 0.25f, 0.19f};
    static constexpr int kLines = 4;

    double sampleRate_ = 48000.0;
    std::vector<float> buffer_;
    int writePos_ = 0;
    float tapSamples_[kTaps] = {};
    float erLpL_ = 0.0f, erLpR_ = 0.0f;
    float dampCoef_ = 0.3f;

    std::array<std::vector<float>, kLines> lines_;
    float baseSamples_[kLines] = {}, delaySamples_[kLines] = {}, feedback_[kLines] = {};
    float damp_[kLines] = {};
    int linePos_[kLines] = {};
    float size_ = 0.4f;
};

} // namespace sapp::kit
