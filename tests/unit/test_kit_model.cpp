#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "core/DiagnosticKit.h"
#include "core/KitModel.h"
#include "core/KitRender.h"

using namespace sapp::kit;
using sapp::sounds::TimedMidiEvent;

namespace {

std::vector<TimedMidiEvent> singleHit(uint8_t note, uint8_t vel = 100)
{
    std::vector<TimedMidiEvent> song;
    song.push_back({0.02, 0x90, 0, note, vel, 0});
    song.push_back({0.4, 0x80, 0, note, 0, 0});
    return song;
}

KitRenderOptions dryOptions()
{
    KitRenderOptions options;
    options.tailSeconds = 1.2;
    options.params.roomLevel = 0.0f;
    options.params.punch = 0.0f;
    options.params.squash = 0.0f;
    options.params.humanize = 0.0f;
    options.params.limiter = false;
    return options;
}

double energyOf(const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
{
    double sum = 0.0;
    for (size_t i = from; i < x.size() && i < to; ++i) sum += double(x[i]) * x[i];
    return sum;
}

size_t zeroCrossings(const std::vector<float>& x)
{
    size_t n = 0;
    for (size_t i = 1; i < x.size(); ++i)
        if ((x[i - 1] < 0.0f) != (x[i] < 0.0f)) ++n;
    return n;
}

} // namespace

TEST_CASE("pad map covers the diagnostic kit with GM-aware names", "[padmap]")
{
    auto inst = makeDiagnosticKit();
    const KitModel model = buildKitModel(inst->definition);

    REQUIRE(model.padCount == 16);
    REQUIRE(model.soundCount == 16);

    // Pads are note-ascending; the essentials are all present and named.
    const int kick = model.padIndexForNote(36);
    REQUIRE(kick >= 0);
    CHECK(model.pads[size_t(kick)].name == "Kick");
    const int snare = model.padIndexForNote(38);
    REQUIRE(snare >= 0);
    CHECK(model.pads[size_t(snare)].name == "Snare");
    const int openHat = model.padIndexForNote(46);
    REQUIRE(openHat >= 0);
    CHECK(model.pads[size_t(openHat)].name == "Open Hat");

    // GM name table sanity.
    CHECK(std::string(gmDrumName(42)) == "Closed Hat");
    CHECK(gmDrumName(20) == nullptr);
}

TEST_CASE("pad map reports chokes, layers, and round robins", "[padmap]")
{
    auto inst = makeDiagnosticKit();
    const KitModel model = buildKitModel(inst->definition);

    // Hi-hat family shares choke group 1 and chokes group 1.
    for (int note : {42, 44, 46}) {
        const int i = model.padIndexForNote(note);
        REQUIRE(i >= 0);
        CHECK(model.pads[size_t(i)].chokeGroup == 1);
        CHECK(model.pads[size_t(i)].chokedBy == 1);
        CHECK(model.pads[size_t(i)].oneShot);
    }
    // Kick has 2 velocity layers and 2 round robins; crash has neither.
    const auto& kick = model.pads[size_t(model.padIndexForNote(36))];
    CHECK(kick.velocityLayers == 2);
    CHECK(kick.roundRobins == 2);
    const auto& crash = model.pads[size_t(model.padIndexForNote(49))];
    CHECK(crash.velocityLayers == 1);
    CHECK(crash.roundRobins == 1);
}

TEST_CASE("pad selection keeps GM essentials when a kit has too many sounds", "[padmap]")
{
    // 20 sounds: full GM core + a run of exotic high notes.
    sapp::sounds::InstrumentDefinition def;
    def.name = "Busy Kit";
    auto addSound = [&def](int note) {
        sapp::sounds::RegionDefinition r;
        r.loKey = r.hiKey = r.rootKey = uint8_t(note);
        r.samplePath = "x_" + std::to_string(note) + ".wav";
        def.regions.push_back(r);
    };
    for (int note : {36, 38, 42, 46, 49, 51, 39, 37, 41, 45, 48, 43})
        addSound(note);
    for (int note : {90, 91, 92, 93, 94, 95, 96, 97})
        addSound(note);

    const KitModel model = buildKitModel(def);
    REQUIRE(model.padCount == 16);
    CHECK(model.soundCount == 20);
    // Every GM essential survives; the overflow trims the exotic tail.
    for (int note : {36, 38, 42, 46, 49, 51})
        CHECK(model.padIndexForNote(note) >= 0);
    CHECK(model.padIndexForNote(97) < 0);
}

TEST_CASE("pad level override scales output", "[overrides]")
{
    auto inst = makeDiagnosticKit();
    const KitModel model = buildKitModel(inst->definition);
    const int kickPad = model.padIndexForNote(36);
    REQUIRE(kickPad >= 0);

    auto options = dryOptions();
    const auto reference = renderKit(inst, singleHit(36), options);
    options.padOverrides[size_t(kickPad)].levelDb = -20.0f;
    const auto quiet = renderKit(inst, singleHit(36), options);

    CHECK(reference.rms > quiet.rms * 5.0f);
}

TEST_CASE("pad tune override shifts pitch", "[overrides]")
{
    auto inst = makeDiagnosticKit();
    const KitModel model = buildKitModel(inst->definition);
    const int congaPad = model.padIndexForNote(63);
    REQUIRE(congaPad >= 0);

    auto options = dryOptions();
    const auto normal = renderKit(inst, singleHit(63), options);
    options.padOverrides[size_t(congaPad)].tuneSemis = 12.0f;
    const auto up = renderKit(inst, singleHit(63), options);

    // An octave up ≈ double the zero-crossing rate on a tonal drum.
    CHECK(double(zeroCrossings(up.left)) > double(zeroCrossings(normal.left)) * 1.5);
}

TEST_CASE("pad pan override moves the pad in the stereo field", "[overrides]")
{
    auto inst = makeDiagnosticKit();
    const KitModel model = buildKitModel(inst->definition);
    const int snarePad = model.padIndexForNote(38);
    REQUIRE(snarePad >= 0);

    auto options = dryOptions();
    options.padOverrides[size_t(snarePad)].pan = -1.0f;
    const auto left = renderKit(inst, singleHit(38), options);

    CHECK(energyOf(left.left) > energyOf(left.right) * 2.0);
}

TEST_CASE("pad decay override gates the tail", "[overrides]")
{
    auto inst = makeDiagnosticKit();
    const KitModel model = buildKitModel(inst->definition);
    const int crashPad = model.padIndexForNote(49);
    REQUIRE(crashPad >= 0);

    auto options = dryOptions();
    const auto natural = renderKit(inst, singleHit(49), options);
    options.padOverrides[size_t(crashPad)].decay = 0.05f;
    const auto gated = renderKit(inst, singleHit(49), options);

    // The crash rings for seconds naturally; gated it must be near-silent
    // half a second in.
    const size_t half = size_t(0.5 * 48000.0);
    const double lateNatural = energyOf(natural.left, half);
    const double lateGated = energyOf(gated.left, half);
    CHECK(lateNatural > lateGated * 20.0);
}

TEST_CASE("default overrides return the base instrument unchanged", "[overrides]")
{
    auto inst = makeDiagnosticKit();
    const KitModel model = buildKitModel(inst->definition);
    PadOverrides overrides{};
    CHECK(applyPadOverrides(inst, model, overrides) == inst);

    overrides[0].tuneSemis = 3.0f;
    auto rebuilt = applyPadOverrides(inst, model, overrides);
    CHECK(rebuilt != inst);
    CHECK(rebuilt->definition.regions.size() == inst->definition.regions.size());
}
