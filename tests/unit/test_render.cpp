#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "core/DiagnosticKit.h"
#include "core/KitRender.h"

using namespace sapp::kit;
using sapp::sounds::TimedMidiEvent;

namespace {

std::vector<TimedMidiEvent> groove()
{
    std::vector<TimedMidiEvent> song;
    auto hit = [&song](double t, uint8_t note, uint8_t vel) {
        song.push_back({t, 0x90, 0, note, vel, 0});
        song.push_back({t + 0.1, 0x80, 0, note, 0, 0});
    };
    for (int bar = 0; bar < 2; ++bar) {
        const double t = bar * 2.0;
        hit(t + 0.0, 36, 120);
        hit(t + 0.5, 42, 90);
        hit(t + 1.0, 38, 110);
        hit(t + 1.5, 46, 100);
        hit(t + 1.75, 42, 80);
    }
    return song;
}

} // namespace

TEST_CASE("kit offline render is deterministic and audible", "[render]")
{
    auto inst = makeDiagnosticKit();

    KitRenderOptions options;
    options.tailSeconds = 1.5;
    options.params.humanize = 0.4f;

    auto a = renderKit(inst, groove(), options);
    auto b = renderKit(inst, groove(), options);

    REQUIRE(a.left.size() == b.left.size());
    for (size_t i = 0; i < a.left.size(); i += 131)
        REQUIRE(a.left[i] == b.left[i]);
    CHECK(a.peak > 0.05f);
    CHECK(a.peak <= 1.0f);
}

TEST_CASE("different seeds give different humanized takes", "[render]")
{
    auto inst = makeDiagnosticKit();

    KitRenderOptions options;
    options.tailSeconds = 1.0;
    options.params.humanize = 0.8f;
    options.seed = 1111;
    auto a = renderKit(inst, groove(), options);
    options.seed = 2222;
    auto b = renderKit(inst, groove(), options);

    REQUIRE(a.left.size() == b.left.size());
    double diff = 0.0, ref = 0.0;
    for (size_t i = 0; i < a.left.size(); ++i) {
        diff += double(a.left[i] - b.left[i]) * (a.left[i] - b.left[i]);
        ref += double(a.left[i]) * a.left[i];
    }
    CHECK(diff > ref * 0.01);
}

TEST_CASE("pad overrides flow through the offline render", "[render]")
{
    auto inst = makeDiagnosticKit();
    const KitModel model = buildKitModel(inst->definition);
    const int kickPad = model.padIndexForNote(36);
    REQUIRE(kickPad >= 0);

    std::vector<TimedMidiEvent> kick;
    kick.push_back({0.02, 0x90, 0, 36, 120, 0});
    kick.push_back({0.4, 0x80, 0, 36, 0, 0});

    KitRenderOptions options;
    options.tailSeconds = 0.8;
    options.params.limiter = false;
    options.params.roomLevel = 0.0f;
    auto reference = renderKit(inst, kick, options);
    options.padOverrides[size_t(kickPad)].levelDb = -24.0f;
    auto trimmed = renderKit(inst, kick, options);

    CHECK(reference.rms > trimmed.rms * 6.0f);
}

#include "core/VersionCompare.h"

TEST_CASE("updater version comparison", "[updater]")
{
    CHECK(sapp::kit::isNewerVersion("v0.3.1", "0.3.0"));
    CHECK(sapp::kit::isNewerVersion("v1.0.0", "0.9.9"));
    CHECK_FALSE(sapp::kit::isNewerVersion("v0.3.0", "0.3.0"));
    CHECK_FALSE(sapp::kit::isNewerVersion("v0.2.9", "0.3.0"));
}
