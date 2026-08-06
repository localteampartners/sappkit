#include "PluginProcessor.h"

#include "../core/DiagnosticKit.h"
#include "../core/SappLinkCCMap.h"
#include "PluginEditor.h"

namespace sappkit {

using namespace sapp::kit;
using sapp::sounds::MidiEvent;

namespace {
const char* kPadSuffixes[4] = {"Tune", "Decay", "Pan", "Level"};
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
    for (int pad = 0; pad < kNumPads; ++pad)
        for (int p = 0; p < 4; ++p)
            padParams_[size_t(pad)][size_t(p)] = raw(padParamId(pad + 1, kPadSuffixes[p]));

    eventScratch_.reserve(512);

    const auto& table = sapplink::mappings();
    static_assert(std::tuple_size<decltype(ccSlews_)>::value == size_t(sapplink::kNumMappings),
                  "ccSlews_ must match the SappLink mapping table size");
    for (size_t i = 0; i < table.size(); ++i)
        ccSlews_[i].parameter = apvts_.getParameter(table[i].paramId);

    loadDiagnosticKit();
    startTimerHz(8);   // debounced pad-override rebuild
}

SappKitProcessor::~SappKitProcessor() { stopTimer(); }

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
    // Debounce: pad knob turns change APVTS params; every tick we compare to
    // the overrides last baked into the playing instrument and rebuild off
    // the message thread when they differ. Region rebuild + sample copy is
    // cheap for kit-sized libraries but far too slow for per-sample audio.
    if (loading_.load() || rebuildInFlight_.load() || baseInstrument_ == nullptr)
        return;
    const PadOverrides wanted = readPadOverrides();
    bool same = true;
    for (int pad = 0; pad < kNumPads && same; ++pad) {
        const auto& a = wanted[size_t(pad)];
        const auto& b = appliedOverrides_[size_t(pad)];
        same = a.tuneSemis == b.tuneSemis && a.decay == b.decay &&
               a.pan == b.pan && a.levelDb == b.levelDb;
    }
    if (same)
        return;
    rebuildInFlight_ = true;
    appliedOverrides_ = wanted;
    rebuildOverriddenInstrument();
}

void SappKitProcessor::rebuildOverriddenInstrument()
{
    const auto base = baseInstrument_;
    const auto model = model_;
    const auto overrides = appliedOverrides_;
    const uint64_t generation = loadGeneration_.load();
    std::thread([this, base, model, overrides, generation] {
        auto rebuilt = applyPadOverrides(base, model, overrides);
        juce::MessageManager::callAsync([this, rebuilt = std::move(rebuilt), generation] {
            if (generation == loadGeneration_.load()) {
                engine_.setInstrument(rebuilt);
                engine_.collectRetired();
            }
            rebuildInFlight_ = false;
        });
    }).detach();
}

// ------------------------------------------------------------- instruments --

void SappKitProcessor::loadDiagnosticKit()
{
    const uint64_t generation = ++loadGeneration_;
    loading_ = true;
    {
        const juce::ScopedLock sl(loadLock_);
        loadStatus_ = "Generating diagnostic kit...";
    }
    std::thread([this, generation] {
        auto inst = makeDiagnosticKit();
        sapp::sounds::LoadResult result;
        result.instrument = inst;
        result.ok = true;
        juce::MessageManager::callAsync([this, result = std::move(result), generation]() mutable {
            finishLoad(std::move(result), {}, generation);
        });
    }).detach();
}

void SappKitProcessor::loadSfzInstrument(const juce::File& sfzFile)
{
    const uint64_t generation = ++loadGeneration_;
    loading_ = true;
    {
        const juce::ScopedLock sl(loadLock_);
        loadStatus_ = "Loading " + sfzFile.getFileName() + "...";
    }
    const juce::String path = sfzFile.getFullPathName();
    std::thread([this, path, generation] {
        sapp::sounds::InstrumentLoader loader;
        auto result = loader.loadSfz(path.toStdString());
        juce::MessageManager::callAsync([this, result = std::move(result), path, generation]() mutable {
            finishLoad(std::move(result), path, generation);
        });
    }).detach();
}

void SappKitProcessor::finishLoad(sapp::sounds::LoadResult result,
                                  const juce::String& path, uint64_t generation)
{
    if (generation != loadGeneration_.load()) return;  // superseded
    loading_ = false;

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
        appliedOverrides_ = readPadOverrides();
        engine_.setInstrument(applyPadOverrides(baseInstrument_, model_, appliedOverrides_));
        engine_.collectRetired();
        sfzPath_ = path;
        instrumentName_ = juce::String(result.instrument->definition.name);
        loadStatus_ = result.missingSamples.empty()
                          ? juce::String(model_.padCount) + " pads ready"
                          : juce::String(result.missingSamples.size()) + " samples missing";
    }
    if (onInstrumentChanged) onInstrumentChanged();
}

juce::String SappKitProcessor::currentInstrumentName() const
{
    const juce::ScopedLock sl(loadLock_);
    return instrumentName_;
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
        if (path.isNotEmpty() && juce::File(path).existsAsFile())
            loadSfzInstrument(juce::File(path));
        else
            loadDiagnosticKit();
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
