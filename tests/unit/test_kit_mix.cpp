// KitMix: per-kit persistent mixes — serialize/parse round trip, note-keyed
// application onto a model, capture, and file naming.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/DiagnosticKit.h"
#include "core/KitMix.h"
#include "core/KitModel.h"

using namespace sapp::kit;

namespace {

KitModel diagnosticModel()
{
    auto inst = makeDiagnosticKit();
    return buildKitModel(inst->definition);
}

} // namespace

TEST_CASE("kit mix serialize/parse round trip", "[mix]")
{
    KitMix mix;
    mix.kitPath = "/tmp/My Kit/kit \"quoted\".sfz";
    mix.kitName = "Test Kit\nwith newline";
    mix.pads.push_back({51, "Ride", {0.0f, 1.0f, 0.0f, -6.0f}});
    mix.pads.push_back({38, "Snare", {-1.5f, 0.7f, 0.25f, 3.0f}});
    mix.setBus("punch", 0.5);
    mix.setBus("masterGain", -3.25);
    mix.setBus("limiter", 1);

    KitMix back;
    REQUIRE(parseKitMix(serializeKitMix(mix), back));
    REQUIRE(back.kitPath == mix.kitPath);
    REQUIRE(back.kitName == mix.kitName);
    REQUIRE(back.pads.size() == 2);
    CHECK(back.pads[0].note == 51);
    CHECK(back.pads[0].name == "Ride");
    CHECK_THAT(back.pads[0].mix.levelDb,
               Catch::Matchers::WithinAbs(-6.0, 1e-4));
    CHECK_THAT(back.pads[1].mix.tuneSemis,
               Catch::Matchers::WithinAbs(-1.5, 1e-4));
    CHECK_THAT(back.pads[1].mix.decay, Catch::Matchers::WithinAbs(0.7, 1e-4));
    CHECK_THAT(back.pads[1].mix.pan, Catch::Matchers::WithinAbs(0.25, 1e-4));
    REQUIRE(back.busValue("punch") != nullptr);
    CHECK_THAT(*back.busValue("punch"), Catch::Matchers::WithinAbs(0.5, 1e-6));
    REQUIRE(back.busValue("masterGain") != nullptr);
    CHECK_THAT(*back.busValue("masterGain"),
               Catch::Matchers::WithinAbs(-3.25, 1e-6));
    CHECK(back.busValue("nope") == nullptr);
}

TEST_CASE("kit mix parse rejects malformed input", "[mix]")
{
    KitMix out;
    out.kitName = "untouched";
    CHECK_FALSE(parseKitMix("", out));
    CHECK_FALSE(parseKitMix("not json", out));
    CHECK_FALSE(parseKitMix("{\"pads\": [", out));
    CHECK_FALSE(parseKitMix("{\"kit\": 42}", out));
    CHECK(out.kitName == "untouched");  // untouched on failure

    // Unknown keys are skipped, not fatal (forward compatibility).
    CHECK(parseKitMix("{\"version\": 9, \"future\": {\"a\": [1, 2]}, "
                      "\"name\": \"ok\"}", out));
    CHECK(out.kitName == "ok");
}

TEST_CASE("kit mix applies by note onto the model", "[mix]")
{
    const auto model = diagnosticModel();
    REQUIRE(model.padCount > 2);

    const int noteA = model.pads[0].note;
    const int noteB = model.pads[2].note;

    KitMix mix;
    mix.pads.push_back({noteA, "", {0.0f, 1.0f, 0.0f, -12.0f}});
    mix.pads.push_back({noteB, "", {5.0f, 1.0f, 0.0f, 0.0f}});
    mix.pads.push_back({127, "", {0.0f, 1.0f, 0.0f, 6.0f}});  // not in model

    PadOverrides overrides{};
    CHECK(applyMixToOverrides(mix, model, overrides) == 2);
    CHECK(overrides[0].levelDb == -12.0f);
    CHECK(overrides[2].tuneSemis == 5.0f);
    CHECK(overrides[1].isDefault());
}

TEST_CASE("capture keeps only non-default pads", "[mix]")
{
    const auto model = diagnosticModel();
    PadOverrides overrides{};
    overrides[1].levelDb = -4.0f;
    overrides[3].pan = 0.5f;

    const auto mix = captureMix("/tmp/kit.sfz", "Kit", model, overrides);
    REQUIRE(mix.pads.size() == 2);
    CHECK(mix.pads[0].note == model.pads[1].note);
    CHECK(mix.pads[0].name == model.pads[1].name);
    CHECK(mix.pads[1].note == model.pads[3].note);

    // Round trip through JSON and back onto a fresh overrides array.
    KitMix back;
    REQUIRE(parseKitMix(serializeKitMix(mix), back));
    PadOverrides restored{};
    CHECK(applyMixToOverrides(back, model, restored) == 2);
    CHECK(restored[1].levelDb == -4.0f);
    CHECK(restored[3].pan == 0.5f);
}

TEST_CASE("mix file names are stable, distinct, and filesystem-safe", "[mix]")
{
    const auto a = kitMixFileName("/Users/x/Samples/avl-drumkits/Black Pearl.sfz");
    const auto b = kitMixFileName("/Users/x/Samples/other/Black Pearl.sfz");
    CHECK(a != b);                                       // same stem, different path
    CHECK(a == kitMixFileName("/Users/x/Samples/avl-drumkits/Black Pearl.sfz"));
    CHECK(a.find("Black Pearl-") == 0);
    CHECK(a.substr(a.size() - 5) == ".json");
    CHECK(kitMixFileName("") == "diagnostic-kit.json");
    const auto weird = kitMixFileName("/k/it$/we?ird*.sfz");
    CHECK(weird.find('?') == std::string::npos);
    CHECK(weird.find('*') == std::string::npos);
}
