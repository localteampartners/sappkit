#pragma once
// SappKit plugin processor: JUCE wrapper around KitEngine.
// Owns parameters (APVTS), host state, MIDI conversion, instrument loading,
// and the debounced pad-override rebuild. All sampler/kit DSP lives below in
// sappkit_core / SappSounds.
//
// THREADING, and why it is shaped this way (issue #1):
// a VST3 plug-in loaded by a non-JUCE headless host (sappradio) HAS a
// MessageManager but nothing ever pumps it. juce::Timer callbacks and
// MessageManager::callAsync() therefore never fire — silently, with no
// assert and no error. The old design installed every kit from a callAsync
// and rebuilt pad overrides from a Timer, so headless the plugin rendered
// exactly zero samples: not even the construction diagnostic kit landed.
// Measured -200.00 dBFS with 0 voices and 0 pads.
//
// So: a dedicated LOADER THREAD owns the LoadJob queue and is the ONE place
// an instrument is installed, and it also runs the 8 Hz pad-override /
// mix-save maintenance tick. It is joined in the destructor (the old
// detached std::thread closures captured `this` and could outlive the
// instance). juce::Timer survives as an EDITOR hook only — nothing about
// loading depends on it.

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include <juce_audio_utils/juce_audio_utils.h>

#include <sapp/sounds/InstrumentLoader.h>

#include "../core/KitEngine.h"
#include "../core/KitMix.h"
#include "../core/KitModel.h"
#include "UserPresets.h"

namespace sappkit {

// Set this to a file path to tee the SappKit-* identity lines there as well
// as to the host log — the station uses it to answer "which kit sounded?".
inline constexpr const char* kLogEnvVar = "SAPP_KIT_LOG";

// One identity/diagnostic line, to the JUCE logger, to stderr on Windows
// (where the fallback logger is invisible to a station box), and to
// $SAPP_KIT_LOG when set.
void logLine(const juce::String& message);

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

    // Select factory kit program N. Any thread: the load is enqueued onto
    // the loader thread.
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
    // <Documents>/SappSounds/presets/sappkit/<name>.json. Any thread.
    bool saveUserPreset(const juce::String& name, const juce::String& notes, juce::String& error);

    // Load a user preset by name (case-insensitive). Any thread.
    bool loadUserPreset(const juce::String& name, juce::String& error);

    // Fresh scan of the user preset folder.
    std::vector<sapp::userpresets::UserPreset> userPresets() const;

    // Choice-list geometry of the `preset` parameter: [0, factoryPresetCount)
    // are factory kit programs, the rest are the user presets discovered when
    // this instance was constructed.
    int factoryPresetCount() const;

    // Apply choice N of the `preset` parameter. Any thread.
    void applyPresetChoice(int index);

    // Last preset save/load outcome, for the editor to show. Any thread.
    juce::String presetStatus() const;
    void setPresetStatus(const juce::String& message);

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- SappKit ------------------------------------------------------------
    juce::AudioProcessorValueTreeState& valueTree() { return apvts_; }
    sapp::kit::KitEngine& engine() { return engine_; }

    // Instrument management, callable from any thread: these only ENQUEUE —
    // the loader thread performs every install, so they work with no message
    // loop at all.
    void loadSfzInstrument(const juce::File& sfzFile);
    void loadDiagnosticKit();
    juce::String currentInstrumentName() const;
    juce::String currentInstrumentPath() const;
    juce::String loadStatus() const;
    bool isLoading() const { return loading_.load(); }

    /// Readiness readout, also exposed as the read-only `libraryReady` host
    /// parameter: false from the instant a load is requested, true once that
    /// kit is installed and nothing is queued. A headless host polls this
    /// instead of guessing a settle window (sappradio#3).
    bool libraryReady() const;

    // Pad map of the loaded instrument (any thread).
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
    void timerCallback() override;              // EDITOR HOOK ONLY
    void pushParamsToEngine();
    sapp::kit::PadOverrides readPadOverrides() const;

    // --- the loader thread: the ONE place a kit is installed ---------------
    // SappKit has exactly one instrument slot, so `generation` IS the
    // per-slot guard: a job whose generation has been superseded RETURNS
    // (never falls through to install a stale kit). sapporchestra #2's
    // global-guard bug — 15 of a 16-slot restore silently dropped — cannot
    // arise with one slot, but the return-on-miss rule is kept for it.
    struct LoadJob {
        enum class Kind { Diagnostic, Sfz };
        Kind kind = Kind::Diagnostic;
        juce::String path;          // Sfz only
        uint64_t generation = 0;
        // true when the incoming mix is already carried by host state or by
        // a user preset, so the kit must NOT overwrite it from its own
        // saved-mix file.
        bool fromRestore = false;
    };
    void loaderLoop();
    void enqueueLoad(LoadJob job);
    void enqueueSfz(const juce::File& sfzFile, bool fromRestore);
    void enqueueDiagnostic(bool fromRestore);
    void performLoad(LoadJob job);
    void maintenanceTick();             // 8 Hz: pad rebuild debounce + mix save
    void publishReadiness();
    void logInstalled(const juce::String& what, bool ok);
    void logAudioSourceIfNeeded();

    void finishLoad(sapp::sounds::LoadResult result, const juce::String& path,
                    uint64_t generation, bool fromRestore);
    void applySavedMixOrDefaults();     // loader thread, after a fresh load
    void applyUserPresetParams(const sapp::userpresets::UserPreset& preset);
    void setParamValue(const juce::String& paramId, float plainValue);
    std::array<float, 11> readBusValues() const;
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
    std::atomic<float>* pClean_ = nullptr;
    std::array<std::array<std::atomic<float>*, 4>, sapp::kit::kNumPads> padParams_{};

    // Read-only readiness readout. Deliberately OUTSIDE the APVTS: host state
    // must never be able to restore a stale "ready", and it must not appear
    // in a saved user preset. Appended LAST (after every APVTS parameter) so
    // no existing automation index moves.
    //
    // HOW IT IS SURFACED — read this before writing a poller (sappradio#3):
    // a plain juce::AudioParameterBool added with addParameter(), marked
    // non-automatable and meta. Same shape as SappOrchestra and SappChoir.
    // In JUCE's VST3 wrapper a non-automatable, non-meter parameter gets
    // flags = 0, i.e. an ordinary VST3 INPUT parameter — not a VST3 output
    // parameter. The direction of travel is the same either way, though: the
    // PROCESSOR owns the value and VST3 offers no way to push it to the
    // controller outside process(), so JUCE ships it in
    // data.outputParameterChanges. A host reading it through the VST3
    // controller therefore sees it only AFTER at least one processBlock —
    // render (or parameter-flush) a block, then poll. A host holding the
    // AudioProcessor directly (the headless harness, a JUCE host) sees it the
    // instant the loader thread sets it.
    //
    // `withMeta` is not cosmetic: without it auval fails its "parameter
    // values across initialization" check, because the plugin keeps
    // rewriting a value the host wrote.
    juce::AudioParameterBool* libraryReady_ = nullptr;

    // SappLink CC-in (see src/core/SappLinkCCMap.h): mapped controllers land
    // as slew targets; each block moves the APVTS parameter a fraction of the
    // way — the same normalized path host automation uses — so 7-bit CC steps
    // don't zipper.
    struct CcSlew {
        juce::RangedAudioParameter* parameter = nullptr;
        float target = 0.0f, current = 0.0f;
        bool active = false;
    };
    std::array<CcSlew, 9> ccSlews_;
    void handleSappLinkCc(int ccNumber, int ccValue);
    void advanceCcSlews(int numSamples);

    std::vector<sapp::sounds::MidiEvent> eventScratch_;

    // MIDI program change lands on the audio thread; the kit load itself is
    // applied by the LOADER THREAD (never the audio thread, and never the
    // message thread — see the threading note at the top).
    std::atomic<int> pendingProgram_{-1};
    std::atomic<int> currentProgram_{0};

    // The `preset` parameter (sapplink/PRESETS.md section 3) can be moved from
    // the audio thread by host automation, so its listener only stores an
    // index; the loader thread applies it. applyingPreset_ is set while WE
    // move the parameter, so keeping it in sync after a kit change never
    // re-enters the load.
    void parameterChanged(const juce::String& parameterId, float newValue) override;
    std::atomic<int> pendingPresetChoice_{-1};
    std::atomic<bool> applyingPreset_{false};
    void syncPresetParameter(int choiceIndex);
    juce::String presetStatus_;         // guarded by loadLock_

    // Instrument state (loader thread writes, any thread reads under loadLock_).
    juce::String sfzPath_;                 // "" = diagnostic kit
    juce::String instrumentName_{"(loading)"};
    juce::String loadStatus_{"starting"};
    std::atomic<bool> loading_{false};
    std::atomic<uint64_t> loadGeneration_{0};
    std::atomic<int> installCount_{0};      // kits actually installed
    mutable juce::CriticalSection loadLock_;

    // Loader thread + its queue. Every install runs here, message loop or no
    // message loop (issue #1).
    std::deque<LoadJob> loadQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::atomic<int> jobsOutstanding_{0};
    std::atomic<bool> loaderStop_{false};
    std::thread loaderThread_;

    sapp::sounds::InstrumentPtr baseInstrument_;   // loader thread only
    sapp::kit::KitModel model_;                    // guarded by loadLock_
    sapp::kit::PadOverrides appliedOverrides_{};   // loader thread only

    // Editor hooks, fired from the timer on the message thread. Nothing about
    // loading depends on either.
    std::atomic<bool> instrumentChangedFlag_{false};
    std::atomic<bool> hostDisplayDirty_{false};

    // Identity logging: a voice batch starting from silence names the kit
    // that produced it (sappkeys #1 / sapptune #21).
    std::atomic<bool> audioBatchStarted_{false};
    int lastVoiceCount_ = 0;               // audio thread only
    double lastAudioSourceLogMs_ = 0.0;    // loader thread only
    uint32_t lastMaintenanceMs_ = 0;       // loader thread only

    // Mix persistence state. suppressMixSave_ covers the param churn a
    // load/restore itself causes; it clears on the first quiet maintenance
    // tick, after which only real user tweaks arm the save countdown.
    std::atomic<bool> suppressMixSave_{true};
    std::atomic<int> mixSaveCountdown_{-1};  // 8 Hz ticks until save; -1 = idle
    std::atomic<bool> lastBusValid_{false};
    std::array<float, 11> lastBus_{};        // loader thread only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SappKitProcessor)
};

} // namespace sappkit
