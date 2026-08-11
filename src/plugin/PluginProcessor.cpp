#include "PluginProcessor.h"

#include "../core/DiagnosticKit.h"
#include "../core/SappLinkCCMap.h"
#include "FactoryKits.h"
#include "PluginEditor.h"
#include "SoundsPanel.h"

namespace sappkit {

using namespace sapp::kit;
using sapp::sounds::MidiEvent;

namespace {
const char* kPadSuffixes[4] = {"Tune", "Decay", "Pan", "Level"};
}

void logLine(const juce::String& message)
{
    juce::Logger::writeToLog(message);
#if JUCE_WINDOWS
    // JUCE's fallback logger is OutputDebugString on Windows — invisible to a
    // station box redirecting the host's output. stderr is what it greps.
    std::fputs((message + "\n").toRawUTF8(), stderr);
    std::fflush(stderr);
#endif
    if (const char* path = std::getenv(kLogEnvVar))
        if (path[0] != 0)
            juce::File(juce::String::fromUTF8(path)).appendText(message + "\n");
}

juce::String SappKitProcessor::padParamId(int padNumber, const char* suffix)
{
    return "pad" + juce::String(padNumber) + suffix;
}

juce::AudioProcessorValueTreeState::ParameterLayout SappKitProcessor::makeLayout()
{
    using P = juce::AudioParameterFloat;
    using Range = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Parameter IDs are compatibility contracts — never reuse or renumber.
    layout.add(std::make_unique<P>(juce::ParameterID{"punch", 1}, "Punch",
                                   Range{0.0f, 1.0f, 0.001f}, 0.35f));
    layout.add(std::make_unique<P>(juce::ParameterID{"squash", 1}, "Squash",
                                   Range{0.0f, 1.0f, 0.001f}, 0.25f));
    layout.add(std::make_unique<P>(juce::ParameterID{"crush", 1}, "Crush",
                                   Range{0.0f, 1.0f, 0.001f}, 0.0f));
    layout.add(std::make_unique<P>(juce::ParameterID{"roomLevel", 1}, "Room",
                                   Range{0.0f, 1.0f, 0.001f}, 0.18f));
    layout.add(std::make_unique<P>(juce::ParameterID{"roomSize", 1}, "Room Size",
                                   Range{0.0f, 1.0f, 0.001f}, 0.4f));
    layout.add(std::make_unique<P>(juce::ParameterID{"width", 1}, "Width",
                                   Range{0.0f, 2.0f, 0.001f}, 1.0f));
    layout.add(std::make_unique<P>(juce::ParameterID{"humanize", 1}, "Humanize",
                                   Range{0.0f, 1.0f, 0.001f}, 0.15f));
    layout.add(std::make_unique<P>(juce::ParameterID{"masterGain", 1}, "Master Gain",
                                   Range{-24.0f, 12.0f, 0.1f}, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"limiter", 1}, "Safety Limiter", true));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"quality", 1}, "Quality",
        juce::StringArray{"Draft", "Normal"}, 1));

    for (int pad = 1; pad <= kNumPads; ++pad) {
        const juce::String n = "Pad " + juce::String(pad) + " ";
        layout.add(std::make_unique<P>(juce::ParameterID{padParamId(pad, "Tune"), 1},
                                       n + "Tune", Range{-12.0f, 12.0f, 0.01f}, 0.0f));
        layout.add(std::make_unique<P>(juce::ParameterID{padParamId(pad, "Decay"), 1},
                                       n + "Decay", Range{0.0f, 1.0f, 0.001f}, 1.0f));
        layout.add(std::make_unique<P>(juce::ParameterID{padParamId(pad, "Pan"), 1},
                                       n + "Pan", Range{-1.0f, 1.0f, 0.001f}, 0.0f));
        layout.add(std::make_unique<P>(juce::ParameterID{padParamId(pad, "Level"), 1},
                                       n + "Level", Range{-24.0f, 12.0f, 0.1f}, 0.0f));
    }

    // Host-automatable sound selection (sapplink/PRESETS.md section 3). ADDED
    // LAST so no existing parameter index moves — ids, order and CC mappings
    // are a contract with the SappLink manifest and with saved DAW sessions.
    // The factory kits in program order, then the user presets that exist
    // right now; the list is fixed for this instance's lifetime because a
    // choice parameter cannot change its choices without breaking automation
    // lanes. It carries no CC of its own.
    juce::StringArray presetChoices;
    for (const auto& kit : factorykits::all())
        presetChoices.add(kit.name);
    presetChoices.addArray(sapp::userpresets::choiceLabels(SappKitProcessor::kInstrument));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{sapp::userpresets::kPresetParamId, 1}, "Preset", presetChoices, 0));

    // Suite-wide `clean` convention (CC 3, sapptune's sappkit manifest):
    // 0 = every modeled imperfection as designed, 1 = none. Here that scales
    // `humanize` — SappKit's per-hit tune scatter — by (1 − clean). Appended
    // AFTER `preset` so no pre-existing automation index moves. Default 0
    // keeps the historical sound, and — issue #1's postmortem rule — no
    // parameter of this plugin may default to a value that silences it.
    layout.add(std::make_unique<P>(juce::ParameterID{"clean", 1}, "Clean",
                                   Range{0.0f, 1.0f, 0.001f}, 0.0f));

    return layout;
}

SappKitProcessor::SappKitProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "SappKit", makeLayout())
{
    auto raw = [this](const juce::String& id) { return apvts_.getRawParameterValue(id); };
    pPunch_ = raw("punch");
    pSquash_ = raw("squash");
    pCrush_ = raw("crush");
    pRoomLevel_ = raw("roomLevel");
    pRoomSize_ = raw("roomSize");
    pWidth_ = raw("width");
    pHumanize_ = raw("humanize");
    pMaster_ = raw("masterGain");
    pLimiter_ = raw("limiter");
    pQuality_ = raw("quality");
    pClean_ = raw("clean");
    for (int pad = 0; pad < kNumPads; ++pad)
        for (int p = 0; p < 4; ++p)
            padParams_[size_t(pad)][size_t(p)] = raw(padParamId(pad + 1, kPadSuffixes[p]));

    eventScratch_.reserve(512);

    const auto& table = sapplink::mappings();
    static_assert(std::tuple_size<decltype(ccSlews_)>::value == size_t(sapplink::kNumMappings),
                  "ccSlews_ must match the SappLink mapping table size");
    for (size_t i = 0; i < table.size(); ++i)
        ccSlews_[i].parameter = apvts_.getParameter(table[i].paramId);

    // Readiness readout (issue #1): a headless host polls this instead of
    // guessing a settle window. Outside the APVTS on purpose — see the
    // declaration. Appended last, after every APVTS parameter.
    // withMeta: the PLUGIN owns this value, not the host. auval's
    // "parameter values across initialization" check compares what it wrote
    // with what it reads back, and a readout the plugin keeps rewriting fails
    // that check unless it is declared meta (AU kIsGlobalMeta).
    libraryReady_ = new juce::AudioParameterBool(
        juce::ParameterID{"libraryReady", 1}, "Library Ready", false,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true));
    addParameter(libraryReady_);

    // Host-automatable sound selection (sapptune issue #13). The callback can
    // arrive on the audio thread; it only stores an index, and the LOADER
    // THREAD applies it.
    apvts_.addParameterListener(sapp::userpresets::kPresetParamId, this);

    // The loader thread owns every kit install. Started before the
    // construction diagnostic is queued so nothing waits on the host.
    loaderThread_ = std::thread([this] { loaderLoop(); });

    // The 8 Hz timer is an EDITOR convenience only. Nothing about loading or
    // the pad-override rebuild depends on it any more — see the threading
    // note in the header.
    startTimerHz(8);

    // Which build did the host just load, and what can it enumerate? One line
    // at construction turns "the wrong sound came out" into a log grep.
    logLine("SappKit-build: version=" SAPPKIT_VERSION
            " root=\"" + SoundsPanel::samplesRoot().getFullPathName()
            + "\" programs=" + juce::String(int(factorykits::all().size())));

    loadDiagnosticKit();
}

SappKitProcessor::~SappKitProcessor()
{
    stopTimer();
    apvts_.removeParameterListener(sapp::userpresets::kPresetParamId, this);
    // Join the loader thread before anything it touches goes away. The old
    // detached-thread + callAsync design left closures capturing `this` alive
    // past destruction — a crash waiting for the next pump.
    loaderStop_.store(true, std::memory_order_release);
    queueCv_.notify_all();
    if (loaderThread_.joinable())
        loaderThread_.join();
}

// -------------------------------------------------------- factory programs --

int SappKitProcessor::getNumPrograms()
{
    return int(factorykits::all().size());
}

const juce::String SappKitProcessor::getProgramName(int index)
{
    const auto& table = factorykits::all();
    if (index < 0 || index >= int(table.size()))
        return {};
    return table[size_t(index)].name;
}

void SappKitProcessor::setCurrentProgram(int index)
{
    // Hosts may call this from any thread; defer to the timer like a MIDI
    // program change. currentProgram_ updates immediately so hosts that read
    // it straight back see the new value.
    if (index < 0 || index >= getNumPrograms() || index == currentProgram_.load())
        return;
    currentProgram_.store(index);
    pendingProgram_.store(index);
    if (libraryReady_ != nullptr && libraryReady_->get())
        *libraryReady_ = false;
    queueCv_.notify_all();
}

void SappKitProcessor::applyKitProgram(int index)
{
    const auto& table = factorykits::all();
    if (index < 0 || index >= int(table.size()))
        return;

    if (index == 0) {
        currentProgram_.store(0);
        loadDiagnosticKit();
        syncPresetParameter(0);
        hostDisplayDirty_.store(true);
        return;
    }

    const auto sfz = factorykits::resolveKit(index, SoundsPanel::samplesRoot());
    if (!sfz.existsAsFile()) {
        {
            const juce::ScopedLock sl(loadLock_);
            loadStatus_ = juce::String(table[size_t(index)].name) + " not installed";
        }
        // Say so. A silent no-op here is exactly how this class of bug hides.
        logLine("SappKit-kit: MISSING program=" + juce::String(index) + " name=\""
                + juce::String(table[size_t(index)].name) + "\" build=" SAPPKIT_VERSION);
        instrumentChangedFlag_.store(true);
        return;
    }
    currentProgram_.store(index);
    if (sfz.getFullPathName() != currentInstrumentPath())
        loadSfzInstrument(sfz);
    // Keep the `preset` chooser following the kit however the kit was picked
    // (MIDI program change, host program API, the chooser itself). Loading a
    // kit does NOT reset parameters to defaults here — applySavedMixOrDefaults
    // touches only the 10 kit-bus ids and the pad ids — so unlike sappsynth
    // there is no "reset everything" loop that could clobber the chooser.
    syncPresetParameter(index);
    hostDisplayDirty_.store(true);
}

// ------------------------------------------------------------ user presets --

int SappKitProcessor::factoryPresetCount() const
{
    return int(factorykits::all().size());
}

std::vector<sapp::userpresets::UserPreset> SappKitProcessor::userPresets() const
{
    return sapp::userpresets::scan(kInstrument);
}

juce::String SappKitProcessor::presetStatus() const
{
    const juce::ScopedLock sl(loadLock_);
    return presetStatus_;
}

void SappKitProcessor::setPresetStatus(const juce::String& message)
{
    const juce::ScopedLock sl(loadLock_);
    presetStatus_ = message;
}

bool SappKitProcessor::saveUserPreset(const juce::String& name, const juce::String& notes,
                                      juce::String& error)
{
    auto preset = sapp::userpresets::capture(*this, name.trim(), notes);
    // capture() is instrument-agnostic and knows nothing about samples, so the
    // kit hint is filled in here: a sappkit sound is the parameter state PLUS
    // the kit it was captured against (PRESETS.md section 1, optional `sfz`).
    // Empty path = the built-in diagnostic kit, i.e. no hint.
    {
        const juce::ScopedLock sl(loadLock_);
        preset.sfz = sfzPath_;
    }
    juce::File written;
    const bool ok = sapp::userpresets::save(preset, kInstrument, written, error);
    setPresetStatus(ok ? "saved \"" + preset.name + "\"" : "save failed: " + error);
    return ok;
}

bool SappKitProcessor::loadUserPreset(const juce::String& name, juce::String& error)
{
    const auto preset = sapp::userpresets::findByName(kInstrument, name);
    if (!preset.has_value()) {
        error = "no user preset named \"" + name + "\" in " +
                sapp::userpresets::presetDir(kInstrument).getFullPathName();
        setPresetStatus("no preset named \"" + name + "\"");
        return false;
    }

    juce::String note;
    if (preset->sfz.isNotEmpty()) {
        const juce::String current = currentInstrumentPath();
        const juce::File kit(preset->sfz);
        if (kit.existsAsFile()) {
            if (kit.getFullPathName() != current) {
                // The preset carries the whole mix, so the incoming kit must
                // NOT overwrite it from its own saved-mix file — exactly the
                // deal a host state restore gets.
                enqueueSfz(kit, true);
            }
        } else {
            // PRESETS.md: an `sfz` hint that does not resolve is ignored. The
            // parameters still apply, to whatever kit is loaded.
            note = " - kit missing, applied to current kit";
        }
    }

    applyUserPresetParams(*preset);
    setPresetStatus("loaded \"" + preset->name + "\"" + note);
    return true;
}

void SappKitProcessor::applyUserPresetParams(const sapp::userpresets::UserPreset& preset)
{
    // The mix machinery watches parameters and writes the CURRENT kit's mix
    // file ~2 s after they move. A preset load is not a mix edit, so arm the
    // same suppression a fresh kit load uses: the timer's next quiet tick
    // clears it, and only genuine user tweaks after that arm a save again.
    // lastBusValid_ = false likewise stops this jump counting as a bus edit.
    suppressMixSave_.store(true);
    mixSaveCountdown_.store(-1);
    lastBusValid_.store(false);
    sapp::userpresets::apply(preset, apvts_);
}

void SappKitProcessor::applyPresetChoice(int index)
{
    if (index < 0)
        return;
    if (index < factoryPresetCount()) {
        applyKitProgram(index);
        return;
    }
    // Beyond the factory bank the entry is a user preset: resolve the choice
    // label back to a name and load from disk, so the file stays the source of
    // truth even if it changed since this instance was constructed.
    auto* choice = dynamic_cast<juce::AudioParameterChoice*>(
        apvts_.getParameter(sapp::userpresets::kPresetParamId));
    if (choice == nullptr || index >= choice->choices.size())
        return;
    juce::String error;
    loadUserPreset(sapp::userpresets::nameFromChoiceLabel(choice->choices[index]), error);
    syncPresetParameter(index);
}

void SappKitProcessor::syncPresetParameter(int choiceIndex)
{
    auto* choice = dynamic_cast<juce::AudioParameterChoice*>(
        apvts_.getParameter(sapp::userpresets::kPresetParamId));
    if (choice == nullptr || choiceIndex < 0 || choiceIndex >= choice->choices.size())
        return;
    if (choice->getIndex() == choiceIndex)
        return;
    applyingPreset_.store(true, std::memory_order_release);
    choice->setValueNotifyingHost(choice->convertTo0to1(float(choiceIndex)));
    applyingPreset_.store(false, std::memory_order_release);
}

void SappKitProcessor::parameterChanged(const juce::String& parameterId, float newValue)
{
    if (parameterId != sapp::userpresets::kPresetParamId
        || applyingPreset_.load(std::memory_order_acquire))
        return;
    pendingPresetChoice_.store(int(newValue + 0.5f));
    // Not ready from the instant the host asks for a different kit — a host
    // that writes the parameter and immediately polls must not read the
    // PREVIOUS kit's "ready" and render too early.
    if (libraryReady_ != nullptr && libraryReady_->get())
        *libraryReady_ = false;
    queueCv_.notify_all();
}

// ----------------------------------------------------------- SappLink CC-in --

void SappKitProcessor::handleSappLinkCc(int ccNumber, int ccValue)
{
    const auto* mapping = sapplink::findMapping(ccNumber);
    if (mapping == nullptr)
        return;
    const auto index = size_t(mapping - sapplink::mappings().data());
    auto& slew = ccSlews_[index];
    if (slew.parameter == nullptr)
        return;
    slew.target = slew.parameter->convertTo0to1(sapplink::ccToEngineering(*mapping, ccValue));
    if (!slew.active)
        slew.current = slew.parameter->getValue();
    slew.active = true;
}

void SappKitProcessor::advanceCcSlews(int numSamples)
{
    // ~15 ms approach per step, applied through the same normalized-value
    // path host automation uses — never straight into the DSP.
    const float coefficient =
        1.0f - std::exp(-float(numSamples) / (0.015f * float(getSampleRate() > 0 ? getSampleRate() : 48000.0)));
    for (auto& slew : ccSlews_) {
        if (!slew.active || slew.parameter == nullptr)
            continue;
        slew.current += (slew.target - slew.current) * coefficient;
        if (std::abs(slew.target - slew.current) < 1.0e-4f) {
            slew.current = slew.target;
            slew.active = false;
        }
        slew.parameter->setValueNotifyingHost(slew.current);
    }
}

// ------------------------------------------------------------------- audio ---

void SappKitProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine_.prepare(sampleRate, juce::jmax(64, samplesPerBlock));
    pushParamsToEngine();
}

void SappKitProcessor::releaseResources() {}

bool SappKitProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SappKitProcessor::pushParamsToEngine()
{
    KitParams p;
    p.punch = pPunch_->load();
    p.squash = pSquash_->load();
    p.crush = pCrush_->load();
    p.roomLevel = pRoomLevel_->load();
    p.roomSize = pRoomSize_->load();
    p.width = pWidth_->load();
    p.humanize = pHumanize_->load();
    p.masterGainDb = pMaster_->load();
    p.limiter = pLimiter_->load() > 0.5f;
    p.quality = int(pQuality_->load());
    p.clean = pClean_->load();     // KitEngine scales humanize by (1 - clean)
    engine_.setParams(p);
}

void SappKitProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    advanceCcSlews(buffer.getNumSamples());
    pushParamsToEngine();

    keyboardState.processNextMidiBuffer(midi, 0, buffer.getNumSamples(), true);

    eventScratch_.clear();
    for (const auto metadata : midi) {
        const auto msg = metadata.getMessage();
        MidiEvent e;
        e.frame = uint32_t(juce::jmax(0, metadata.samplePosition));
        if (msg.isNoteOn()) {
            e.type = MidiEvent::Type::NoteOn;
            e.note = uint8_t(msg.getNoteNumber());
            e.value = uint8_t(msg.getVelocity());
        } else if (msg.isNoteOff()) {
            e.type = MidiEvent::Type::NoteOff;
            e.note = uint8_t(msg.getNoteNumber());
        } else if (msg.isController()) {
            e.type = MidiEvent::Type::Controller;
            e.note = uint8_t(msg.getControllerNumber());
            e.value = uint8_t(msg.getControllerValue());
            // SappLink CC-in (any channel): mapped CCs also steer parameters.
            // The event still reaches the engine below (SFZ CC conditions and
            // CC64 pedal semantics stay untouched).
            handleSappLinkCc(msg.getControllerNumber(), msg.getControllerValue());
        } else if (msg.isPitchWheel()) {
            e.type = MidiEvent::Type::PitchBend;
            e.bend14 = int16_t(msg.getPitchWheelValue() - 8192);
        } else if (msg.isAllNotesOff()) {
            e.type = MidiEvent::Type::AllNotesOff;
        } else if (msg.isAllSoundOff()) {
            e.type = MidiEvent::Type::AllSoundOff;
        } else if (msg.isProgramChange()) {
            // Factory kit select; the load runs from the timer (message
            // thread) — never from the audio thread.
            pendingProgram_.store(msg.getProgramChangeNumber());
            // Not ready from the instant the host asks (sappkeys #4).
            // publishReadiness() already accounts for the queued program, but
            // it only runs on the loader thread — a host that sends the
            // program change and polls in the same breath would read the
            // OUTGOING kit's "ready" in the gap. Clearing here, on the
            // calling thread, closes it. Writing a parameter from
            // processBlock is this processor's normal path already
            // (advanceCcSlews does it every block).
            if (libraryReady_ != nullptr && libraryReady_->get())
                *libraryReady_ = false;
            continue;
        } else {
            continue;
        }
        eventScratch_.push_back(e);
    }
    std::stable_sort(eventScratch_.begin(), eventScratch_.end(),
                     [](const MidiEvent& a, const MidiEvent& b) { return a.frame < b.frame; });

    buffer.clear();
    if (buffer.getNumChannels() >= 2) {
        engine_.process(eventScratch_.data(), int(eventScratch_.size()),
                        buffer.getWritePointer(0), buffer.getWritePointer(1),
                        buffer.getNumSamples());
    }

    // Silence → voices: flag it so the loader thread names what just sounded.
    const int voices = engine_.sampler().activeVoiceCount();
    if (voices > 0 && lastVoiceCount_ == 0)
        audioBatchStarted_.store(true, std::memory_order_relaxed);
    lastVoiceCount_ = voices;

    midi.clear();
}

// ---------------------------------------------------------- pad overrides ---

PadOverrides SappKitProcessor::readPadOverrides() const
{
    PadOverrides overrides{};
    for (int pad = 0; pad < kNumPads; ++pad) {
        auto& o = overrides[size_t(pad)];
        o.tuneSemis = padParams_[size_t(pad)][0]->load();
        o.decay = padParams_[size_t(pad)][1]->load();
        o.pan = padParams_[size_t(pad)][2]->load();
        o.levelDb = padParams_[size_t(pad)][3]->load();
    }
    return overrides;
}

void SappKitProcessor::timerCallback()
{
    // EDITOR HOOK ONLY. Loading and the pad rebuild run on the loader thread
    // (see the threading note in the header) — a host with no message loop
    // never gets here, and must not need to.
    if (hostDisplayDirty_.exchange(false))
        updateHostDisplay(ChangeDetails{}.withProgramChanged(true));
    if (instrumentChangedFlag_.exchange(false) && onInstrumentChanged)
        onInstrumentChanged();
}

// The loader thread: the one place a kit is installed, and the home of the
// 8 Hz maintenance tick. Runs whether or not the host has a message loop,
// which is the entire point (issue #1).
void SappKitProcessor::loaderLoop()
{
    while (!loaderStop_.load(std::memory_order_acquire)) {
        // MIDI / host program change first, then an explicit parameter move:
        // when both land in the same pass the parameter (the deliberate host
        // move) wins because it is enqueued last.
        const int program = pendingProgram_.exchange(-1);
        if (program >= 0)
            applyKitProgram(program);
        const int choice = pendingPresetChoice_.exchange(-1);
        if (choice >= 0)
            applyPresetChoice(choice);

        LoadJob job;
        bool haveJob = false;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (!loadQueue_.empty()) {
                job = std::move(loadQueue_.front());
                loadQueue_.pop_front();
                haveJob = true;
            }
        }
        if (haveJob) {
            performLoad(std::move(job));
            jobsOutstanding_.fetch_sub(1);
            loading_.store(jobsOutstanding_.load() > 0);
            publishReadiness();
            continue;
        }

        loading_.store(false);
        publishReadiness();
        logAudioSourceIfNeeded();
        maintenanceTick();

        std::unique_lock<std::mutex> lock(queueMutex_);
        queueCv_.wait_for(lock, std::chrono::milliseconds(5));
    }
}

void SappKitProcessor::enqueueLoad(LoadJob job)
{
    jobsOutstanding_.fetch_add(1);
    loading_.store(true);
    if (libraryReady_ != nullptr && libraryReady_->get())
        *libraryReady_ = false;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        loadQueue_.push_back(std::move(job));
    }
    queueCv_.notify_all();
}

void SappKitProcessor::performLoad(LoadJob job)
{
    if (job.generation != loadGeneration_.load())
        return;  // a newer load was queued before this one started

    if (job.kind == LoadJob::Kind::Diagnostic) {
        sapp::sounds::LoadResult result;
        result.instrument = makeDiagnosticKit();
        result.ok = result.instrument != nullptr;
        finishLoad(std::move(result), {}, job.generation, job.fromRestore);
        return;
    }

    auto result = sapp::kit::loadKitSfz(job.path.toStdString());
    finishLoad(std::move(result), job.path, job.generation, job.fromRestore);
}

void SappKitProcessor::publishReadiness()
{
    if (libraryReady_ == nullptr) return;
    const bool ready = jobsOutstanding_.load() == 0
                       && pendingPresetChoice_.load() < 0
                       && pendingProgram_.load() < 0
                       && installCount_.load() > 0;
    if (libraryReady_->get() != ready)
        *libraryReady_ = ready;
}

bool SappKitProcessor::libraryReady() const
{
    return libraryReady_ != nullptr && libraryReady_->get();
}

void SappKitProcessor::logInstalled(const juce::String& what, bool ok)
{
    logLine(juce::String("SappKit-kit: ") + (ok ? "loaded" : "FAILED")
            + " source=\"" + what + "\" build=" SAPPKIT_VERSION);
}

void SappKitProcessor::logAudioSourceIfNeeded()
{
    // Voices started from silence: name the kit that produced them. This is
    // the line that makes "the plugin is playing its built-in diagnostic kit"
    // — or nothing at all — visible in the wild instead of only audible.
    if (!audioBatchStarted_.exchange(false)) return;
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    if (nowMs - lastAudioSourceLogMs_ < 3000.0) return;
    lastAudioSourceLogMs_ = nowMs;

    juce::String source, name;
    int pads = 0;
    {
        const juce::ScopedLock sl(loadLock_);
        source = sfzPath_;
        name = instrumentName_;
        pads = model_.padCount;
    }
    if (source.isEmpty())
        // ASCII only: these lines end up in host logs with every encoding.
        source = "DIAGNOSTIC(no SFZ loaded - the built-in kit is sounding)";
    logLine("SappKit-audio-source: kit=\"" + source + "\" name=\"" + name
            + "\" pads=" + juce::String(pads) + " build=" SAPPKIT_VERSION
            + " ready=" + juce::String(libraryReady() ? 1 : 0));
}

// Debounce: pad knob turns change APVTS params; every tick we compare to the
// overrides last baked into the playing instrument and rebuild when they
// differ. Region rebuild + sample copy is cheap for kit-sized libraries but
// far too slow for per-sample audio, so it belongs here and not in
// processBlock. Loader thread, ~8 Hz.
void SappKitProcessor::maintenanceTick()
{
    const uint32_t nowMs = juce::Time::getMillisecondCounter();
    if (nowMs - lastMaintenanceMs_ < 125) return;
    lastMaintenanceMs_ = nowMs;

    if (baseInstrument_ == nullptr)
        return;
    const PadOverrides wanted = readPadOverrides();
    bool same = true;
    for (int pad = 0; pad < kNumPads && same; ++pad) {
        const auto& a = wanted[size_t(pad)];
        const auto& b = appliedOverrides_[size_t(pad)];
        same = a.tuneSemis == b.tuneSemis && a.decay == b.decay &&
               a.pan == b.pan && a.levelDb == b.levelDb;
    }

    // Kit-bus changes count as mix edits too (no rebuild needed for them).
    const auto bus = readBusValues();
    const bool busSame = lastBusValid_.load() && bus == lastBus_;
    lastBus_ = bus;
    lastBusValid_.store(true);

    if (same && busSame) {
        // Quiet tick: the churn from a load/restore has settled; from here on
        // any parameter movement is the user (or the host) actually mixing.
        suppressMixSave_.store(false);
        const int countdown = mixSaveCountdown_.load();
        if (countdown > 0) {
            mixSaveCountdown_.store(countdown - 1);
            if (countdown - 1 == 0) {
                mixSaveCountdown_.store(-1);
                saveMixNow();
            }
        }
        return;
    }

    if (!suppressMixSave_.load())
        mixSaveCountdown_.store(16);  // ~2 s after the last touch at 8 Hz

    if (!same) {
        appliedOverrides_ = wanted;
        sapp::kit::KitModel model;
        {
            const juce::ScopedLock sl(loadLock_);
            model = model_;
        }
        engine_.setInstrument(applyPadOverrides(baseInstrument_, model, appliedOverrides_));
        engine_.collectRetired();
    }
}

// ------------------------------------------------------------- instruments --

void SappKitProcessor::enqueueDiagnostic(bool fromRestore)
{
    LoadJob job;
    job.kind = LoadJob::Kind::Diagnostic;
    job.generation = ++loadGeneration_;
    job.fromRestore = fromRestore;
    {
        const juce::ScopedLock sl(loadLock_);
        loadStatus_ = "Generating diagnostic kit...";
    }
    enqueueLoad(std::move(job));
}

void SappKitProcessor::enqueueSfz(const juce::File& sfzFile, bool fromRestore)
{
    LoadJob job;
    job.kind = LoadJob::Kind::Sfz;
    job.path = sfzFile.getFullPathName();
    job.generation = ++loadGeneration_;
    job.fromRestore = fromRestore;
    {
        const juce::ScopedLock sl(loadLock_);
        loadStatus_ = "Loading " + sfzFile.getFileName() + "...";
    }
    enqueueLoad(std::move(job));
}

void SappKitProcessor::loadDiagnosticKit() { enqueueDiagnostic(false); }

void SappKitProcessor::loadSfzInstrument(const juce::File& sfzFile)
{
    enqueueSfz(sfzFile, false);
}

void SappKitProcessor::finishLoad(sapp::sounds::LoadResult result,
                                  const juce::String& path, uint64_t generation,
                                  bool fromRestore)
{
    if (generation != loadGeneration_.load()) return;  // superseded

    bool installed = false;
    {
        const juce::ScopedLock sl(loadLock_);
        if (!result.ok || result.instrument == nullptr) {
            loadStatus_ = "Load failed";
            for (const auto& d : result.diagnostics)
                if (d.severity == sapp::sounds::Severity::Error) {
                    loadStatus_ = "Load failed: " + juce::String(d.message);
                    break;
                }
        } else {
            baseInstrument_ = result.instrument;
            model_ = buildKitModel(result.instrument->definition);
            sfzPath_ = path;
            instrumentName_ = juce::String(result.instrument->definition.name);

            // Fresh load: the kit brings its own saved mix (or clean defaults)
            // so one kit's pad tweaks never bleed onto another. Host state
            // restores and user presets skip this — they already carry a mix.
            suppressMixSave_.store(true);
            mixSaveCountdown_.store(-1);
            lastBusValid_.store(false);
            if (!fromRestore) applySavedMixOrDefaults();

            appliedOverrides_ = readPadOverrides();
            engine_.setInstrument(applyPadOverrides(baseInstrument_, model_, appliedOverrides_));
            engine_.collectRetired();
            loadStatus_ = result.missingSamples.empty()
                              ? juce::String(model_.padCount) + " pads ready"
                              : juce::String(result.missingSamples.size()) + " samples missing";

            // Keep getCurrentProgram honest for kits reached via the sounds
            // browser or host state restore, not just via program change.
            const int program =
                factorykits::programForKitFile(sfzPath_, SoundsPanel::samplesRoot());
            if (program >= 0)
                currentProgram_.store(program);
            installed = true;
        }
    }
    if (installed)
        installCount_.fetch_add(1);
    logInstalled(path.isEmpty() ? juce::String("DIAGNOSTIC(built-in kit)") : path,
                 installed);
    instrumentChangedFlag_.store(true);
}

// ---------------------------------------------------------------- kit mix ---

juce::File SappKitProcessor::kitMixDir()
{
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    base = base.getChildFile("Application Support");  // userApplicationDataDirectory = ~/Library
#endif
    return base.getChildFile("Sapp").getChildFile("KitMixes");
}

juce::File SappKitProcessor::currentKitMixFile() const
{
    const juce::ScopedLock sl(loadLock_);   // recursive: finishLoad holds it
    return kitMixDir().getChildFile(
        juce::String(sapp::kit::kitMixFileName(sfzPath_.toStdString())));
}

void SappKitProcessor::setParamValue(const juce::String& paramId, float plainValue)
{
    if (auto* p = apvts_.getParameter(paramId)) {
        const float norm = p->convertTo0to1(plainValue);
        if (p->getValue() != norm) p->setValueNotifyingHost(norm);
    }
}

// The kit-bus parameter IDs stored in a mix file, in readBusValues() order.
// `clean` is appended LAST so mix files written before it existed still
// parse — a missing key just leaves the parameter alone (busValue -> null).
static const char* const kBusIds[11] = {
    "punch", "squash", "crush", "roomLevel", "roomSize",
    "width", "humanize", "masterGain", "limiter", "quality", "clean",
};

std::array<float, 11> SappKitProcessor::readBusValues() const
{
    return {pPunch_->load(), pSquash_->load(), pCrush_->load(),
            pRoomLevel_->load(), pRoomSize_->load(), pWidth_->load(),
            pHumanize_->load(), pMaster_->load(), pLimiter_->load(),
            pQuality_->load(), pClean_->load()};
}

void SappKitProcessor::applySavedMixOrDefaults()
{
    // Target = defaults, overlaid with the kit's saved mix when one exists.
    PadOverrides target{};
    sapp::kit::KitMix mix;
    bool haveMix = false;
    const auto file = currentKitMixFile();
    if (file.existsAsFile() &&
        sapp::kit::parseKitMix(file.loadFileAsString().toStdString(), mix)) {
        sapp::kit::applyMixToOverrides(mix, model_, target);
        haveMix = true;
    }

    for (int pad = 0; pad < kNumPads; ++pad) {
        const auto& t = target[size_t(pad)];
        const int n = pad + 1;
        setParamValue(padParamId(n, "Tune"), t.tuneSemis);
        setParamValue(padParamId(n, "Decay"), t.decay);
        setParamValue(padParamId(n, "Pan"), t.pan);
        setParamValue(padParamId(n, "Level"), t.levelDb);
    }
    if (haveMix)
        for (const auto& id : kBusIds)
            if (const double* v = mix.busValue(id))
                setParamValue(id, float(*v));
}

void SappKitProcessor::saveMixNow()
{
    // Loader thread. The kit-mix schema stays at the 10 historical bus ids —
    // `clean` is a host/station control, carried by host state and by user
    // presets, not by a per-kit mix file.
    sapp::kit::KitMix mix;
    {
        const juce::ScopedLock sl(loadLock_);
        mix = sapp::kit::captureMix(sfzPath_.toStdString(),
                                    instrumentName_.toStdString(),
                                    model_, appliedOverrides_);
    }
    const auto bus = readBusValues();
    for (size_t i = 0; i < bus.size(); ++i)
        mix.setBus(kBusIds[i], bus[i]);

    const auto file = currentKitMixFile();
    file.getParentDirectory().createDirectory();
    if (file.replaceWithText(juce::String(sapp::kit::serializeKitMix(mix)))) {
        const juce::ScopedLock sl(loadLock_);
        loadStatus_ = "mix saved";
    }
}

juce::String SappKitProcessor::currentInstrumentName() const
{
    const juce::ScopedLock sl(loadLock_);
    return instrumentName_;
}

juce::String SappKitProcessor::currentInstrumentPath() const
{
    const juce::ScopedLock sl(loadLock_);
    return sfzPath_;
}

juce::String SappKitProcessor::loadStatus() const
{
    const juce::ScopedLock sl(loadLock_);
    return loadStatus_;
}

sapp::kit::KitModel SappKitProcessor::kitModel() const
{
    const juce::ScopedLock sl(loadLock_);
    return model_;
}

// -------------------------------------------------------------------- state --

void SappKitProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts_.copyState();
    state.setProperty("sfzPath", sfzPath_, nullptr);
    state.setProperty("stateVersion", 1, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void SappKitProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        auto state = juce::ValueTree::fromXml(*xml);
        if (!state.isValid()) return;
        apvts_.replaceState(state);
        const juce::String path = state.getProperty("sfzPath", "").toString();
        // Host state carries the mix; the kit must not overwrite it from its
        // own saved-mix file. The flag rides on the JOB, not on a member, so
        // a concurrent load can never steal or lose it.
        if (path.isNotEmpty() && juce::File(path).existsAsFile())
            enqueueSfz(juce::File(path), true);
        else
            enqueueDiagnostic(true);
    }
}

juce::AudioProcessorEditor* SappKitProcessor::createEditor()
{
    return new SappKitEditor(*this);
}

} // namespace sappkit

// JUCE plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new sappkit::SappKitProcessor();
}
