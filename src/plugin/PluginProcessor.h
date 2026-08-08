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
#include "UserPresets.h"

namespace sappkit {

class SappKitProcessor : public juce::AudioProcessor,
                         private juce::AudioProcessorValueTreeState::Listener,
                         private juce::Timer
{
public:
    // The SappLink instrument name: names the user-preset folder and must
    // match sapplink/manifests/sappkit.json.
    static constexpr const char* kInstrument = "sappkit";

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

    // ---------------------------------------------------------- user presets --
    // Saved sounds, shared format across the suite (sapplink/PRESETS.md).
    // Factory kits stay addressed by program index; user presets are addressed
    // by NAME, so the two can never collide.
    //
    // A sappkit "sound" is the parameter state (10 kit-bus params + 4 per pad)
    // PLUS the kit it was captured with: the pad params only mean anything
    // against that pad map. The kit travels in the preset's optional `sfz`
    // field and is reloaded on the way in when the path still resolves.

    // Capture the current parameter state + loaded kit path to
    // <Documents>/SappSounds/presets/sappkit/<name>.json. Message thread.
    bool saveUserPreset(const juce::String& name, const juce::String& notes, juce::String& error);

    // Load a user preset by name (case-insensitive). Message thread.
    bool loadUserPreset(const juce::String& name, juce::String& error);

    // Fresh scan of the user preset folder.
    std::vector<sapp::userpresets::UserPreset> userPresets() const;

    // Choice-list geometry of the `preset` parameter: [0, factoryPresetCount)
    // are factory kit programs, the rest are the user presets discovered when
    // this instance was constructed.
    int factoryPresetCount() const;

    // Apply choice N of the `preset` parameter. Message thread.
    void applyPresetChoice(int index);

    // Last preset save/load outcome, for the editor to show. Any thread.
    juce::String presetStatus() const;
    void setPresetStatus(const juce::String& message);

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
    void applyUserPresetParams(const sapp::userpresets::UserPreset& preset);
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

    // The `preset` parameter (sapplink/PRESETS.md section 3) can be moved from
    // the audio thread by host automation, so its listener only stores an
    // index — the same 8 Hz timer that already defers program changes does the
    // loading. applyingPreset_ is set while WE move the parameter, so keeping
    // it in sync after a kit change never re-enters the load.
    void parameterChanged(const juce::String& parameterId, float newValue) override;
    std::atomic<int> pendingPresetChoice_{-1};
    bool applyingPreset_ = false;
    void syncPresetParameter(int choiceIndex);
    juce::String presetStatus_;         // guarded by loadLock_

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
