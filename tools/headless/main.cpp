// sappkit-headless — the station harness.
//
// Drives SappKitProcessor exactly the way the sappradio station host does:
// no editor is ever created, and — this is the part that matters — the JUCE
// dispatch loop is NEVER run. A plugin embedded in a non-JUCE headless host
// has a MessageManager but nothing pumps it, so juce::Timer callbacks and
// MessageManager::callAsync() never fire. Anything the plugin needs the
// message loop for simply does not happen, silently (sappkit #1,
// sappchoir #1, sapporchestra #2).
//
//   sappkit-headless selftest [--fixture DIR]
//       Regression suite for sappkit #1. Exit 0 = all pass.
//
//   sappkit-headless render [--preset N] [--out F.wav] [--root DIR]
//                           [--settle MS] [--pump] [--param ID=VALUE]
//                           [--cc N=V]
//       One station-style render: a 4-bar groove, default parameters.
//       --pump runs the dispatch loop during the settle window (i.e. pretends
//       to be a JUCE host); the default does not, which is the real station
//       condition.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "SoundsPanel.h"

namespace {

void setEnv(const char* name, const juce::String& value)
{
#if JUCE_WINDOWS
    _putenv_s(name, value.toRawUTF8());
#else
    if (value.isEmpty()) ::unsetenv(name);
    else ::setenv(name, value.toRawUTF8(), 1);
#endif
}

double toDb(double linear)
{
    return linear > 1.0e-12 ? 20.0 * std::log10(linear) : -200.0;
}

uint64_t audioHash(const std::vector<float>& l, const std::vector<float>& r)
{
    uint64_t h = 1469598103934665603ull;
    auto feed = [&h](float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        for (int b = 0; b < 4; ++b) {
            h ^= uint64_t((bits >> (b * 8)) & 0xff);
            h *= 1099511628211ull;
        }
    };
    for (float v : l) feed(v);
    for (float v : r) feed(v);
    return h;
}

struct RenderResult {
    std::vector<float> left, right;
    juce::String instrumentPath, instrumentName, status;
    bool libraryReady = false;
    int padCount = 0;
    double rms = 0.0, peak = 0.0;
    int maxVoices = 0;          // peak active voice count during the render
    uint64_t hash = 0;
};

struct RenderOptions {
    int settleMs = 8000;
    bool pump = false;
    // Apply --param AFTER the kit is installed (plus a quiet 500 ms), which
    // isolates the debounced pad-override rebuild from the bake-in that
    // finishLoad already does. Still no dispatch loop, ever.
    bool paramsAfterSettle = false;
    juce::StringPairArray params;      // parameter id -> engineering value
    juce::Array<int> cc;               // pairs: number, value (sent at frame 0)
};

// The station's shape for a drum instrument: a plain 4-bar backbeat at 120 bpm
// on the GM notes every kit maps (36 kick, 38 snare, 42/46 hats).
struct Hit { int block; int note; float velocity; };

std::vector<Hit> stationGroove(int blocksPerEighth, int bars)
{
    std::vector<Hit> score;
    for (int bar = 0; bar < bars; ++bar) {
        for (int eighth = 0; eighth < 8; ++eighth) {
            const int block = (bar * 8 + eighth) * blocksPerEighth;
            score.push_back({block, eighth == 3 ? 46 : 42, eighth % 2 == 0 ? 0.7f : 0.45f});
            if (eighth == 0 || eighth == 5) score.push_back({block, 36, 0.95f});
            if (eighth == 2 || eighth == 6) score.push_back({block, 38, 0.85f});
        }
    }
    return score;
}

// One station render. `presetChoice` < 0 = select nothing (the control case).
RenderResult stationRender(int presetChoice, const RenderOptions& options)
{
    RenderResult out;
    auto processor = std::make_unique<sappkit::SappKitProcessor>();
    processor->prepareToPlay(48000.0, 512);

    if (presetChoice >= 0) {
        auto* parameter = processor->valueTree().getParameter("preset");
        // Exactly what a host does with an automation lane: normalized write.
        parameter->setValueNotifyingHost(parameter->convertTo0to1(float(presetChoice)));
    }

    auto applyParams = [&] {
        for (const auto& id : options.params.getAllKeys())
            if (auto* parameter = processor->valueTree().getParameter(id))
                parameter->setValueNotifyingHost(
                    parameter->convertTo0to1(options.params[id].getFloatValue()));
    };
    if (!options.paramsAfterSettle) applyParams();

    // Settle window. The station passes --settle and does NOT pump a JUCE
    // dispatch loop; --pump models a JUCE-based host instead.
    const auto deadline = juce::Time::getMillisecondCounter() + uint32_t(options.settleMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        if (processor->libraryReady()) break;
        if (options.pump) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        else juce::Thread::sleep(5);
    }

    if (options.paramsAfterSettle) {
        applyParams();
        juce::Thread::sleep(500);   // ~4 maintenance ticks, no message loop
    }

    out.instrumentPath = processor->currentInstrumentPath();
    out.instrumentName = processor->currentInstrumentName();
    out.status = processor->loadStatus();
    out.libraryReady = processor->libraryReady();
    out.padCount = processor->kitModel().padCount;

    constexpr int kBlock = 512;
    constexpr int kBlocksPerEighth = 23;               // ~0.25 s at 48 kHz
    const auto score = stationGroove(kBlocksPerEighth, 4);
    const int kBlocks = kBlocksPerEighth * 8 * 4 + 120;   // + tail

    juce::AudioBuffer<float> buffer(2, kBlock);
    for (int b = 0; b < kBlocks; ++b) {
        juce::MidiBuffer midi;
        if (b == 0)
            for (int i = 0; i + 1 < options.cc.size(); i += 2)
                midi.addEvent(juce::MidiMessage::controllerEvent(1, options.cc[i],
                                                                 options.cc[i + 1]), 0);
        for (const auto& hit : score)
            if (hit.block == b) {
                midi.addEvent(juce::MidiMessage::noteOn(10, hit.note, hit.velocity), 0);
                midi.addEvent(juce::MidiMessage::noteOff(10, hit.note), 200);
            }
        buffer.clear();
        processor->processBlock(buffer, midi);
        out.maxVoices = juce::jmax(out.maxVoices,
                                   processor->engine().sampler().activeVoiceCount());
        out.left.insert(out.left.end(), buffer.getReadPointer(0),
                        buffer.getReadPointer(0) + kBlock);
        out.right.insert(out.right.end(), buffer.getReadPointer(1),
                         buffer.getReadPointer(1) + kBlock);
    }

    double sum = 0.0;
    for (size_t i = 0; i < out.left.size(); ++i) {
        sum += double(out.left[i]) * out.left[i] + double(out.right[i]) * out.right[i];
        out.peak = std::max(out.peak, double(std::abs(out.left[i])));
        out.peak = std::max(out.peak, double(std::abs(out.right[i])));
    }
    out.rms = std::sqrt(sum / double(out.left.size() * 2));
    out.hash = audioHash(out.left, out.right);
    processor.reset();
    return out;
}

void report(const char* label, const RenderResult& r)
{
    std::printf("        %-10s rms %.8f (%7.2f dBFS) peak %.5f voices %2d pads %2d "
                "ready %d kit \"%s\" status \"%s\"\n",
                label, r.rms, toDb(r.rms), r.peak, r.maxVoices, r.padCount,
                r.libraryReady ? 1 : 0,
                r.instrumentPath.isEmpty() ? "(built-in diagnostic)"
                                           : r.instrumentPath.toRawUTF8(),
                r.status.toRawUTF8());
    std::fflush(stdout);
}

// --------------------------------------------------------------- selftest --

int fails = 0;

void check(bool ok, const juce::String& what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    std::fflush(stdout);
    if (!ok) ++fails;
}

// Used in place: the fixture is only ever READ (resolveKit walks it, the SFZ
// loader parses it), so there is nothing to isolate by copying — and copying
// is one more thing that can silently reshape the tree.
juce::File prepareFixture(const juce::String& fixtureRoot)
{
    const juce::File source(fixtureRoot);
    if (!source.getChildFile("avl-drumkits").isDirectory()) {
        std::printf("FAIL: %s is not a fixture samples root\n", fixtureRoot.toRawUTF8());
        ++fails;
        return {};
    }
    return source;
}

// The usable band. A drum bus the station can air sits well above -45 dBFS
// RMS over a busy groove, and below clipping.
constexpr double kUsableRmsDbLo = -45.0;
constexpr double kUsableRmsDbHi = -6.0;

// Fixture programs (FactoryKits.cpp order): 2 = avl-drumkits/Black_Pearl_5pc,
// 8 = gogodze-phu/Kit (deliberately ~20 dB quieter).
constexpr int kLoudProgram = 2;
constexpr int kQuietProgram = 8;

int runSelftest(const juce::String& fixtureRoot)
{
    const auto root = prepareFixture(fixtureRoot);
    if (!root.isDirectory()) return 1;
    setEnv(sappkit::kSamplesRootEnvVar, root.getFullPathName());

    RenderOptions plain;

    std::printf("sappkit #1 — kit loading must work with no message loop\n");

    const auto builtin = stationRender(-1, plain);
    report("built-in", builtin);
    check(builtin.maxVoices > 0, "voices actually render with no host assistance");
    check(toDb(builtin.rms) > kUsableRmsDbLo && toDb(builtin.rms) < kUsableRmsDbHi,
          "the built-in diagnostic kit renders in the usable band "
          "(-45..-6 dBFS RMS): " + juce::String(toDb(builtin.rms), 2) + " dBFS");
    check(builtin.padCount > 0, "the pad map is populated without a dispatch loop");
    check(builtin.libraryReady, "libraryReady reads 1 without a dispatch loop");

    const auto loud = stationRender(kLoudProgram, plain);
    report("loud kit", loud);
    check(loud.instrumentPath == root.getChildFile("avl-drumkits")
                                     .getChildFile("Black_Pearl_5pc.sfz").getFullPathName(),
          "the selected factory kit is the one that loaded");
    check(loud.hash != builtin.hash, "selected and unselected renders DIFFER");
    check(toDb(loud.rms) > kUsableRmsDbLo && toDb(loud.rms) < kUsableRmsDbHi,
          "the selected kit renders in the usable band: "
              + juce::String(toDb(loud.rms), 2) + " dBFS");
    check(loud.libraryReady, "libraryReady reads 1 once the selection is installed");
    check(loud.padCount == 4, "all four fixture pads mapped (got "
                                  + juce::String(loud.padCount) + ")");

    const auto quiet = stationRender(kQuietProgram, plain);
    report("quiet kit", quiet);
    check(quiet.instrumentPath ==
              root.getChildFile("gogodze-phu").getChildFile("Kit.sfz").getFullPathName(),
          "the second program loaded its own kit");
    check(quiet.rms < loud.rms * 0.6,
          "the quiet kit really is the quiet one (it is what sounded)");

    RenderOptions pumped = plain;
    pumped.pump = true;
    const auto withPump = stationRender(kLoudProgram, pumped);
    report("pumped", withPump);
    check(withPump.hash == loud.hash,
          "pumping the message loop changes nothing (same render)");

    // ---- no parameter may default to silence ------------------------------
    // `clean` is the suite-wide imperfection scaler: 0 = fully modeled. At
    // either extreme the kit must still sound.
    RenderOptions fullyClean = plain;
    fullyClean.params.set("clean", "1.0");
    const auto cleaned = stationRender(kLoudProgram, fullyClean);
    report("clean=1", cleaned);
    check(toDb(cleaned.rms) > kUsableRmsDbLo,
          "clean=1 still renders in the usable band: "
              + juce::String(toDb(cleaned.rms), 2) + " dBFS");
    check(cleaned.hash != loud.hash, "clean=1 changes the sound (humanize scales out)");

    // ---- CC 3 reaches `clean` through the SappLink map --------------------
    RenderOptions cleanCc = plain;
    cleanCc.cc.add(3);
    cleanCc.cc.add(127);
    const auto viaCc = stationRender(kLoudProgram, cleanCc);
    report("cc3=127", viaCc);
    check(viaCc.hash != loud.hash, "CC 3 moves `clean` (SappLink map)");

    // ---- MIDI program change selects a kit, headlessly --------------------
    {
        auto processor = std::make_unique<sappkit::SappKitProcessor>();
        processor->prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::programChange(10, kQuietProgram), 0);
        buffer.clear();
        processor->processBlock(buffer, midi);          // no dispatch loop, ever
        const auto deadline = juce::Time::getMillisecondCounter() + 8000u;
        while (juce::Time::getMillisecondCounter() < deadline && !processor->libraryReady())
            juce::Thread::sleep(5);
        check(processor->currentInstrumentPath() ==
                  root.getChildFile("gogodze-phu").getChildFile("Kit.sfz").getFullPathName(),
              "a MIDI program change loaded its kit headlessly");
        check(processor->getCurrentProgram() == kQuietProgram,
              "getCurrentProgram followed the program change");
        processor.reset();
    }

    // ---- state restore installs the kit headlessly ------------------------
    {
        auto settle = [](sappkit::SappKitProcessor& p) {
            const auto deadline = juce::Time::getMillisecondCounter() + 8000u;
            while (juce::Time::getMillisecondCounter() < deadline && !p.libraryReady())
                juce::Thread::sleep(5);   // still no dispatch loop anywhere
        };
        auto saved = std::make_unique<sappkit::SappKitProcessor>();
        saved->prepareToPlay(48000.0, 512);
        saved->loadSfzInstrument(root.getChildFile("avl-drumkits")
                                     .getChildFile("Black_Pearl_5pc.sfz"));
        settle(*saved);
        juce::MemoryBlock state;
        saved->getStateInformation(state);
        saved.reset();

        auto restored = std::make_unique<sappkit::SappKitProcessor>();
        restored->prepareToPlay(48000.0, 512);
        restored->setStateInformation(state.getData(), int(state.getSize()));
        settle(*restored);
        check(restored->currentInstrumentPath() ==
                  root.getChildFile("avl-drumkits")
                      .getChildFile("Black_Pearl_5pc.sfz").getFullPathName(),
              "a state restore installed its kit headlessly");
        check(restored->kitModel().padCount == 4,
              "the restored kit's pad map is complete (4 pads) — the generation "
              "guard dropped nothing");
        restored.reset();
    }

    // ---- pad overrides bake in without a Timer ----------------------------
    {
        RenderOptions tuned = plain;
        tuned.paramsAfterSettle = true;
        tuned.params.set("pad1Tune", "-12.0");   // pad 1 = note 36, the kick
        const auto detuned = stationRender(kLoudProgram, tuned);
        report("pad tune", detuned);
        check(detuned.hash != loud.hash,
              "a pad override rebuild reaches the engine with no Timer");
    }

    std::printf("selftest: %s\n", fails == 0 ? "ALL PASS" : "FAILURES");
    return fails == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String command = argc > 1 ? juce::String(argv[1]) : juce::String();
    juce::String root, out, fixture;
    int presetChoice = -1;
    RenderOptions options;
    for (int i = 2; i < argc; ++i) {
        const juce::String arg(argv[i]);
        auto next = [&]() -> juce::String {
            return i + 1 < argc ? juce::String(argv[++i]) : juce::String();
        };
        if (arg == "--root") root = next();
        else if (arg == "--fixture") fixture = next();
        else if (arg == "--preset") presetChoice = next().getIntValue();
        else if (arg == "--out") out = next();
        else if (arg == "--settle") options.settleMs = next().getIntValue();
        else if (arg == "--pump") options.pump = true;
        else if (arg == "--param") {
            const auto kv = next();
            options.params.set(kv.upToFirstOccurrenceOf("=", false, false),
                               kv.fromFirstOccurrenceOf("=", false, false));
        } else if (arg == "--cc") {
            const auto kv = next();
            options.cc.add(kv.upToFirstOccurrenceOf("=", false, false).getIntValue());
            options.cc.add(kv.fromFirstOccurrenceOf("=", false, false).getIntValue());
        }
    }

    if (command == "selftest") {
        if (fixture.isEmpty()) fixture = root;
#ifdef SAPPKIT_TEST_DATA_DIR
        if (fixture.isEmpty()) fixture = juce::String(SAPPKIT_TEST_DATA_DIR) + "/kit-headless";
#endif
        return runSelftest(fixture);
    }

    if (command == "render") {
        if (root.isNotEmpty())
            setEnv(sappkit::kSamplesRootEnvVar, root);
        const auto result = stationRender(presetChoice, options);
        std::printf("kit:      %s\n", result.instrumentPath.toRawUTF8());
        std::printf("name:     %s\n", result.instrumentName.toRawUTF8());
        std::printf("status:   %s\n", result.status.toRawUTF8());
        std::printf("ready:    %d\n", result.libraryReady ? 1 : 0);
        std::printf("pads:     %d\n", result.padCount);
        std::printf("voices:   %d\n", result.maxVoices);
        std::printf("rms:      %.8f  (%.2f dBFS)\n", result.rms, toDb(result.rms));
        std::printf("peak:     %.8f  (%.2f dBFS)\n", result.peak, toDb(result.peak));
        std::printf("hash:     %016llx\n", (unsigned long long) result.hash);
        if (out.isNotEmpty()) {
            juce::File file(out);
            file.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
            if (stream != nullptr) {
                std::unique_ptr<juce::AudioFormatWriter> writer(
                    wav.createWriterFor(stream.get(), 48000.0, 2, 24, {}, 0));
                if (writer != nullptr) {
                    stream.release();
                    const float* channels[2] = {result.left.data(), result.right.data()};
                    writer->writeFromFloatArrays(channels, 2, int(result.left.size()));
                }
            }
            std::printf("wrote:    %s\n", out.toRawUTF8());
        }
        return 0;
    }

    std::fprintf(stderr,
                 "sappkit-headless — station harness (no GUI, no message loop)\n"
                 "  sappkit-headless selftest [--fixture DIR]\n"
                 "  sappkit-headless render   [--preset N] [--out F.wav]\n"
                 "                            [--root DIR] [--settle MS] [--pump]\n"
                 "                            [--param ID=VALUE] [--cc N=V]\n");
    return 2;
}
