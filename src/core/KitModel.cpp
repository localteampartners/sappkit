#include "KitModel.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <vector>

namespace sapp::kit {

using namespace sapp::sounds;

const char* gmDrumName(int note)
{
    switch (note) {
        case 35: return "Kick 2";
        case 36: return "Kick";
        case 37: return "Side Stick";
        case 38: return "Snare";
        case 39: return "Clap";
        case 40: return "Snare 2";
        case 41: return "Low Floor Tom";
        case 42: return "Closed Hat";
        case 43: return "High Floor Tom";
        case 44: return "Pedal Hat";
        case 45: return "Low Tom";
        case 46: return "Open Hat";
        case 47: return "Low-Mid Tom";
        case 48: return "High-Mid Tom";
        case 49: return "Crash";
        case 50: return "High Tom";
        case 51: return "Ride";
        case 52: return "China";
        case 53: return "Ride Bell";
        case 54: return "Tambourine";
        case 55: return "Splash";
        case 56: return "Cowbell";
        case 57: return "Crash 2";
        case 58: return "Vibraslap";
        case 59: return "Ride 2";
        case 60: return "High Bongo";
        case 61: return "Low Bongo";
        case 62: return "Mute Conga";
        case 63: return "Open Conga";
        case 64: return "Low Conga";
        case 65: return "High Timbale";
        case 66: return "Low Timbale";
        case 67: return "High Agogo";
        case 68: return "Low Agogo";
        case 69: return "Cabasa";
        case 70: return "Maracas";
        case 71: return "Short Whistle";
        case 72: return "Long Whistle";
        case 73: return "Short Guiro";
        case 74: return "Long Guiro";
        case 75: return "Claves";
        case 76: return "High Woodblock";
        case 77: return "Low Woodblock";
        case 78: return "Mute Cuica";
        case 79: return "Open Cuica";
        case 80: return "Mute Triangle";
        case 81: return "Open Triangle";
        default: return nullptr;
    }
}

namespace {

// The trigger key a region answers to. Percussion regions are usually
// lokey==hikey; when a region spans a range, the root key names the sound.
int regionKey(const RegionDefinition& r)
{
    if (r.rootKey >= r.loKey && r.rootKey <= r.hiKey) return r.rootKey;
    return r.loKey;
}

// GM-priority ranking for pad selection: the essential drum-set voices come
// first, then remaining GM notes, then everything else by ascending pitch.
int padPriority(int note)
{
    static constexpr int kCore[] = {36, 38, 42, 46, 49, 51, 39, 37, 41, 45,
                                    48, 43, 44, 53, 54, 56, 35, 40, 50, 47,
                                    55, 57, 59, 52, 58};
    for (size_t i = 0; i < sizeof(kCore) / sizeof(kCore[0]); ++i)
        if (kCore[i] == note) return int(i);
    if (gmDrumName(note) != nullptr) return 100 + note;
    return 1000 + note;
}

// "BDrumNewhit_v3_rr1_Sum.wav" → "BDrumNewhit"; used when a note has no GM
// name. Strips path, extension, and the common vN/rrN/layer suffix tokens.
std::string cleanSampleName(const std::string& path)
{
    std::string base = path;
    const auto slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    const auto dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);

    // Drop trailing tokens like _v1, _rr2, _Sum, -1, numbers-only chunks.
    auto isNoiseToken = [](const std::string& t) {
        if (t.empty()) return true;
        std::string lower;
        for (char c : t) lower += char(std::tolower(uint8_t(c)));
        if (lower == "sum" || lower == "mono" || lower == "stereo") return true;
        auto digitsFrom = [&](size_t i) {
            if (i >= lower.size()) return false;
            for (; i < lower.size(); ++i)
                if (!std::isdigit(uint8_t(lower[i]))) return false;
            return true;
        };
        if (lower[0] == 'v' && digitsFrom(1)) return true;
        if (lower.rfind("rr", 0) == 0 && digitsFrom(2)) return true;
        return digitsFrom(0);
    };
    while (true) {
        const auto sep = base.find_last_of("_-");
        if (sep == std::string::npos || sep == 0) break;
        if (!isNoiseToken(base.substr(sep + 1))) break;
        base = base.substr(0, sep);
    }
    return base.empty() ? std::string("Pad") : base;
}

} // namespace

KitModel buildKitModel(const InstrumentDefinition& def)
{
    struct SoundStats {
        std::vector<int> loVels;
        int maxSeq = 1;
        int chokeGroup = 0, chokedBy = 0;
        int regions = 0;
        bool oneShot = false;
        std::string firstSample;
    };
    std::map<int, SoundStats> sounds;

    for (const auto& r : def.regions) {
        if (r.trigger == TriggerMode::Release || r.trigger == TriggerMode::ReleaseKey)
            continue;
        auto& s = sounds[regionKey(r)];
        ++s.regions;
        s.loVels.push_back(r.loVel);
        s.maxSeq = std::max(s.maxSeq, int(r.seqLength));
        if (s.chokeGroup == 0 && r.group != 0) s.chokeGroup = int(r.group);
        if (s.chokedBy == 0 && r.offBy != 0) s.chokedBy = int(r.offBy);
        if (r.loop.mode == LoopMode::OneShot) s.oneShot = true;
        if (s.firstSample.empty()) s.firstSample = r.samplePath;
    }

    std::vector<int> keys;
    keys.reserve(sounds.size());
    for (const auto& [key, stats] : sounds) keys.push_back(key);
    std::sort(keys.begin(), keys.end(), [](int a, int b) {
        const int pa = padPriority(a), pb = padPriority(b);
        return pa != pb ? pa < pb : a < b;
    });
    if (keys.size() > size_t(kNumPads)) keys.resize(size_t(kNumPads));
    std::sort(keys.begin(), keys.end());

    KitModel model;
    model.soundCount = int(sounds.size());
    for (int key : keys) {
        const auto& s = sounds[key];
        PadInfo pad;
        pad.note = key;
        if (const char* gm = gmDrumName(key)) pad.name = gm;
        else pad.name = cleanSampleName(s.firstSample);
        pad.chokeGroup = s.chokeGroup;
        pad.chokedBy = s.chokedBy;
        pad.regionCount = s.regions;
        std::vector<int> vels = s.loVels;
        std::sort(vels.begin(), vels.end());
        vels.erase(std::unique(vels.begin(), vels.end()), vels.end());
        pad.velocityLayers = int(vels.size());
        pad.roundRobins = s.maxSeq;
        pad.oneShot = s.oneShot;
        model.pads[size_t(model.padCount++)] = std::move(pad);
    }
    return model;
}

InstrumentPtr applyPadOverrides(const InstrumentPtr& base, const KitModel& model,
                                const PadOverrides& overrides)
{
    if (!base) return base;
    bool anyChange = false;
    for (const auto& o : overrides) anyChange |= !o.isDefault();
    if (!anyChange) return base;

    auto modified = std::make_shared<LoadedInstrument>(*base);
    for (auto& r : modified->definition.regions) {
        if (r.trigger == TriggerMode::Release || r.trigger == TriggerMode::ReleaseKey)
            continue;
        const int padIndex = model.padIndexForNote(regionKey(r));
        if (padIndex < 0) continue;
        const PadOverride& o = overrides[size_t(padIndex)];
        if (o.isDefault()) continue;

        r.tuneCents += o.tuneSemis * 100.0f;
        r.volumeDb += o.levelDb;
        r.pan = std::clamp(r.pan + o.pan * 100.0f, -100.0f, 100.0f);
        if (o.decay < 0.999f) {
            // Imposed decay-to-zero: knob 0 ≈ 20 ms, knob 1 = natural length.
            const float d = std::clamp(o.decay, 0.0f, 1.0f);
            r.ampeg.hold = 0.0f;
            r.ampeg.decay = 0.02f + d * d * 1.98f;
            r.ampeg.sustain = 0.0f;
        }
    }
    return modified;
}

} // namespace sapp::kit
