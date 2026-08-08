#pragma once
// SappKit plugin processor: JUCE wrapper around KitEngine.
// Owns parameters (APVTS), host state, MIDI conversion, async instrument
// loading, and the debounced pad-override rebuild. All sampler/kit DSP lives
// below in sappkit_core / SappSounds.

#include <array>
#include <atomic>
#include <memory>
#include <thread>

#include <juce_audio_utils/juce_audio_utils.h>

#include <sapp/sounds/InstrumentLoader.h>

#include "../core/KitEngine.h"
#include "../core/KitMix.h"
#include "../core/KitModel.h"

namespace sappkit {

class SappKitProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    SappKitProcessor();
    ~SappKitProcessor() override;

    // --- AudioProcessor -----------------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SappKit"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    // Factory kit programs (see FactoryKits.h): program N loads kit N via
    // the same path the sounds browser uses (loadKitSfz + saved kit mixes).
    // Reachable from the host program API and via MIDI program change
    // (SappLink set_patches). Programs whose library is not installed are a
    // no-op with a status message. CCs keep working on top.
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram_.load(); }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    // Load factory kit program N now. Message thread only.
    void applyKitProgram(int index);

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- SappKit ------------------------------------------------------------
    juce::AudioProcessorValueTreeState& valueTree() { return apvts_; }
    sapp::kit::KitEngine& engine() { return engine_; }

    // Async instrument management (message thread).
    void loadSfzInstrument(const juce::File& sfzFile);
    void loadDiagnosticKit();
    juce::String currentInstrumentName() const;
    juce::String currentInstrumentPath() const { return sfzPath_; }
    juce::String loadStatus() const;
    bool isLoading() const { return loading_.load(); }

    // Pad map of the loaded instrument (message/UI thread).
    sapp::kit::KitModel kitModel() const;

    // Stable per-pad parameter IDs ("pad3Tune"), 1-based pad numbers.
    static juce::String padParamId(int padNumber, const char* suffix);

    juce::MidiKeyboardState keyboardState;

    std::function<void()> onInstrumentChanged;  // editor hook (message thread)

    // Kit-mix persistence: one JSON per kit (see core/KitMix.h) in the shared
    // Sapp settings dir, read and written by the plugin, the CLI, and agents.
    // The mix auto-loads with its kit and auto-saves ~2 s after a tweak.
    static juce::File kitMixDir();
    juce::File currentKitMixFile() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void timerCallback() override;              // debounced pad-override rebuild
    void pushParamsToEngine();
    sapp::kit::PadOverrides readPadOverrides() const;
    void finishLoad(sapp::sounds::LoadResult result, const juce::String& path,
                    uint64_t generation);
    void rebuildOverriddenInstrument();
    void applySavedMixOrDefaults();     // message thread, after a fresh load
    void setParamValue(const juce::String& paramId, float plainValue);
    std::array<float, 10> readBusValues() const;
    void saveMixNow();

    juce::AudioProcessorValueTreeState apvts_;
    sapp::kit::KitEngine engine_;

    // Cached raw parameter pointers (audio thread reads).
    std::atomic<float>* pPunch_ = nullptr;
    std::atomic<float>* pSquash_ = nullptr;
    std::atomic<float>* pCrush_ = nullptr;
    std::atomic<float>* pRoomLevel_ = nullptr;
    std::atomic<float>* pRoomSize_ = nullptr;
    std::atomic<float>* pWidth_ = nullptr;
    std::atomic<float>* pHumanize_ = nullptr;
    std::atomic<float>* pMaster_ = nullptr;
    std::atomic<float>* pLimiter_ = nullptr;
    std::atomic<float>* pQuality_ = nullptr;
    std::array<std::array<std::atomic<float>*, 4>, sapp::kit::kNumPads> padParams_{};

    // SappLink CC-in (see src/core/SappLinkCCMap.h): mapped controllers land
    // as slew targets; each block moves the APVTS parameter a fraction of the
    // way — the same normalized path host automation uses — so 7-bit CC steps
    // don't zipper.
    struct CcSlew {
        juce::RangedAudioParameter* parameter = nullptr;
        float target = 0.0f, current = 0.0f;
        bool active = false;
    };
    std::array<CcSlew, 8> ccSlews_;
    void handleSappLinkCc(int ccNumber, int ccValue);
    void advanceCcSlews(int numSamples);

    std::vector<sapp::sounds::MidiEvent> eventScratch_;

    // MIDI program change lands on the audio thread; the kit load itself is
    // triggered from the timer (message thread), sappstep-style.
    std::atomic<int> pendingProgram_{-1};
    std::atomic<int> currentProgram_{0};

    // Instrument state (message thread + load thread under loadLock_).
    juce::String sfzPath_;                 // "" = diagnostic kit
    juce::String instrumentName_{"(loading)"};
    juce::String loadStatus_{"starting"};
    std::atomic<bool> loading_{false};
    std::atomic<uint64_t> loadGeneration_{0};
    juce::CriticalSection loadLock_;

    sapp::sounds::InstrumentPtr baseInstrument_;   // unmodified load result
    sapp::kit::KitModel model_;
    sapp::kit::PadOverrides appliedOverrides_{};
    std::atomic<bool> rebuildInFlight_{false};

    // Mix persistence state (message thread). suppressMixSave_ covers the
    // param churn a load/restore itself causes; it clears on the first quiet
    // timer tick, after which only real user tweaks arm the save countdown.
    bool restorePending_ = false;     // next finishLoad comes from host state
    bool suppressMixSave_ = true;
    int mixSaveCountdown_ = -1;       // 8 Hz ticks until save; -1 = idle
    std::array<float, 10> lastBus_{};
    bool lastBusValid_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SappKitProcessor)
};

} // namespace sappkit
