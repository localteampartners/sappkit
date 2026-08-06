// SappKitUiShot — renders the plugin editor offscreen and writes a PNG.
// Used to verify UI changes without a screen-recording session.
//   SappKitUiShot [output.png]
//   SappKitUiShot --cctest    (end-to-end SappLink proof through the plugin)

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

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

    void initialise(const juce::String& commandLine) override
    {
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
};

START_JUCE_APPLICATION(UiShotApp)
