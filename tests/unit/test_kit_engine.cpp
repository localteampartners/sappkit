#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "core/DiagnosticKit.h"
#include "core/KitEngine.h"

using namespace sapp::kit;
using sapp::sounds::MidiEvent;

namespace {

MidiEvent noteOn(uint32_t frame, uint8_t note, uint8_t vel)
{
    MidiEvent e;
    e.type = MidiEvent::Type::NoteOn;
    e.frame = frame;
    e.note = note;
    e.value = vel;
    return e;
}

MidiEvent noteOff(uint32_t frame, uint8_t note)
{
    MidiEvent e;
    e.type = MidiEvent::Type::NoteOff;
    e.frame = frame;
    e.note = note;
    return e;
}

struct Rendered {
    std::vector<float> left, right;
    float peak = 0.0f;
    float rms = 0.0f;
};

Rendered run(KitEngine& engine, std::vector<MidiEvent> events, int totalFrames,
             int block = 512)
{
    Rendered out;
    out.left.assign(size_t(totalFrames), 0.0f);
    out.right.assign(size_t(totalFrames), 0.0f);
    size_t next = 0;
    for (int start = 0; start < totalFrames; start += block) {
        const int frames = std::min(block, totalFrames - start);
        std::vector<MidiEvent> blockEvents;
        while (next < events.size() && events[next].frame < uint32_t(start + frames)) {
            MidiEvent e = events[next];
            e.frame = e.frame >= uint32_t(start) ? e.frame - uint32_t(start) : 0;
            blockEvents.push_back(e);
            ++next;
        }
        engine.process(blockEvents.data(), int(blockEvents.size()),
                       out.left.data() + start, out.right.data() + start, frames);
    }
    double sumSq = 0.0;
    for (size_t i = 0; i < out.left.size(); ++i) {
        out.peak = std::max({out.peak, std::abs(out.left[i]), std::abs(out.right[i])});
        sumSq += double(out.left[i]) * out.left[i] + double(out.right[i]) * out.right[i];
    }
    out.rms = float(std::sqrt(sumSq / double(out.left.size() * 2)));
    return out;
}

KitParams dryParams()
{
    KitParams p;
    p.roomLevel = 0.0f;
    p.punch = 0.0f;
    p.squash = 0.0f;
    p.crush = 0.0f;
    p.humanize = 0.0f;
    p.limiter = false;
    return p;
}

KitEngine& freshEngine(KitEngine& engine, KitParams params)
{
    engine.prepare(48000, 512);
    engine.setParams(params);
    engine.setInstrument(makeDiagnosticKit());
    return engine;
}

double energyBetween(const Rendered& out, double fromSec, double toSec)
{
    const size_t a = size_t(fromSec * 48000.0);
    const size_t b = size_t(toSec * 48000.0);
    double sum = 0.0;
    for (size_t i = a; i < b && i < out.left.size(); ++i)
        sum += double(out.left[i]) * out.left[i] + double(out.right[i]) * out.right[i];
    return sum;
}

} // namespace

TEST_CASE("kit makes sound and one-shots ignore note-off", "[kit]")
{
    KitEngine engine;
    freshEngine(engine, dryParams());
    // Note-off almost immediately after the crash hit: one_shot must ride on.
    auto out = run(engine, {noteOn(0, 49, 110), noteOff(500, 49)}, 96000);
    CHECK(out.peak > 0.05f);
    CHECK(energyBetween(out, 1.0, 1.6) > 1.0e-5);
}

TEST_CASE("closed hat chokes the open hat", "[kit][choke]")
{
    KitEngine open1, open2;
    freshEngine(open1, dryParams());
    freshEngine(open2, dryParams());

    // Open hat alone rings past 0.6 s.
    auto ringing = run(open1, {noteOn(0, 46, 110)}, 48000);
    const double lateRinging = energyBetween(ringing, 0.6, 1.0);
    CHECK(lateRinging > 1.0e-6);

    // Same open hat, but a closed hat at 0.4 s chokes it (off_by group 1).
    auto choked = run(open2, {noteOn(0, 46, 110), noteOn(19200, 42, 90)}, 48000);
    const double lateChoked = energyBetween(choked, 0.6, 1.0);

    // Post-choke, only the (short, quiet-by-0.6s) closed hat remains.
    CHECK(lateRinging > lateChoked * 8.0);
}

TEST_CASE("pedal hat chokes the open hat too", "[kit][choke]")
{
    KitEngine a, b;
    freshEngine(a, dryParams());
    freshEngine(b, dryParams());
    auto ringing = run(a, {noteOn(0, 46, 110)}, 48000);
    auto choked = run(b, {noteOn(0, 46, 110), noteOn(14400, 44, 90)}, 48000);
    CHECK(energyBetween(ringing, 0.5, 1.0) > energyBetween(choked, 0.5, 1.0) * 8.0);
}

TEST_CASE("snare and kick do not choke each other", "[kit][choke]")
{
    KitEngine a, b;
    freshEngine(a, dryParams());
    freshEngine(b, dryParams());
    auto alone = run(a, {noteOn(0, 49, 110)}, 48000);        // crash rings
    auto busy = run(b, {noteOn(0, 49, 110), noteOn(9600, 36, 120),
                        noteOn(14400, 38, 120)}, 48000);
    // Crash tail must survive kick+snare hits (no spurious chokes).
    CHECK(energyBetween(busy, 0.8, 1.0) > energyBetween(alone, 0.8, 1.0) * 0.5);
}

TEST_CASE("round robins alternate takes on repeated hits", "[kit][rr]")
{
    KitEngine engine;
    freshEngine(engine, dryParams());

    // Two snare hits far enough apart to compare their attack windows.
    auto out = run(engine, {noteOn(0, 38, 100), noteOn(24000, 38, 100)}, 48000);
    const size_t window = 4800;  // 100 ms
    double diff = 0.0, ref = 0.0;
    for (size_t i = 0; i < window; ++i) {
        const float a = out.left[i];
        const float b = out.left[24000 + i];
        diff += double(a - b) * (a - b);
        ref += double(a) * a;
    }
    // Different RR sample content ⇒ the two hits are not near-identical.
    CHECK(diff > ref * 0.05);
}

TEST_CASE("velocity layers switch sample content", "[kit]")
{
    KitEngine a, b;
    freshEngine(a, dryParams());
    freshEngine(b, dryParams());
    auto soft = run(a, {noteOn(0, 36, 40)}, 24000);
    auto hard = run(b, {noteOn(0, 36, 127)}, 24000);
    CHECK(hard.rms > soft.rms * 1.5f);  // velocity tracking + hot layer
}

TEST_CASE("punch lifts the attack-to-sustain ratio", "[kit][fx]")
{
    KitParams flat = dryParams();
    KitParams punchy = flat;
    punchy.punch = 1.0f;

    KitEngine a, b;
    freshEngine(a, flat);
    freshEngine(b, punchy);
    auto dry = run(a, {noteOn(0, 63, 110)}, 24000);
    auto punched = run(b, {noteOn(0, 63, 110)}, 24000);

    const double dryRatio = energyBetween(dry, 0.0, 0.03) /
                            (energyBetween(dry, 0.05, 0.3) + 1e-12);
    const double punchedRatio = energyBetween(punched, 0.0, 0.03) /
                                (energyBetween(punched, 0.05, 0.3) + 1e-12);
    CHECK(punchedRatio > dryRatio * 1.15);
}

TEST_CASE("squash flattens the decay slope of a loud hit", "[kit][fx]")
{
    KitParams flat = dryParams();
    KitParams squashed = flat;
    squashed.squash = 1.0f;

    KitEngine a, b;
    freshEngine(a, flat);
    freshEngine(b, squashed);
    auto dry = run(a, {noteOn(0, 49, 127)}, 72000);
    auto glued = run(b, {noteOn(0, 49, 127)}, 72000);

    // The compressor releases as the crash decays, so the loud head is
    // reduced relative to the tail: head/tail energy ratio must shrink.
    const double dryRatio = energyBetween(dry, 0.02, 0.25) /
                            (energyBetween(dry, 0.8, 1.3) + 1e-12);
    const double gluedRatio = energyBetween(glued, 0.02, 0.25) /
                              (energyBetween(glued, 0.8, 1.3) + 1e-12);
    CHECK(gluedRatio < dryRatio * 0.8);
}

TEST_CASE("crush audibly changes the signal", "[kit][fx]")
{
    KitParams flat = dryParams();
    KitParams crushed = flat;
    crushed.crush = 0.9f;

    KitEngine a, b;
    freshEngine(a, flat);
    freshEngine(b, crushed);
    auto clean = run(a, {noteOn(0, 51, 110)}, 24000);
    auto lofi = run(b, {noteOn(0, 51, 110)}, 24000);

    double diff = 0.0, ref = 0.0;
    for (size_t i = 0; i < clean.left.size(); ++i) {
        diff += double(clean.left[i] - lofi.left[i]) * (clean.left[i] - lofi.left[i]);
        ref += double(clean.left[i]) * clean.left[i];
    }
    CHECK(diff > ref * 0.05);
    CHECK(lofi.peak <= 1.5f);
}

TEST_CASE("room adds a tail that follows the hit", "[kit][fx]")
{
    KitParams dry = dryParams();
    KitParams wet = dry;
    wet.roomLevel = 0.9f;
    wet.roomSize = 0.8f;

    KitEngine a, b;
    freshEngine(a, dry);
    freshEngine(b, wet);
    auto direct = run(a, {noteOn(0, 37, 120)}, 48000);   // short side stick
    auto ambient = run(b, {noteOn(0, 37, 120)}, 48000);

    CHECK(energyBetween(ambient, 0.25, 0.6) > energyBetween(direct, 0.25, 0.6) * 3.0);
}

TEST_CASE("output is always finite, limiter caps extremes", "[kit]")
{
    KitParams hot = dryParams();
    hot.masterGainDb = 12.0f;
    hot.limiter = true;
    hot.punch = 1.0f;
    hot.squash = 1.0f;

    KitEngine engine;
    freshEngine(engine, hot);
    std::vector<MidiEvent> wall;
    for (int i = 0; i < 32; ++i)
        wall.push_back(noteOn(uint32_t(i * 300), uint8_t(36 + (i * 3) % 28), 127));
    auto out = run(engine, wall, 48000);
    for (float v : out.left) {
        REQUIRE(std::isfinite(v));
        REQUIRE(std::abs(v) <= 1.0f);
    }
}
