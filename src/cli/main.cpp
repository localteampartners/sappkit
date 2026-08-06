// sappkit — the SappKit agent/automation CLI.
//
// This is the machine API for external software (e.g. MIDI-generation
// agents): discover the pad map (note → sound name, chokes, layers),
// validate SFZ, dump the parameter schema, and render MIDI through the full
// kit chain. Every output is a single JSON document on stdout.
//
//   sappkit inspect  (--sfz <f.sfz> | --diagnostic) [--regions]
//   sappkit pads     (--sfz <f.sfz> | --diagnostic)
//   sappkit validate --sfz <f.sfz>
//   sappkit params
//   sappkit scan <library-dir> [--all]
//   sappkit render   (--sfz <f.sfz> | --diagnostic) --midi <f.mid>
//                    --out <f.wav> [--sr N] [--seed N] [--tail S]
//                    [--param NAME=VALUE ...]
//
// See docs/agent_api.md for the full contract.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <sapp/sounds/InstrumentLoader.h>
#include <sapp/sounds/MidiFile.h>
#include <sapp/sounds/SfzParser.h>
#include <sapp/sounds/WavIo.h>

#include "../core/DiagnosticKit.h"
#include "../core/KitRender.h"
#include "../core/SappLinkCCMap.h"
#include "Json.h"

using namespace sapp::sounds;
using namespace sapp::kit;
using sapptools::JsonWriter;

namespace {

const char* noteName(int note)
{
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static char buf[8];
    std::snprintf(buf, sizeof(buf), "%s%d", names[((note % 12) + 12) % 12], note / 12 - 1);
    return buf;
}

struct ParamSpec {
    const char* name;      // CLI --param name (snake_case)
    const char* apvtsId;   // stable plugin parameter ID (= SappLink manifest id)
    float KitParams::* field;
    float lo, hi, def;
    const char* doc;
};

// Single source of truth for the kit-wide float parameters an agent may set.
// MIDI CC reachability comes from the SappLink table (core/SappLinkCCMap.cpp).
const ParamSpec kParams[] = {
    {"punch", "punch", &KitParams::punch, 0.0f, 1.0f, 0.35f,
     "Transient emphasis: lifts attacks without touching sustain. 0 off, 1 maximum snap."},
    {"squash", "squash", &KitParams::squash, 0.0f, 1.0f, 0.25f,
     "One-knob bus compressor: threshold down, ratio up, makeup gain matched. Glue at ~0.3, pump at 1."},
    {"crush", "crush", &KitParams::crush, 0.0f, 1.0f, 0.0f,
     "Vintage-sampler character: bit-depth + sample-rate reduction mixed against the clean path."},
    {"room_level", "roomLevel", &KitParams::roomLevel, 0.0f, 1.0f, 0.18f,
     "Tight small-room ambience level (early reflections + very short tail — not a hall)."},
    {"room_size", "roomSize", &KitParams::roomSize, 0.0f, 1.0f, 0.4f,
     "Room size: 0 closet, 1 live room."},
    {"width", "width", &KitParams::width, 0.0f, 2.0f, 1.0f,
     "Stereo width: 0 mono, 1 natural, 2 wide."},
    {"humanize", "humanize", &KitParams::humanize, 0.0f, 1.0f, 0.15f,
     "Per-hit random tune scatter on top of the library's own round robins and *_random opcodes."},
    {"master_gain_db", "masterGain", &KitParams::masterGainDb, -24.0f, 12.0f, 0.0f,
     "Master output gain in dB."},
};

// Pad override params: pad<1-16>_tune/_decay/_pan/_level, 1-based pad index.
struct PadParamSpec {
    const char* suffix;
    float PadOverride::* field;
    float lo, hi, def;
    const char* doc;
};
const PadParamSpec kPadParams[] = {
    {"tune", &PadOverride::tuneSemis, -12.0f, 12.0f, 0.0f,
     "Pad tune in semitones."},
    {"decay", &PadOverride::decay, 0.0f, 1.0f, 1.0f,
     "Pad decay: 1 = natural sample length, 0 = ~20 ms gate. Applied at the region-policy layer."},
    {"pan", &PadOverride::pan, -1.0f, 1.0f, 0.0f,
     "Pad pan, -1 left to +1 right."},
    {"level", &PadOverride::levelDb, -24.0f, 12.0f, 0.0f,
     "Pad level trim in dB."},
};

// "pad3_tune=..." → pad index 2 + spec. Returns false if not a pad param.
bool parsePadParam(const std::string& name, int& padIndex, const PadParamSpec*& spec)
{
    if (name.rfind("pad", 0) != 0) return false;
    size_t i = 3;
    if (i >= name.size() || !std::isdigit(uint8_t(name[i]))) return false;
    int num = 0;
    while (i < name.size() && std::isdigit(uint8_t(name[i])))
        num = num * 10 + (name[i++] - '0');
    if (i >= name.size() || name[i] != '_') return false;
    const std::string suffix = name.substr(i + 1);
    for (const auto& p : kPadParams) {
        if (suffix == p.suffix) {
            if (num < 1 || num > kNumPads) return false;
            padIndex = num - 1;
            spec = &p;
            return true;
        }
    }
    return false;
}

InstrumentPtr loadInstrument(const std::string& sfzPath, bool useDiagnostic,
                             std::vector<Diagnostic>& diags,
                             std::vector<std::string>& missing)
{
    if (useDiagnostic) return makeDiagnosticKit();
    InstrumentLoader loader;
    auto result = loader.loadSfz(sfzPath);
    diags = result.diagnostics;
    missing = result.missingSamples;
    return result.ok ? result.instrument : nullptr;
}

void writeDiagnostics(JsonWriter& w, const std::vector<Diagnostic>& diags)
{
    w.key("diagnostics");
    w.beginArray();
    for (const auto& d : diags) {
        w.beginObject();
        w.field("severity", d.severity == Severity::Error ? "error"
                          : d.severity == Severity::Warning ? "warning" : "info");
        w.field("file", d.file);
        w.field("line", d.line);
        w.field("message", d.message);
        w.endObject();
    }
    w.endArray();
}

void writePads(JsonWriter& w, const KitModel& model)
{
    w.key("pads");
    w.beginArray();
    for (int i = 0; i < model.padCount; ++i) {
        const auto& pad = model.pads[size_t(i)];
        w.beginObject();
        w.field("pad", i + 1);
        w.field("note", pad.note);
        w.field("noteName", noteName(pad.note));
        w.field("name", pad.name);
        if (const char* gm = gmDrumName(pad.note)) w.field("gmName", gm);
        w.field("chokeGroup", pad.chokeGroup);
        w.field("chokedBy", pad.chokedBy);
        w.field("velocityLayers", pad.velocityLayers);
        w.field("roundRobins", pad.roundRobins);
        w.field("oneShot", pad.oneShot);
        w.field("regions", pad.regionCount);
        w.endObject();
    }
    w.endArray();
    w.field("padCount", model.padCount);
    w.field("soundCount", model.soundCount);
}

int cmdInspect(const std::string& sfzPath, bool useDiagnostic, bool dumpRegions,
               bool padsOnly)
{
    std::vector<Diagnostic> diags;
    std::vector<std::string> missing;
    auto inst = loadInstrument(sfzPath, useDiagnostic, diags, missing);

    JsonWriter w;
    w.beginObject();
    if (!inst) {
        w.field("ok", false);
        writeDiagnostics(w, diags);
        w.endObject();
        std::printf("%s\n", w.str().c_str());
        return 2;
    }
    const auto& def = inst->definition;
    const KitModel model = buildKitModel(def);

    w.field("ok", true);
    w.field("name", def.name);
    if (!padsOnly) {
        w.field("source", def.sourcePath.empty() ? std::string("(generated)") : def.sourcePath);
        w.field("regions", uint64_t(def.regions.size()));
        w.field("missingSamples", uint64_t(missing.size()));
        w.field("estimatedRamBytes", inst->sampleBytes());
    }

    writePads(w, model);

    if (!padsOnly) {
        std::set<int> chokeGroups;
        int maxVelocityLayers = 1, maxRoundRobins = 1;
        for (int i = 0; i < model.padCount; ++i) {
            const auto& pad = model.pads[size_t(i)];
            if (pad.chokeGroup != 0) chokeGroups.insert(pad.chokeGroup);
            maxVelocityLayers = std::max(maxVelocityLayers, pad.velocityLayers);
            maxRoundRobins = std::max(maxRoundRobins, pad.roundRobins);
        }
        w.key("capabilities");
        w.beginObject();
        w.field("velocityLayers", maxVelocityLayers);
        w.field("roundRobins", maxRoundRobins);
        w.field("chokeGroups", uint64_t(chokeGroups.size()));
        w.endObject();

        // Controller conventions the kit engine responds to.
        w.key("controllers");
        w.beginArray();
        w.beginObject();
        w.field("cc", 64);
        w.field("role", "sustain");
        w.field("doc", "Sustain pedal (SappSounds-native). One-shot drum regions ignore note-off anyway.");
        w.endObject();
        for (const auto& m : sapp::kit::sapplink::mappings()) {
            w.beginObject();
            w.field("cc", m.cc);
            w.field("role", m.paramId);
            w.field("doc", "SappLink CC-in: steers this kit parameter live (see params).");
            w.endObject();
        }
        w.endArray();

        if (dumpRegions) {
            w.key("regionDetails");
            w.beginArray();
            for (const auto& r : def.regions) {
                w.beginObject();
                w.field("sample", r.samplePath);
                w.field("loKey", int(r.loKey));
                w.field("hiKey", int(r.hiKey));
                w.field("rootKey", int(r.rootKey));
                w.field("loVel", int(r.loVel));
                w.field("hiVel", int(r.hiVel));
                w.field("group", int(r.group));
                w.field("offBy", int(r.offBy));
                w.field("seqPosition", int(r.seqPosition));
                w.field("seqLength", int(r.seqLength));
                w.field("missing", r.sample == kInvalidSample);
                w.endObject();
            }
            w.endArray();
        }

        writeDiagnostics(w, diags);
    }
    w.endObject();
    std::printf("%s\n", w.str().c_str());
    return missing.empty() ? 0 : 1;
}

int cmdValidate(const std::string& sfzPath)
{
    std::vector<Diagnostic> diags;
    std::vector<std::string> missing;
    auto inst = loadInstrument(sfzPath, false, diags, missing);

    int errors = 0, warnings = 0;
    for (const auto& d : diags) {
        if (d.severity == Severity::Error) ++errors;
        else if (d.severity == Severity::Warning) ++warnings;
    }

    JsonWriter w;
    w.beginObject();
    w.field("ok", inst != nullptr);
    w.field("file", sfzPath);
    w.field("errors", errors);
    w.field("warnings", warnings);
    w.field("missingSamples", uint64_t(missing.size()));
    if (inst) {
        w.field("regions", uint64_t(inst->definition.regions.size()));
        w.key("unsupportedOpcodes");
        w.beginArray();
        for (const auto& o : inst->definition.unsupportedOpcodes) w.value(o);
        w.endArray();
    }
    writeDiagnostics(w, diags);
    w.endObject();
    std::printf("%s\n", w.str().c_str());
    return inst == nullptr ? 2 : (warnings > 0 || !missing.empty() ? 1 : 0);
}

int cmdParams()
{
    JsonWriter w;
    w.beginObject();
    w.field("ok", true);
    w.field("product", "SappKit");
    w.key("params");
    w.beginArray();
    for (const auto& p : kParams) {
        w.beginObject();
        w.field("name", p.name);
        w.field("id", p.apvtsId);
        w.field("min", double(p.lo));
        w.field("max", double(p.hi));
        w.field("default", double(p.def));
        // MIDI reachability (SappLink).
        for (const auto& m : sapp::kit::sapplink::mappings()) {
            if (std::string(m.paramId) == p.apvtsId) {
                w.field("cc", m.cc);
                w.field("ccCurve", m.curve == sapp::kit::sapplink::Curve::Log ? "log" : "linear");
                break;
            }
        }
        w.field("doc", p.doc);
        w.endObject();
    }
    w.endArray();
    // Per-pad overrides: 16 pads × 4 params, addressed by pad number (1-based,
    // ascending note order — discover the mapping with `sappkit pads`).
    w.key("padParams");
    w.beginArray();
    for (const auto& p : kPadParams) {
        w.beginObject();
        w.key("pattern");
        {
            std::string pattern = "pad<1-16>_";
            pattern += p.suffix;
            w.value(pattern);
        }
        w.field("id", (std::string("pad<1-16>") +
                       char(std::toupper(uint8_t(p.suffix[0]))) + (p.suffix + 1)));
        w.field("min", double(p.lo));
        w.field("max", double(p.hi));
        w.field("default", double(p.def));
        w.field("doc", p.doc);
        w.endObject();
    }
    w.endArray();
    w.key("enums");
    w.beginObject();
    w.key("quality");
    w.beginArray();
    w.value("draft");
    w.value("normal");
    w.endArray();
    w.endObject();
    w.endObject();
    std::printf("%s\n", w.str().c_str());
    return 0;
}

int cmdScan(int argc, char** argv)
{
    std::string dir;
    bool includePartials = false;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--all") includePartials = true;
        else dir = arg;
    }
    if (dir.empty()) {
        std::fprintf(stderr, "usage: sappkit scan <library-dir> [--all]\n");
        return 2;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        std::fprintf(stderr, "error: not a directory: %s\n", dir.c_str());
        return 2;
    }

    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file(ec)) continue;
        auto ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (ext != ".sfz") continue;
        // Skip include-partials (conventionally kept in "includes/" folders)
        // unless --all: they are fragments, not playable instruments.
        if (!includePartials) {
            bool partial = false;
            for (const auto& part : it->path().parent_path())
                if (part == "includes") partial = true;
            if (partial) continue;
        }
        files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());

    SfzParser parser;
    JsonWriter w;
    w.beginObject();
    w.field("ok", true);
    w.field("root", dir);
    w.key("instruments");
    w.beginArray();
    size_t playable = 0;
    for (const auto& file : files) {
        auto parsed = parser.parseFile(file);
        const auto& def = parsed.instrument;
        if (def.regions.empty()) continue;
        ++playable;
        const KitModel model = buildKitModel(def);
        w.beginObject();
        w.field("path", file.string());
        w.field("name", def.name);
        w.field("category",
                fs::relative(file.parent_path(), dir, ec).string());
        w.field("regions", uint64_t(def.regions.size()));
        w.field("sounds", model.soundCount);
        w.field("lowKey", int(def.loKeyUsed));
        w.field("highKey", int(def.hiKeyUsed));
        w.endObject();
    }
    w.endArray();
    w.field("count", uint64_t(playable));
    w.endObject();
    std::printf("%s\n", w.str().c_str());
    return 0;
}

int cmdRender(int argc, char** argv)
{
    std::string sfzPath, midiPath, outPath;
    bool useDiagnostic = false;
    KitRenderOptions options;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--sfz") sfzPath = next();
        else if (arg == "--midi") midiPath = next();
        else if (arg == "--out") outPath = next();
        else if (arg == "--diagnostic") useDiagnostic = true;
        else if (arg == "--sr") options.sampleRate = std::atof(next().c_str());
        else if (arg == "--seed") options.seed = uint32_t(std::strtoul(next().c_str(), nullptr, 10));
        else if (arg == "--tail") options.tailSeconds = std::atof(next().c_str());
        else if (arg == "--param") {
            const std::string kv = next();
            const size_t eq = kv.find('=');
            if (eq == std::string::npos) {
                std::fprintf(stderr, "error: --param expects NAME=VALUE, got '%s'\n", kv.c_str());
                return 2;
            }
            const std::string name = kv.substr(0, eq);
            const float value = float(std::atof(kv.c_str() + eq + 1));
            bool found = false;
            for (const auto& p : kParams) {
                if (name == p.name) {
                    options.params.*(p.field) = std::clamp(value, p.lo, p.hi);
                    found = true;
                    break;
                }
            }
            int padIndex = -1;
            const PadParamSpec* padSpec = nullptr;
            if (!found && parsePadParam(name, padIndex, padSpec)) {
                options.padOverrides[size_t(padIndex)].*(padSpec->field) =
                    std::clamp(value, padSpec->lo, padSpec->hi);
                found = true;
            }
            if (name == "quality") { options.params.quality = int(value); found = true; }
            if (name == "limiter") { options.params.limiter = value >= 0.5f; found = true; }
            if (!found) {
                std::fprintf(stderr, "error: unknown param '%s' (see: sappkit params)\n",
                             name.c_str());
                return 2;
            }
        }
    }

    if ((sfzPath.empty() && !useDiagnostic) || midiPath.empty() || outPath.empty()) {
        std::fprintf(stderr, "usage: sappkit render (--sfz <f.sfz> | --diagnostic) "
                             "--midi <f.mid> --out <f.wav> [--sr N] [--seed N] [--tail S] "
                             "[--param NAME=VALUE ...]\n");
        return 2;
    }

    std::vector<Diagnostic> diags;
    std::vector<std::string> missing;
    auto inst = loadInstrument(sfzPath, useDiagnostic, diags, missing);
    if (!inst) {
        JsonWriter w;
        w.beginObject();
        w.field("ok", false);
        w.field("error", "failed to load instrument");
        writeDiagnostics(w, diags);
        w.endObject();
        std::printf("%s\n", w.str().c_str());
        return 2;
    }

    auto midi = readMidiFile(midiPath);
    if (!midi.ok) {
        std::fprintf(stderr, "error: %s: %s\n", midiPath.c_str(), midi.error.c_str());
        return 2;
    }

    auto rendered = renderKit(inst, midi.events, options);
    if (rendered.left.empty() ||
        !writeWavFile(outPath, rendered.left.data(), rendered.right.data(),
                      rendered.left.size(), uint32_t(options.sampleRate), true)) {
        std::fprintf(stderr, "error: render/write failed\n");
        return 2;
    }

    JsonWriter w;
    w.beginObject();
    w.field("ok", true);
    w.field("out", outPath);
    w.field("sampleRate", options.sampleRate);
    w.field("frames", uint64_t(rendered.left.size()));
    w.field("durationSeconds", double(rendered.left.size()) / options.sampleRate);
    w.field("peak", double(rendered.peak));
    w.field("rms", double(rendered.rms));
    w.field("midiEvents", uint64_t(midi.events.size()));
    w.field("seed", uint64_t(options.seed));
    w.endObject();
    std::printf("%s\n", w.str().c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr,
                     "sappkit — SappKit agent CLI\n"
                     "  sappkit inspect  (--sfz <f.sfz> | --diagnostic) [--regions]\n"
                     "  sappkit pads     (--sfz <f.sfz> | --diagnostic)\n"
                     "  sappkit validate --sfz <f.sfz>\n"
                     "  sappkit params\n"
                     "  sappkit scan <library-dir> [--all]\n"
                     "  sappkit render   (--sfz | --diagnostic) --midi <f.mid> --out <f.wav>\n"
                     "                   [--seed N] [--param NAME=VALUE ...]\n");
        return 2;
    }
    const std::string cmd = argv[1];
    std::string sfzPath;
    bool useDiagnostic = false, dumpRegions = false;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--sfz" && i + 1 < argc) sfzPath = argv[++i];
        else if (arg == "--diagnostic") useDiagnostic = true;
        else if (arg == "--regions") dumpRegions = true;
    }

    if (cmd == "inspect") return cmdInspect(sfzPath, useDiagnostic, dumpRegions, false);
    if (cmd == "pads") return cmdInspect(sfzPath, useDiagnostic, false, true);
    if (cmd == "validate") {
        if (sfzPath.empty()) { std::fprintf(stderr, "validate requires --sfz\n"); return 2; }
        return cmdValidate(sfzPath);
    }
    if (cmd == "params") return cmdParams();
    if (cmd == "scan") return cmdScan(argc, argv);
    if (cmd == "render") return cmdRender(argc, argv);

    std::fprintf(stderr, "unknown command '%s'\n", cmd.c_str());
    return 2;
}
