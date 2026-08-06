#pragma once
// SappKit editor — "dark club".
// Near-black blue, hot-pink neon accents, cyan secondary, vector-drawn
// controls. 4×4 pad grid with choke badges, per-pad edit strip, kit bus
// knobs, level meters.

#include <array>
#include <atomic>

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

namespace sappkit {

// ------------------------------------------------------------------ palette --
namespace palette {
const juce::Colour background{0xff0b0b10};   // near-black blue
const juce::Colour panel{0xff14141b};
const juce::Colour panelEdge{0xff232330};
const juce::Colour neon{0xffff3d7f};         // hot pink
const juce::Colour neonBright{0xffff7aa8};
const juce::Colour cyan{0xff37e0ff};
const juce::Colour amber{0xffffc14d};
const juce::Colour text{0xffe9e9f2};
const juce::Colour dim{0xff767688};
const juce::Colour shadow{0xff050508};
} // namespace palette

// ------------------------------------------------------------ look and feel --
class KitLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KitLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float sliderPos, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&,
                              const juce::Colour& backgroundColour,
                              bool highlighted, bool down) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};

// ------------------------------------------------------------- labeled knob --
class Knob : public juce::Component
{
public:
    Knob(const juce::String& title, bool big = false);
    void resized() override;
    void setTitle(const juce::String& title);
    juce::Slider slider;

private:
    juce::Label label_;
    bool big_;
};

// ------------------------------------------------------------------- pad -----
// One cell of the 4×4 grid: sound name, note, choke badge, hit flash,
// selection ring. Click = select + audition.
class PadComponent : public juce::Component
{
public:
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    std::function<void()> onPress;
    sapp::kit::PadInfo info;     // info.note < 0 → empty pad
    int padNumber = 0;           // 1-based display number
    bool selected = false;
    float flash = 0.0f;          // 0..1, decayed by the editor timer
};

// ------------------------------------------------------------------- editor --
class SappKitEditor : public juce::AudioProcessorEditor,
                      private juce::Timer,
                      private juce::MidiKeyboardState::Listener
{
public:
    explicit SappKitEditor(SappKitProcessor&);
    ~SappKitEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void handleNoteOn(juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff(juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void rebuildPads();
    void selectPad(int index);
    void chooseSfz();

    SappKitProcessor& processor_;
    KitLookAndFeel lookAndFeel_;

    juce::Label title_, subtitle_, instrumentName_, status_;
    juce::TextButton loadButton_{"LOAD SFZ"};
    juce::TextButton diagButton_{"BUILT-IN KIT"};

    juce::Label padsHeader_, busHeader_, editHeader_;

    std::array<PadComponent, sapp::kit::kNumPads> pads_;
    std::array<std::atomic<bool>, 128> noteHit_{};   // audio → timer handoff
    int selectedPad_ = 0;

    std::unique_ptr<Knob> punch_, squash_, crush_;
    std::unique_ptr<Knob> roomLevel_, roomSize_, width_, humanize_, master_;
    std::unique_ptr<Knob> padTune_, padDecay_, padPan_, padLevel_;
    juce::ComboBox quality_;
    juce::ToggleButton limiter_{"limiter"};

    juce::Label voicesLabel_;
    float meterL_ = 0.0f, meterR_ = 0.0f;
    juce::Rectangle<int> meterArea_;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::vector<std::unique_ptr<SliderAttachment>> kitAttachments_;
    std::array<std::unique_ptr<SliderAttachment>, 4> padAttachments_;
    std::unique_ptr<ComboAttachment> qualityAttachment_;
    std::unique_ptr<ButtonAttachment> limiterAttachment_;

    std::unique_ptr<juce::FileChooser> fileChooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SappKitEditor)
};

} // namespace sappkit
