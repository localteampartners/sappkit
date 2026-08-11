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

TEST_CASE("clean scales the modeled imperfections out", "[render][clean]")
{
    // Suite-wide `clean` convention (SappLink CC 3): 0 = every modeled
    // imperfection as designed, 1 = none. SappKit's only imperfection source
    // is `humanize` (per-hit tune scatter), so clean=1 must be bit-identical
    // to humanize=0 — and must never silence the kit.
    auto inst = makeDiagnosticKit();

    KitRenderOptions modeled;
    modeled.tailSeconds = 1.0;
    modeled.params.humanize = 0.6f;
    modeled.params.clean = 0.0f;

    KitRenderOptions cleaned = modeled;
    cleaned.params.clean = 1.0f;

    KitRenderOptions noHumanize = modeled;
    noHumanize.params.humanize = 0.0f;

    const auto a = renderKit(inst, groove(), modeled);
    const auto b = renderKit(inst, groove(), cleaned);
    const auto c = renderKit(inst, groove(), noHumanize);

    REQUIRE(a.left.size() == b.left.size());
    REQUIRE(b.left.size() == c.left.size());

    bool cleanEqualsNoHumanize = true;
    bool cleanDiffersFromModeled = false;
    for (size_t i = 0; i < b.left.size(); ++i) {
        if (b.left[i] != c.left[i]) cleanEqualsNoHumanize = false;
        if (b.left[i] != a.left[i]) cleanDiffersFromModeled = true;
    }
    CHECK(cleanEqualsNoHumanize);
    CHECK(cleanDiffersFromModeled);

    // No parameter of this instrument may default — or be driven — to silence.
    CHECK(b.peak > 0.05f);

    // Half-clean lands between the two, not on either.
    KitRenderOptions half = modeled;
    half.params.clean = 0.5f;
    const auto d = renderKit(inst, groove(), half);
    bool halfDiffersFromBoth = false;
    for (size_t i = 0; i < d.left.size(); ++i)
        if (d.left[i] != a.left[i] && d.left[i] != b.left[i]) {
            halfDiffersFromBoth = true;
            break;
        }
    CHECK(halfDiffersFromBoth);
    CHECK(d.peak > 0.05f);
}
