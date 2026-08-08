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
#include "../core/KitMix.h"
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
    auto result = loadKitSfz(sfzPath);  // parse + ARIA-gate normalize + decode
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

// -------------------------------------------------------------------- mix ---
// Persistent per-kit mixes (core/KitMix.h): the same JSON files the plugin
// auto-loads and auto-saves. `mix set` lets an agent fix "the ride is too
// loud" in one line: sappkit mix set --sfz kit.sfz ride.level=-6

std::string mixDirPath()
{
#if defined(_WIN32)
    const char* base = std::getenv("APPDATA");
    return std::string(base ? base : ".") + "\\Sapp\\KitMixes";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/Library/Application Support/Sapp/KitMixes";
#else
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.config/Sapp/KitMixes";
#endif
}

// Same identity the plugin uses: absolute, lexically normal path.
std::string canonicalKitPath(const std::string& sfzPath)
{
    if (sfzPath.empty()) return sfzPath;
    std::error_code ec;
    auto abs = std::filesystem::absolute(sfzPath, ec);
    if (ec) return sfzPath;
    return abs.lexically_normal().string();
}

std::string mixFilePathFor(const std::string& canonicalPath)
{
    return mixDirPath() + "/" + kitMixFileName(canonicalPath);
}

bool readTextFile(const std::string& path, std::string& out)
{
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? size_t(n) : 0);
    const size_t got = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    out.resize(got);
    return true;
}

bool writeTextFile(const std::string& path, const std::string& text)
{
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    return ok;
}

// Pad map without decoding audio — mix edits on gigabyte kits stay instant.
bool buildModelForMix(const std::string& sfzPath, bool useDiagnostic,
                      KitModel& model, std::string& kitName)
{
    if (useDiagnostic) {
        auto inst = makeDiagnosticKit();
        model = buildKitModel(inst->definition);
        kitName = inst->definition.name;
        return true;
    }
    SfzParser parser;
    auto parsed = parser.parseFile(sfzPath);
    if (!parsed.ok || parsed.hasErrors()) return false;
    model = buildKitModel(parsed.instrument);
    kitName = parsed.instrument.name;
    return true;
}

std::string lowercase(std::string s)
{
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

// Resolve a mix target to pad indices: "pad3", "note51", or a
// case-insensitive substring of the pad name ("ride", "crash"). Substrings
// may match several pads (all get the edit — "cymbals" style intent).
std::vector<int> resolvePads(const KitModel& model, const std::string& target)
{
    std::vector<int> pads;
    if (target.rfind("pad", 0) == 0 && target.size() > 3 &&
        std::isdigit(uint8_t(target[3]))) {
        const int n = std::atoi(target.c_str() + 3);
        if (n >= 1 && n <= model.padCount) pads.push_back(n - 1);
        return pads;
    }
    if (target.rfind("note", 0) == 0 && target.size() > 4 &&
        std::isdigit(uint8_t(target[4]))) {
        const int idx = model.padIndexForNote(std::atoi(target.c_str() + 4));
        if (idx >= 0) pads.push_back(idx);
        return pads;
    }
    const std::string want = lowercase(target);
    for (int i = 0; i < model.padCount; ++i) {
        if (lowercase(model.pads[size_t(i)].name).find(want) != std::string::npos)
            pads.push_back(i);
    }
    return pads;
}

void writeMixJson(JsonWriter& w, const KitMix& mix, const KitModel& model,
                  const std::string& file, bool exists)
{
    w.field("file", file);
    w.field("exists", exists);
    w.field("kit", mix.kitPath.empty() ? "(diagnostic)" : mix.kitPath);
    w.field("name", mix.kitName);
    w.key("pads");
    w.beginArray();
    for (int i = 0; i < model.padCount; ++i) {
        const auto& info = model.pads[size_t(i)];
        PadOverride effective;  // defaults unless the mix has this note
        bool saved = false;
        for (const auto& e : mix.pads)
            if (e.note == info.note) { effective = e.mix; saved = true; break; }
        w.beginObject();
        w.field("pad", i + 1);
        w.field("note", info.note);
        w.field("name", info.name);
        w.field("tune", double(effective.tuneSemis));
        w.field("decay", double(effective.decay));
        w.field("pan", double(effective.pan));
        w.field("level", double(effective.levelDb));
        w.field("saved", saved);
        w.endObject();
    }
    w.endArray();
    w.key("bus");
    w.beginObject();
    for (const auto& kv : mix.bus) w.field(kv.first, kv.second);
    w.endObject();
}

int cmdMix(int argc, char** argv)
{
    const std::string sub = argc >= 3 ? argv[2] : "";
    std::string sfzPath;
    bool useDiagnostic = false;
    std::vector<std::string> assignments;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--sfz" && i + 1 < argc) sfzPath = argv[++i];
        else if (arg == "--diagnostic") useDiagnostic = true;
        else if (arg.find('=') != std::string::npos) assignments.push_back(arg);
    }
    const bool haveKit = useDiagnostic || !sfzPath.empty();
    if ((sub != "show" && sub != "set" && sub != "clear") || !haveKit) {
        std::fprintf(stderr,
                     "usage: sappkit mix show  (--sfz <f.sfz> | --diagnostic)\n"
                     "       sappkit mix set   (--sfz <f.sfz> | --diagnostic) TARGET.PARAM=VALUE ...\n"
                     "       sappkit mix clear (--sfz <f.sfz> | --diagnostic)\n"
                     "  TARGET: pad<1-16> | note<N> | pad-name substring (ride, crash, snare...)\n"
                     "  PARAM:  tune (-12..12) | decay (0..1) | pan (-1..1) | level (dB, -24..12)\n"
                     "  bus:    bus.<param>=VALUE (punch, squash, crush, roomLevel, roomSize,\n"
                     "          width, humanize, masterGain, limiter, quality)\n");
        return 2;
    }

    const std::string kitPath = useDiagnostic ? "" : canonicalKitPath(sfzPath);
    const std::string file = mixFilePathFor(kitPath);

    if (sub == "clear") {
        std::error_code ec;
        const bool existed = std::filesystem::remove(file, ec);
        JsonWriter w;
        w.beginObject();
        w.field("ok", !ec);
        w.field("file", file);
        w.field("removed", existed);
        w.endObject();
        std::printf("%s\n", w.str().c_str());
        return ec ? 2 : 0;
    }

    KitModel model;
    std::string kitName;
    if (!buildModelForMix(sfzPath, useDiagnostic, model, kitName)) {
        std::fprintf(stderr, "error: cannot parse '%s'\n", sfzPath.c_str());
        return 2;
    }

    KitMix mix;
    std::string text;
    const bool exists = readTextFile(file, text) && parseKitMix(text, mix);
    mix.kitPath = kitPath;
    mix.kitName = kitName;

    if (sub == "show") {
        JsonWriter w;
        w.beginObject();
        w.field("ok", true);
        writeMixJson(w, mix, model, file, exists);
        w.endObject();
        std::printf("%s\n", w.str().c_str());
        return 0;
    }

    // --- set ---
    if (assignments.empty()) {
        std::fprintf(stderr, "error: mix set needs TARGET.PARAM=VALUE assignments\n");
        return 2;
    }
    struct Applied { int note; std::string name, param; double value; };
    std::vector<Applied> applied;
    for (const auto& a : assignments) {
        const size_t eq = a.find('=');
        const size_t dot = a.rfind('.', eq);
        if (dot == std::string::npos || dot == 0 || dot > eq) {
            std::fprintf(stderr, "error: expected TARGET.PARAM=VALUE, got '%s'\n", a.c_str());
            return 2;
        }
        const std::string target = a.substr(0, dot);
        const std::string param = a.substr(dot + 1, eq - dot - 1);
        const double value = std::atof(a.c_str() + eq + 1);

        if (target == "bus") {
            bool known = false;
            for (const auto& p : kParams)
                if (param == p.apvtsId || param == p.name) {
                    mix.setBus(p.apvtsId, std::clamp(value, double(p.lo), double(p.hi)));
                    known = true;
                    break;
                }
            if (param == "limiter") { mix.setBus("limiter", value >= 0.5 ? 1 : 0); known = true; }
            if (param == "quality") { mix.setBus("quality", std::clamp(value, 0.0, 1.0)); known = true; }
            if (!known) {
                std::fprintf(stderr, "error: unknown bus param '%s'\n", param.c_str());
                return 2;
            }
            applied.push_back({-1, "bus", param, value});
            continue;
        }

        const auto pads = resolvePads(model, target);
        if (pads.empty()) {
            std::fprintf(stderr, "error: no pad matches '%s' (see: sappkit pads)\n",
                         target.c_str());
            return 2;
        }
        for (int idx : pads) {
            const auto& info = model.pads[size_t(idx)];
            PadMixEntry* entry = nullptr;
            for (auto& e : mix.pads)
                if (e.note == info.note) { entry = &e; break; }
            if (entry == nullptr) {
                mix.pads.push_back({info.note, info.name, {}});
                entry = &mix.pads.back();
            }
            entry->name = info.name;
            if (param == "tune") entry->mix.tuneSemis = std::clamp(float(value), -12.0f, 12.0f);
            else if (param == "decay") entry->mix.decay = std::clamp(float(value), 0.0f, 1.0f);
            else if (param == "pan") entry->mix.pan = std::clamp(float(value), -1.0f, 1.0f);
            else if (param == "level") entry->mix.levelDb = std::clamp(float(value), -24.0f, 12.0f);
            else {
                std::fprintf(stderr, "error: unknown pad param '%s' "
                                     "(tune|decay|pan|level)\n", param.c_str());
                return 2;
            }
            applied.push_back({info.note, info.name, param, value});
        }
    }

    // Drop entries that ended up all-default; keep the file minimal.
    mix.pads.erase(std::remove_if(mix.pads.begin(), mix.pads.end(),
                                  [](const PadMixEntry& e) { return e.mix.isDefault(); }),
                   mix.pads.end());

    if (!writeTextFile(file, serializeKitMix(mix))) {
        std::fprintf(stderr, "error: cannot write %s\n", file.c_str());
        return 2;
    }

    JsonWriter w;
    w.beginObject();
    w.field("ok", true);
    w.key("applied");
    w.beginArray();
    for (const auto& ap : applied) {
        w.beginObject();
        if (ap.note >= 0) w.field("note", ap.note);
        w.field("target", ap.name);
        w.field("param", ap.param);
        w.field("value", ap.value);
        w.endObject();
    }
    w.endArray();
    writeMixJson(w, mix, model, file, true);
    w.endObject();
    std::printf("%s\n", w.str().c_str());
    return 0;
}

int cmdRender(int argc, char** argv)
{
    std::string sfzPath, midiPath, outPath;
    bool useDiagnostic = false, noMix = false;
    KitRenderOptions options;

    // The saved kit mix applies first (same sound as the plugin); explicit
    // --param assignments below then win over it.
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--sfz" && i + 1 < argc) sfzPath = argv[i + 1];
        else if (arg == "--diagnostic") useDiagnostic = true;
        else if (arg == "--no-mix") noMix = true;
    }
    bool mixApplied = false;
    if (!noMix && (useDiagnostic || !sfzPath.empty())) {
        const std::string kitPath = useDiagnostic ? "" : canonicalKitPath(sfzPath);
        std::string text;
        KitMix mix;
        if (readTextFile(mixFilePathFor(kitPath), text) && parseKitMix(text, mix)) {
            KitModel model;
            std::string kitName;
            if (buildModelForMix(sfzPath, useDiagnostic, model, kitName)) {
                applyMixToOverrides(mix, model, options.padOverrides);
                for (const auto& p : kParams)
                    if (const double* v = mix.busValue(p.apvtsId))
                        options.params.*(p.field) = std::clamp(float(*v), p.lo, p.hi);
                if (const double* v = mix.busValue("limiter"))
                    options.params.limiter = *v >= 0.5;
                if (const double* v = mix.busValue("quality"))
                    options.params.quality = int(*v);
                mixApplied = true;
            }
        }
    }

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--sfz") sfzPath = next();
        else if (arg == "--midi") midiPath = next();
        else if (arg == "--out") outPath = next();
        else if (arg == "--diagnostic" || arg == "--no-mix") { /* handled above */ }
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
    w.field("mixApplied", mixApplied);
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
                     "                   [--seed N] [--param NAME=VALUE ...] [--no-mix]\n"
                     "  sappkit mix      show|set|clear (--sfz | --diagnostic) [TARGET.PARAM=VALUE ...]\n"
                     "                   (persistent per-kit mixes, shared with the plugin)\n");
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
    if (cmd == "mix") return cmdMix(argc, argv);

    std::fprintf(stderr, "unknown command '%s'\n", cmd.c_str());
    return 2;
}
