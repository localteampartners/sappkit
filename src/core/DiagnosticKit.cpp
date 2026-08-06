#include "DiagnosticKit.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace sapp::kit {

using namespace sapp::sounds;

namespace {

constexpr double kPi = 3.14159265358979323846;

enum class DrumKind {
    Kick, SideStick, Snare, Clap, Tom, ClosedHat, PedalHat, OpenHat,
    Crash, Ride, RideBell, Tambourine, Cowbell, Conga
};

struct SynthSpec {
    DrumKind kind;
    float seconds;
    double f0 = 0.0;      // fundamental / sweep target where it applies
    int velLayer = 0;     // 0 soft, 1 hard
    int variant = 0;      // round robin / take index
};

// Deterministic per (note, layer, variant, seed) drum synthesis. Not meant to
// be a great-sounding kit — meant to be *distinct* per sound, layer, and take
// so tests can measure choke, RR, tune, decay, pan, and level behavior.
SampleData synthesizeDrum(int note, const SynthSpec& spec, uint32_t sampleRate,
                          uint32_t seed)
{
    SampleData s;
    s.sampleRate = sampleRate;
    s.channels = 2;
    const uint64_t frames = uint64_t(double(sampleRate) * spec.seconds);
    s.frames = frames;
    s.data.assign(2, std::vector<float>(size_t(frames), 0.0f));

    std::mt19937 rng(seed ^ uint32_t(note * 7919) ^ uint32_t(spec.variant * 104729) ^
                     uint32_t(spec.velLayer * 15485863));
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    auto noise = [&]() { return unit(rng) * 2.0 - 1.0; };

    const bool hard = spec.velLayer == 1;
    // Each take starts at a random phase so round robins are audibly distinct.
    const double phase0 = unit(rng) * 2.0 * kPi;

    double hp = 0.0, lp = 0.0;
    for (uint64_t i = 0; i < frames; ++i) {
        const double t = double(i) / double(sampleRate);
        double v = 0.0;

        switch (spec.kind) {
            case DrumKind::Kick: {
                const double f = spec.f0 * (1.0 + 2.1 * std::exp(-t / 0.032));
                v = std::sin(2.0 * kPi * f * t + phase0) * std::exp(-t / (hard ? 0.17 : 0.12));
                if (hard && t < 0.004) v += noise() * 0.5 * (1.0 - t / 0.004);
                break;
            }
            case DrumKind::SideStick:
                v = std::sin(2.0 * kPi * 815.0 * t + phase0) * std::exp(-t / 0.015) +
                    noise() * 0.6 * std::exp(-t / 0.004);
                break;
            case DrumKind::Snare: {
                const double tone = (std::sin(2.0 * kPi * 187.0 * t + phase0) +
                                     0.6 * std::sin(2.0 * kPi * 331.0 * t)) *
                                    std::exp(-t / 0.055);
                const double wires = noise() * std::exp(-t / (hard ? 0.16 : 0.11));
                v = tone * (hard ? 0.7 : 1.0) + wires * (hard ? 1.1 : 0.7);
                break;
            }
            case DrumKind::Clap: {
                double burst = 0.0;
                for (double onset : {0.0, 0.011, 0.023})
                    if (t >= onset) burst += std::exp(-(t - onset) / 0.009);
                burst += 0.4 * std::exp(-std::max(0.0, t - 0.023) / 0.09);
                v = noise() * burst;
                break;
            }
            case DrumKind::Tom: {
                const double f = spec.f0 * (1.0 + 0.55 * std::exp(-t / 0.045));
                v = std::sin(2.0 * kPi * f * t + phase0) * std::exp(-t / 0.24) +
                    noise() * 0.12 * std::exp(-t / 0.02);
                break;
            }
            case DrumKind::ClosedHat:
            case DrumKind::PedalHat:
            case DrumKind::OpenHat: {
                const double tau = spec.kind == DrumKind::OpenHat ? 0.34
                                   : spec.kind == DrumKind::PedalHat ? 0.022 : 0.028;
                v = noise() * std::exp(-t / tau) *
                    (0.7 + 0.3 * std::sin(2.0 * kPi * 6100.0 * t + phase0));
                break;
            }
            case DrumKind::Crash:
                v = noise() * std::exp(-t / 0.85) *
                    (0.6 + 0.4 * std::sin(2.0 * kPi * 3400.0 * t + phase0));
                break;
            case DrumKind::Ride:
                v = noise() * 0.7 * std::exp(-t / 0.55) +
                    std::sin(2.0 * kPi * 843.0 * t + phase0) * 0.4 * std::exp(-t / 0.35);
                break;
            case DrumKind::RideBell:
                v = (std::sin(2.0 * kPi * 1046.0 * t + phase0) +
                     0.7 * std::sin(2.0 * kPi * 1567.0 * t) +
                     0.4 * std::sin(2.0 * kPi * 2140.0 * t)) *
                    std::exp(-t / 0.28);
                break;
            case DrumKind::Tambourine: {
                double jingle = std::exp(-t / 0.07);
                if (t >= 0.015) jingle += 0.6 * std::exp(-(t - 0.015) / 0.06);
                v = noise() * jingle * (0.6 + 0.4 * std::sin(2.0 * kPi * 7300.0 * t + phase0));
                break;
            }
            case DrumKind::Cowbell:
                v = (std::sin(2.0 * kPi * 545.0 * t + phase0) +
                     0.8 * std::sin(2.0 * kPi * 812.0 * t)) *
                    std::exp(-t / 0.13);
                v = std::tanh(v * 2.2);
                break;
            case DrumKind::Conga: {
                const double f = spec.f0 * (1.0 + 0.2 * std::exp(-t / 0.02));
                v = std::sin(2.0 * kPi * f * t + phase0) * std::exp(-t / 0.15) +
                    noise() * 0.08 * std::exp(-t / 0.008);
                break;
            }
        }

        // Metallic sounds get a first-difference high-pass; drums a body LP.
        const bool metallic = spec.kind == DrumKind::ClosedHat || spec.kind == DrumKind::PedalHat ||
                              spec.kind == DrumKind::OpenHat || spec.kind == DrumKind::Crash ||
                              spec.kind == DrumKind::Tambourine;
        if (metallic) {
            const double out = v - hp;
            hp = v;
            v = out;
        } else {
            lp += 0.72 * (v - lp);
            v = lp;
        }

        // Slight stereo decorrelation for the long metals.
        const double spreadR = metallic ? noise() * 0.06 * std::exp(-t / 0.4) : 0.0;
        s.data[0][size_t(i)] = float(v);
        s.data[1][size_t(i)] = float(v * 0.97 + spreadR);
    }

    // Normalize to a consistent, healthy level (deterministic).
    float rawPeak = 0.0f;
    for (size_t c = 0; c < 2; ++c)
        for (float v : s.data[c]) rawPeak = std::max(rawPeak, std::abs(v));
    const float target = 0.7f;
    const float gain = rawPeak > 1.0e-6f ? target / rawPeak : 1.0f;
    double sumSq = 0.0;
    for (size_t c = 0; c < 2; ++c)
        for (float& v : s.data[c]) { v *= gain; sumSq += double(v) * v; }
    s.peak = target;
    s.rms = float(std::sqrt(sumSq / double(std::max<uint64_t>(1, frames * 2))));
    return s;
}

} // namespace

InstrumentPtr makeDiagnosticKit(const DiagnosticKitOptions& options)
{
    auto inst = std::make_shared<LoadedInstrument>();
    auto& def = inst->definition;
    def.name = "SappKit Diagnostic Kit";
    def.sourcePath = "";

    struct Sound {
        int note;
        const char* label;
        DrumKind kind;
        float seconds;
        double f0;
        int velLayers, roundRobins;
        int group, offBy;         // hi-hat choke family
        float pitchRandomCents, ampRandomDb;
        int notePolyphony;
        bool randTakes;           // lorand/hirand instead of seq round robin
    };
    const Sound sounds[] = {
        {36, "Kick",       DrumKind::Kick,       0.55f, 46.0,  2, 2, 0, 0, 0.0f, 0.0f, 2, false},
        {37, "SideStick",  DrumKind::SideStick,  0.20f, 0.0,   1, 2, 0, 0, 0.0f, 0.0f, 0, false},
        {38, "Snare",      DrumKind::Snare,      0.45f, 0.0,   2, 2, 0, 0, 0.0f, 0.0f, 0, false},
        {39, "Clap",       DrumKind::Clap,       0.40f, 0.0,   1, 2, 0, 0, 0.0f, 1.0f, 0, false},
        {41, "FloorTom",   DrumKind::Tom,        0.80f, 84.0,  1, 2, 0, 0, 6.0f, 0.0f, 0, false},
        {42, "ClosedHat",  DrumKind::ClosedHat,  0.14f, 0.0,   2, 2, 1, 1, 0.0f, 1.2f, 0, false},
        {44, "PedalHat",   DrumKind::PedalHat,   0.10f, 0.0,   1, 2, 1, 1, 0.0f, 1.2f, 0, false},
        {45, "LowTom",     DrumKind::Tom,        0.75f, 104.0, 1, 2, 0, 0, 6.0f, 0.0f, 0, false},
        {46, "OpenHat",    DrumKind::OpenHat,    1.40f, 0.0,   2, 2, 1, 1, 0.0f, 1.2f, 0, false},
        {48, "HiMidTom",   DrumKind::Tom,        0.70f, 128.0, 1, 2, 0, 0, 6.0f, 0.0f, 0, false},
        {49, "Crash",      DrumKind::Crash,      2.60f, 0.0,   1, 1, 0, 0, 0.0f, 0.0f, 0, false},
        {51, "Ride",       DrumKind::Ride,       1.80f, 0.0,   1, 2, 0, 0, 0.0f, 0.8f, 0, false},
        {53, "RideBell",   DrumKind::RideBell,   1.10f, 0.0,   1, 1, 0, 0, 0.0f, 0.0f, 0, false},
        {54, "Tambourine", DrumKind::Tambourine, 0.40f, 0.0,   1, 2, 0, 0, 0.0f, 1.5f, 0, true},
        {56, "Cowbell",    DrumKind::Cowbell,    0.50f, 0.0,   1, 1, 0, 0, 0.0f, 0.0f, 0, false},
        {63, "OpenConga",  DrumKind::Conga,      0.60f, 176.0, 1, 2, 0, 0, 8.0f, 0.0f, 0, false},
    };

    for (const auto& sound : sounds) {
        for (int vel = 0; vel < sound.velLayers; ++vel) {
            for (int rr = 0; rr < sound.roundRobins; ++rr) {
                SynthSpec spec{sound.kind, sound.seconds, sound.f0, vel, rr};
                SampleData s = synthesizeDrum(sound.note, spec, options.sampleRate,
                                              options.seed);
                s.relativePath = std::string("diagkit/") + sound.label + "_v" +
                                 std::to_string(vel) + "_rr" + std::to_string(rr) + ".gen";
                inst->samples.push_back(std::move(s));

                RegionDefinition r;
                r.sample = SampleIndex(inst->samples.size() - 1);
                r.samplePath = inst->samples.back().relativePath;
                r.loKey = r.hiKey = r.rootKey = uint8_t(sound.note);
                if (sound.velLayers == 2) {
                    r.loVel = vel == 0 ? 0 : 80;
                    r.hiVel = vel == 0 ? 79 : 127;
                }
                if (sound.randTakes) {
                    // Alternate takes chosen by the engine's random stream.
                    r.loRand = rr == 0 ? 0.0f : 0.5f;
                    r.hiRand = rr == 0 ? 0.5f : 1.0f;
                } else {
                    r.seqLength = uint16_t(sound.roundRobins);
                    r.seqPosition = uint16_t(rr + 1);
                }
                r.loop.mode = LoopMode::OneShot;
                r.loop.explicitMode = true;
                r.ampeg.attack = 0.0005f;
                r.ampeg.release = 0.25f;
                r.group = sound.group;
                r.offBy = sound.offBy;
                if (sound.offBy != 0) {
                    r.offMode = OffMode::Time;
                    r.offTime = 0.03f;
                }
                r.pitchRandomCents = sound.pitchRandomCents;
                r.ampRandomDb = sound.ampRandomDb;
                r.notePolyphony = sound.notePolyphony;
                r.volumeDb = -6.0f;
                def.regions.push_back(std::move(r));
            }
        }
    }

    def.loKeyUsed = 36;
    def.hiKeyUsed = 63;
    return inst;
}

} // namespace sapp::kit
