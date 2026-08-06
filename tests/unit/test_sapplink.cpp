#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/DiagnosticKit.h"
#include "core/KitRender.h"
#include "core/SappLinkCCMap.h"

// The vendored manifest at tests/data/sapplink-manifest.json mirrors the
// SOURCE OF TRUTH at ~/apps/sapptune/sapplink/manifests/sappkit.json.
// If sapptune's manifest changes, update the vendored copy AND the table in
// src/core/SappLinkCCMap.cpp together — this test makes silent drift fail CI.

using namespace sapp::kit;
using namespace sapp::kit::sapplink;

namespace {

struct ManifestRow {
    int cc = -1;
    std::string id, curve;
    float lo = 0, hi = 0;
};

// Minimal extractor for the known manifest shape (no JSON dependency in the
// core test target): parses each object of the "parameters" array.
std::vector<ManifestRow> loadManifest(const std::string& path)
{
    std::ifstream file(path);
    REQUIRE(file.good());
    std::stringstream ss;
    ss << file.rdbuf();
    const std::string text = ss.str();

    auto grabString = [](const std::string& obj, const std::string& key) {
        const auto k = obj.find("\"" + key + "\"");
        if (k == std::string::npos) return std::string();
        const auto q1 = obj.find('"', obj.find(':', k));
        const auto q2 = obj.find('"', q1 + 1);
        return obj.substr(q1 + 1, q2 - q1 - 1);
    };

    std::vector<ManifestRow> rows;
    size_t pos = text.find("\"parameters\"");
    REQUIRE(pos != std::string::npos);
    while ((pos = text.find("{ \"id\"", pos)) != std::string::npos) {
        const auto end = text.find('}', pos);
        const std::string obj = text.substr(pos, end - pos);
        ManifestRow row;
        row.id = grabString(obj, "id");
        row.curve = grabString(obj, "curve");
        row.cc = std::stoi(obj.substr(obj.find(':', obj.find("\"cc\"")) + 1));
        const auto rangeStart = obj.find('[', obj.find("\"range\""));
        const auto comma = obj.find(',', rangeStart);
        row.lo = std::stof(obj.substr(rangeStart + 1, comma - rangeStart - 1));
        row.hi = std::stof(obj.substr(comma + 1, obj.find(']', comma) - comma - 1));
        rows.push_back(row);
        pos = end;
    }
    return rows;
}

} // namespace

TEST_CASE("SappLink table matches the vendored manifest exactly", "[sapplink]")
{
    const auto rows = loadManifest(std::string(SAPPKIT_TEST_DATA_DIR) + "/sapplink-manifest.json");
    REQUIRE(rows.size() == size_t(kNumMappings));

    for (const auto& row : rows) {
        INFO("cc " << row.cc << " id " << row.id);
        const auto* mapping = findMapping(row.cc);
        REQUIRE(mapping != nullptr);
        REQUIRE(std::string(mapping->paramId) == row.id);
        REQUIRE(mapping->lo == row.lo);
        REQUIRE(mapping->hi == row.hi);
        REQUIRE(std::string(mapping->curve == Curve::Log ? "log" : "linear") == row.curve);
    }

    // No duplicate CC assignments in the table.
    for (const auto& a : mappings())
        REQUIRE(findMapping(a.cc) == &a);
}

TEST_CASE("reserved controllers stay engine- or library-native", "[sapplink]")
{
    // CC 64 sustain is SappSounds-native; CC 1/11 stay free for the
    // library's own SFZ CC conditions. Never in the mapping.
    REQUIRE(findMapping(1) == nullptr);
    REQUIRE(findMapping(11) == nullptr);
    REQUIRE(findMapping(64) == nullptr);
}

TEST_CASE("CC curves interpolate correctly and monotonically", "[sapplink]")
{
    const auto* master = findMapping(7);  // masterGain, linear -24..12
    REQUIRE(master != nullptr);
    REQUIRE(std::abs(ccToEngineering(*master, 0) - (-24.0f)) < 1e-4f);
    REQUIRE(std::abs(ccToEngineering(*master, 127) - 12.0f) < 1e-4f);

    const auto* crush = findMapping(16);  // crush, linear 0..1
    REQUIRE(crush != nullptr);
    REQUIRE(std::abs(ccToEngineering(*crush, 0) - 0.0f) < 1e-5f);
    REQUIRE(std::abs(ccToEngineering(*crush, 127) - 1.0f) < 1e-5f);

    for (const auto& mapping : mappings()) {
        float previous = ccToEngineering(mapping, 0);
        for (int v = 1; v <= 127; ++v) {
            const float value = ccToEngineering(mapping, v);
            REQUIRE(std::isfinite(value));
            REQUIRE(value >= previous - 1e-6f);
            previous = value;
        }
    }
}

TEST_CASE("applyCcToParams writes the mapped field and ignores others", "[sapplink]")
{
    KitParams params;
    REQUIRE(applyCcToParams(params, 14, 127));
    REQUIRE(std::abs(params.punch - 1.0f) < 1e-5f);
    REQUIRE(applyCcToParams(params, 7, 0));
    REQUIRE(std::abs(params.masterGainDb - (-24.0f)) < 1e-4f);
    REQUIRE_FALSE(applyCcToParams(params, 1, 64));    // library territory
    REQUIRE_FALSE(applyCcToParams(params, 74, 64));   // sappsynth's CC, not ours
}

TEST_CASE("CC 7 in a rendered clip scales output level", "[sapplink]")
{
    auto inst = makeDiagnosticKit();

    auto renderWithMasterCc = [&](int ccValue) {
        std::vector<sapp::sounds::TimedMidiEvent> song;
        song.push_back({0.0, 0xB0, 0, 7, uint8_t(ccValue), 0});
        song.push_back({0.2, 0x90, 0, 38, 110, 0});
        song.push_back({0.5, 0x80, 0, 38, 0, 0});
        KitRenderOptions options;
        options.tailSeconds = 0.4;
        options.params.limiter = false;
        options.params.humanize = 0.0f;
        return renderKit(inst, song, options);
    };

    const float quiet = renderWithMasterCc(0).rms;    // -24 dB
    const float loud = renderWithMasterCc(127).rms;   // +12 dB
    REQUIRE(loud > quiet * 10.0f);
}

TEST_CASE("CC 91 in a rendered clip opens the room", "[sapplink]")
{
    auto inst = makeDiagnosticKit();

    auto renderWithRoomCc = [&](int ccValue) {
        std::vector<sapp::sounds::TimedMidiEvent> song;
        song.push_back({0.0, 0xB0, 0, 91, uint8_t(ccValue), 0});
        song.push_back({0.0, 0xB0, 0, 92, 127, 0});   // big small-room
        song.push_back({0.1, 0x90, 0, 37, 120, 0});   // short side stick
        song.push_back({0.3, 0x80, 0, 37, 0, 0});
        KitRenderOptions options;
        options.tailSeconds = 1.0;
        options.params.humanize = 0.0f;
        return renderKit(inst, song, options);
    };

    auto tailEnergy = [](const KitRenderOutput& out) {
        double sum = 0.0;
        for (size_t i = size_t(0.35 * 48000.0); i < out.left.size(); ++i)
            sum += double(out.left[i]) * out.left[i] + double(out.right[i]) * out.right[i];
        return sum;
    };

    const double dry = tailEnergy(renderWithRoomCc(0));
    const double wet = tailEnergy(renderWithRoomCc(127));
    REQUIRE(wet > dry * 3.0);
}
