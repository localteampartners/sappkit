// SappKitUiShot — renders the plugin editor offscreen and writes a PNG.
// Used to verify UI changes without a screen-recording session.
//   SappKitUiShot [output.png]
//   SappKitUiShot --cctest             (end-to-end SappLink proof through the plugin)
//   SappKitUiShot --presettest [dir]   (user-preset round trip, sapplink/PRESETS.md)

#include <cstdlib>

#include <juce_audio_utils/juce_audio_utils.h>

#include "FactoryKits.h"
#include "PluginProcessor.h"
#include "SoundsPanel.h"
#include "UserPresets.h"

namespace {

// Every parameter except `preset` itself (never captured, and its choice list
// differs between an instance created before and after a preset was saved).
using ParamSnapshot = std::vector<std::pair<juce::String, float>>;

ParamSnapshot snapshotParams(juce::AudioProcessor& processor)
{
    ParamSnapshot out;
    for (auto* parameter : processor.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            if (withId->paramID != sapp::userpresets::kPresetParamId)
                out.push_back({withId->paramID, withId->getValue()});
    return out;
}

// Max |normalised difference| between a snapshot and a live processor.
double maxDiff(const ParamSnapshot& reference, sappkit::SappKitProcessor& processor,
               juce::String& worstId, int& differing)
{
    double worst = 0.0;
    differing = 0;
    for (const auto& entry : reference) {
        auto* parameter = processor.valueTree().getParameter(entry.first);
        if (parameter == nullptr)
            continue;
        const double d = std::abs(double(parameter->getValue()) - double(entry.second));
        if (d > 0.0) ++differing;
        if (d > worst) {
            worst = d;
            worstId = entry.first;
        }
    }
    return worst;
}

float normOf(sappkit::SappKitProcessor& processor, const juce::String& id)
{
    auto* parameter = processor.valueTree().getParameter(id);
    return parameter != nullptr ? parameter->getValue() : -1.0f;
}

void setNorm(sappkit::SappKitProcessor& processor, const juce::String& id, float value)
{
    if (auto* parameter = processor.valueTree().getParameter(id))
        parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, value));
}

} // namespace

class UiShotApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "SappKitUiShot"; }
    const juce::String getApplicationVersion() override { return "1.0"; }

    // --cctest: CC 7 (masterGain) arrives via processBlock, slews the APVTS
    // parameter exactly like host automation, and must scale the output level.
    void runCcTest()
    {
        processor = std::make_unique<sappkit::SappKitProcessor>();
        processor->prepareToPlay(48000.0, 512);
        // The diagnostic kit loads asynchronously on the message thread; give
        // it time on the normal run loop, then measure.
        juce::Timer::callAfterDelay(2000, [this] { finishCcTest(); });
    }

    void finishCcTest()
    {
        processor->valueTree().getParameter("roomLevel")->setValueNotifyingHost(0.0f);
        processor->valueTree().getParameter("limiter")->setValueNotifyingHost(0.0f);

        juce::AudioBuffer<float> buffer(2, 512);
        auto measure = [&](int ccValue) {
            double energy = 0.0;
            for (int b = 0; b < 100; ++b) {   // ~1 s per side
                juce::MidiBuffer midi;
                if (b == 0)
                    midi.addEvent(juce::MidiMessage::controllerEvent(1, 7, ccValue), 0);
                if (b % 20 == 5)
                    midi.addEvent(juce::MidiMessage::noteOn(1, 38, 0.8f), 1);
                buffer.clear();
                processor->processBlock(buffer, midi);
                if (b > 30) {  // measure after the slew settles
                    for (int i = 0; i < 512; ++i) {
                        energy += double(buffer.getSample(0, i)) * buffer.getSample(0, i);
                        energy += double(buffer.getSample(1, i)) * buffer.getSample(1, i);
                    }
                }
            }
            for (int b = 0; b < 40; ++b) { juce::MidiBuffer none; buffer.clear(); processor->processBlock(buffer, none); }
            return energy;
        };

        const double quiet = measure(0);     // -24 dB
        const double loud = measure(127);    // +12 dB
        const bool pass = loud > quiet * 16.0;
        std::printf("SappLink CC7 sweep: cc=0 %.3g  cc=127 %.3g  [%s]\n",
                    quiet, loud, pass ? "PASS" : "FAIL");
        processor.reset();
        setApplicationReturnValue(pass ? 0 : 1);
        quit();
    }

    // ------------------------------------------------------ preset round trip --
    // --presettest: the SappLink user-preset contract, proved end to end
    // through the real processor (sapplink/PRESETS.md). Presets are written to
    // a throwaway dir via SAPPSOUNDS_PRESETS, never the user's own folder.
    // Everything runs on the message thread; the steps are chained through the
    // normal run loop because kit loading is asynchronous.

    void fail(const juce::String& what)
    {
        std::printf("  [FAIL] %s\n", what.toRawUTF8());
        ++failures_;
    }
    void pass(const juce::String& what) { std::printf("  [PASS] %s\n", what.toRawUTF8()); }
    void check(bool ok, const juce::String& what) { ok ? pass(what) : fail(what); }

    void runPresetTest(const juce::String& dirArg)
    {
        presetRoot_ = dirArg.isNotEmpty()
            ? juce::File(dirArg)
            : juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getChildFile("sappkit-presettest-" +
                                juce::String(juce::Time::currentTimeMillis()));
        presetRoot_.createDirectory();
        ::setenv("SAPPSOUNDS_PRESETS", presetRoot_.getFullPathName().toRawUTF8(), 1);
        std::printf("SAPPSOUNDS_PRESETS = %s\n", presetRoot_.getFullPathName().toRawUTF8());

        processor = std::make_unique<sappkit::SappKitProcessor>();
        processor->prepareToPlay(48000.0, 512);

        // Capture against a real sample kit when one is installed, so the
        // preset's `sfz` hint (and the reload on the way back in) is exercised.
        kitProgram_ = 0;
        for (int i = 1; i < int(sappkit::factorykits::all().size()); ++i) {
            if (sappkit::factorykits::resolveKit(i, sappkit::SoundsPanel::samplesRoot())
                    .existsAsFile()) {
                kitProgram_ = i;
                break;
            }
        }
        std::printf("capture kit: program %d \"%s\"\n", kitProgram_,
                    processor->getProgramName(kitProgram_).toRawUTF8());
        if (kitProgram_ > 0)
            processor->applyKitProgram(kitProgram_);

        juce::Timer::callAfterDelay(4000, [this] { presetStepSave(); });
    }

    // 1. nudge bus AND pad parameters, 2. save + print the JSON on disk.
    void presetStepSave()
    {
        std::printf("\n-- step 1/2: nudge parameters, save preset --\n");
        const char* ids[] = {"punch",   "roomLevel", "masterGain", "width",    "humanize",
                             "quality", "pad3Tune",  "pad7Level",  "pad12Pan", "pad16Decay"};
        const float norms[] = {0.8123f, 0.4211f, 0.7050f, 0.3100f, 0.6600f,
                               0.0f,    0.6000f, 0.2200f, 0.9000f, 0.3300f};
        for (size_t i = 0; i < std::size(ids); ++i) {
            setNorm(*processor, ids[i], norms[i]);
            std::printf("  set %-11s -> %.9f\n", ids[i], normOf(*processor, ids[i]));
        }
        kitPath_ = processor->currentInstrumentPath();
        std::printf("  kit path: \"%s\"\n", kitPath_.toRawUTF8());

        reference_ = snapshotParams(*processor);
        std::printf("  snapshot: %d parameters (excluding \"preset\")\n", int(reference_.size()));

        juce::String error;
        const bool saved = processor->saveUserPreset("RoundTrip Test",
                                                     "headless round-trip proof", error);
        check(saved, "saveUserPreset(\"RoundTrip Test\") -> " +
                         (saved ? juce::String("ok") : error));

        const auto file = sapp::userpresets::presetDir(sappkit::SappKitProcessor::kInstrument)
                              .getChildFile("RoundTrip Test.json");
        check(file.existsAsFile(), "file on disk: " + file.getFullPathName());
        std::printf("---- %s ----\n%s---- end ----\n", file.getFileName().toRawUTF8(),
                    file.loadFileAsString().toRawUTF8());
        savedSfz_ = juce::JSON::parse(file.loadFileAsString()).getProperty("sfz", "").toString();
        check(savedSfz_ == kitPath_, "preset \"sfz\" field == loaded kit path (\"" +
                                         savedSfz_ + "\")");

        // Destroy before the ~2 s mix-save countdown can fire: this test must
        // not rewrite the user's real per-kit mix files.
        processor.reset();
        juce::Timer::callAfterDelay(200, [this] {
            std::printf("\n-- step 3: fresh processor --\n");
            processor = std::make_unique<sappkit::SappKitProcessor>();
            processor->prepareToPlay(48000.0, 512);
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(
                    processor->valueTree().getParameter(sapp::userpresets::kPresetParamId))) {
                std::printf("  `preset` choices: %d factory + %d user (last = \"%s\")\n",
                            processor->factoryPresetCount(),
                            choice->choices.size() - processor->factoryPresetCount(),
                            choice->choices[choice->choices.size() - 1].toRawUTF8());
                check(choice->choices.size() == processor->factoryPresetCount() + 1,
                      "the saved preset joined the `preset` choice list");
            }
            juce::Timer::callAfterDelay(2000, [this] { presetStepLoad(); });
        });
    }

    // 3./4. scramble the fresh processor, load by name, compare everything.
    void presetStepLoad()
    {
        std::printf("\n-- step 4: scramble, load by name, compare --\n");
        for (const auto& entry : reference_)
            setNorm(*processor, entry.first, std::fmod(entry.second + 0.37f, 1.0f));

        juce::String worst;
        int differing = 0;
        double diff = maxDiff(reference_, *processor, worst, differing);
        std::printf("  before load: max |diff| = %.9f (%s), %d/%d parameters differ\n",
                    diff, worst.toRawUTF8(), differing, int(reference_.size()));
        check(differing > 0, "scramble actually moved the state");

        juce::String error;
        check(processor->loadUserPreset("RoundTrip Test", error),
              "loadUserPreset(\"RoundTrip Test\") -> " + error);

        juce::Timer::callAfterDelay(3000, [this] {
            juce::String worstId;
            int differing2 = 0;
            const double diff2 = maxDiff(reference_, *processor, worstId, differing2);
            std::printf("  after load:  max |diff| = %.9f (worst id: %s), %d parameters differ\n",
                        diff2, worstId.isEmpty() ? "-" : worstId.toRawUTF8(), differing2);
            check(diff2 == 0.0, "every parameter round-tripped exactly (max |diff| == 0)");
            std::printf("  kit reloaded from the preset's `sfz`: \"%s\"\n",
                        processor->currentInstrumentPath().toRawUTF8());
            check(processor->currentInstrumentPath() == savedSfz_,
                  "loaded kit == the kit the preset was captured with");
            presetStepParameter();
        });
    }

    // 5. the host-automatable `preset` parameter actually changes state.
    void presetStepParameter()
    {
        std::printf("\n-- step 5: the `preset` parameter --\n");
        const float punchWas = normOf(*processor, "punch");
        const float tuneWas = normOf(*processor, "pad3Tune");
        setNorm(*processor, "punch", 0.0500f);
        setNorm(*processor, "pad3Tune", 0.9500f);
        std::printf("  before: punch = %.9f  pad3Tune = %.9f\n",
                    normOf(*processor, "punch"), normOf(*processor, "pad3Tune"));

        auto* choice = dynamic_cast<juce::AudioParameterChoice*>(
            processor->valueTree().getParameter(sapp::userpresets::kPresetParamId));
        const int userIndex = processor->factoryPresetCount();   // first user preset
        std::printf("  setting `preset` -> index %d (\"%s\")\n", userIndex,
                    choice->choices[userIndex].toRawUTF8());
        choice->setValueNotifyingHost(choice->convertTo0to1(float(userIndex)));

        juce::Timer::callAfterDelay(1500, [this, punchWas, tuneWas, choice] {
            const float punchNow = normOf(*processor, "punch");
            const float tuneNow = normOf(*processor, "pad3Tune");
            std::printf("  after:  punch = %.9f  pad3Tune = %.9f\n", punchNow, tuneNow);
            check(punchNow == punchWas && tuneNow == tuneWas,
                  "the `preset` parameter loaded the preset (both values restored)");
            check(choice->getIndex() == processor->factoryPresetCount(),
                  "`preset` parameter stayed on the chosen entry");
            presetStepProgramChange();
        });
    }

    // 6. regression: MIDI program change still selects factory kits.
    void presetStepProgramChange()
    {
        std::printf("\n-- step 6: MIDI program change regression --\n");
        std::printf("  before: program %d, kit \"%s\"\n", processor->getCurrentProgram(),
                    processor->currentInstrumentPath().toRawUTF8());
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::programChange(1, 0), 0);   // diagnostic kit
        buffer.clear();
        processor->processBlock(buffer, midi);

        juce::Timer::callAfterDelay(2000, [this] {
            std::printf("  after PC 0: program %d, kit \"%s\" (\"\" = built-in)\n",
                        processor->getCurrentProgram(),
                        processor->currentInstrumentPath().toRawUTF8());
            check(processor->getCurrentProgram() == 0 &&
                      processor->currentInstrumentPath().isEmpty(),
                  "program change 0 selected the built-in kit");

            if (kitProgram_ > 0) {
                juce::AudioBuffer<float> buffer2(2, 512);
                juce::MidiBuffer midi2;
                midi2.addEvent(juce::MidiMessage::programChange(1, kitProgram_), 0);
                buffer2.clear();
                processor->processBlock(buffer2, midi2);
                juce::Timer::callAfterDelay(3000, [this] {
                    std::printf("  after PC %d: program %d, kit \"%s\"\n", kitProgram_,
                                processor->getCurrentProgram(),
                                processor->currentInstrumentPath().toRawUTF8());
                    check(processor->getCurrentProgram() == kitProgram_ &&
                              processor->currentInstrumentPath() == kitPath_,
                          "program change " + juce::String(kitProgram_) + " loaded its kit");
                    auto* choice = dynamic_cast<juce::AudioParameterChoice*>(
                        processor->valueTree().getParameter(sapp::userpresets::kPresetParamId));
                    std::printf("  `preset` parameter followed to index %d\n", choice->getIndex());
                    check(choice->getIndex() == kitProgram_,
                          "`preset` chooser followed the program change");
                    presetStepHostState();
                });
                return;
            }
            presetStepHostState();
        });
    }

    // 7. regression: host state save/restore still round-trips.
    void presetStepHostState()
    {
        std::printf("\n-- step 7: host state round trip regression --\n");
        stateReference_ = snapshotParams(*processor);
        stateBlob_.reset();
        processor->getStateInformation(stateBlob_);
        std::printf("  getStateInformation: %d bytes\n", int(stateBlob_.getSize()));

        for (const auto& entry : stateReference_)
            setNorm(*processor, entry.first, std::fmod(entry.second + 0.41f, 1.0f));
        juce::String worst;
        int differing = 0;
        std::printf("  scrambled: max |diff| = %.9f, %d parameters differ\n",
                    maxDiff(stateReference_, *processor, worst, differing), differing);

        processor->setStateInformation(stateBlob_.getData(), int(stateBlob_.getSize()));
        juce::Timer::callAfterDelay(3000, [this] {
            juce::String worstId;
            int differing2 = 0;
            const double diff = maxDiff(stateReference_, *processor, worstId, differing2);
            std::printf("  restored:  max |diff| = %.9f (worst id: %s), %d parameters differ\n",
                        diff, worstId.isEmpty() ? "-" : worstId.toRawUTF8(), differing2);
            // The APVTS XML serialisation is decimal, so this path is exact to
            // float printing, not bit-exact — that is pre-existing behaviour,
            // unrelated to presets. Presets round-trip exactly (step 4).
            check(diff < 1.0e-6, "host state restored every parameter (< 1e-6)");
            finishPresetTest();
        });
    }

    void finishPresetTest()
    {
        processor.reset();
        std::printf("\npreset round trip: %s (%d failure%s)\n",
                    failures_ == 0 ? "PASS" : "FAIL", failures_, failures_ == 1 ? "" : "s");
        setApplicationReturnValue(failures_ == 0 ? 0 : 1);
        quit();
    }

    void initialise(const juce::String& commandLine) override
    {
        if (commandLine.contains("--presettest")) {
            runPresetTest(commandLine.fromFirstOccurrenceOf("--presettest", false, false)
                              .trim()
                              .unquoted());
            return;
        }
        if (commandLine.contains("--cctest")) {
            runCcTest();
            return;
        }

        const juce::String outPath = commandLine.trim().isNotEmpty()
            ? commandLine.trim().unquoted() : juce::String("/tmp/sappkit-ui.png");

        processor = std::make_unique<sappkit::SappKitProcessor>();
        processor->prepareToPlay(48000.0, 512);
        editor.reset(processor->createEditor());

        // Give the async diagnostic-kit load and fonts time to settle, then
        // play a few hits so pads flash and the meter is alive in the shot.
        juce::Timer::callAfterDelay(2200, [this, outPath]
        {
            juce::AudioBuffer<float> buffer(2, 512);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.9f), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 42, 0.7f), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 51, 0.75f), 0);
            for (int i = 0; i < 12; ++i) {
                buffer.clear();
                processor->processBlock(buffer, midi);
                midi.clear();
            }

            juce::Timer::callAfterDelay(250, [this, outPath]
            {
                auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
                juce::File file(outPath);
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat png;
                if (stream.openedOk() && png.writeImageToStream(snapshot, stream))
                    std::printf("wrote %s (%dx%d)\n", outPath.toRawUTF8(),
                                snapshot.getWidth(), snapshot.getHeight());
                else
                    std::printf("FAILED to write %s\n", outPath.toRawUTF8());
                editor.reset();
                processor.reset();
                quit();
            });
        });
    }

    void shutdown() override {}

private:
    std::unique_ptr<sappkit::SappKitProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;

    // --presettest state.
    juce::File presetRoot_;
    ParamSnapshot reference_, stateReference_;
    juce::MemoryBlock stateBlob_;
    juce::String kitPath_, savedSfz_;
    int kitProgram_ = 0;
    int failures_ = 0;
};

START_JUCE_APPLICATION(UiShotApp)
