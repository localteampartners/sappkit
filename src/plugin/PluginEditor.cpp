#include "PluginEditor.h"

#include "SoundsPanel.h"

namespace sappkit {

namespace {

juce::String midiNoteName(int note)
{
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(names[((note % 12) + 12) % 12]) + juce::String(note / 12 - 1);
}

juce::Font titleFont(float height)
{
    return juce::Font(juce::FontOptions{"Futura", height, juce::Font::bold});
}
juce::Font uiFont(float height, bool bold = false)
{
    return juce::Font(juce::FontOptions{height, bold ? juce::Font::bold : juce::Font::plain});
}

// Choke groups get stable badge colors so related pads read as a family.
juce::Colour chokeColour(int group)
{
    switch (((group - 1) % 4 + 4) % 4) {
        case 0: return palette::amber;
        case 1: return palette::cyan;
        case 2: return palette::neonBright;
        default: return juce::Colour(0xff9dff57);
    }
}

} // namespace

// ------------------------------------------------------------ look and feel --

KitLookAndFeel::KitLookAndFeel()
{
    setColour(juce::Slider::textBoxTextColourId, palette::dim);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId, palette::text);
    setColour(juce::ComboBox::backgroundColourId, palette::panel);
    setColour(juce::ComboBox::textColourId, palette::text);
    setColour(juce::ComboBox::outlineColourId, palette::panelEdge);
    setColour(juce::ComboBox::arrowColourId, palette::neon);
    setColour(juce::PopupMenu::backgroundColourId, palette::panel);
    setColour(juce::PopupMenu::textColourId, palette::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, palette::neon.withAlpha(0.35f));
    setColour(juce::TextButton::buttonColourId, palette::panel);
    setColour(juce::TextButton::textColourOffId, palette::text);
    setColour(juce::TextButton::textColourOnId, palette::background);
    setColour(juce::ToggleButton::textColourId, palette::dim);
    setColour(juce::ToggleButton::tickColourId, palette::neon);
    setColour(juce::ToggleButton::tickDisabledColourId, palette::panelEdge);
}

void KitLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                      float sliderPos, float startAngle,
                                      float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).reduced(4.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float arcThickness = juce::jmax(2.2f, radius * 0.09f);
    const float arcRadius = radius - arcThickness * 0.5f;

    // Body: matte cap with a subtle top light.
    const float capRadius = arcRadius - arcThickness * 1.6f;
    juce::ColourGradient bodyGrad(palette::panelEdge.brighter(0.15f),
                                  centre.x - capRadius * 0.35f, centre.y - capRadius * 0.55f,
                                  palette::shadow, centre.x, centre.y + capRadius, true);
    g.setGradientFill(bodyGrad);
    g.fillEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2, capRadius * 2);
    g.setColour(palette::shadow.withAlpha(0.7f));
    g.drawEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2, capRadius * 2, 1.0f);

    // Track arc.
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                        startAngle, endAngle, true);
    g.setColour(palette::shadow);
    g.strokePath(track, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // Value arc with a neon glow.
    juce::Path value;
    value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                        startAngle, angle, true);
    g.setColour(palette::neon.withAlpha(0.28f));
    g.strokePath(value, juce::PathStrokeType(arcThickness * 2.3f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    g.setColour(slider.isEnabled() ? palette::neon : palette::dim);
    g.strokePath(value, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // Pointer.
    juce::Path pointer;
    pointer.addRoundedRectangle(-arcThickness * 0.5f, -capRadius + arcThickness,
                                arcThickness, capRadius * 0.45f, arcThickness * 0.4f);
    g.setColour(palette::text);
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
}

void KitLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                  int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float>(0, 0, float(width), float(height)).reduced(0.5f);
    g.setColour(palette::panel);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(box.hasKeyboardFocus(true) ? palette::neon : palette::panelEdge);
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    juce::Path arrow;
    const float ax = float(width) - 14.0f, ay = float(height) * 0.5f;
    arrow.addTriangle(ax - 4, ay - 2.5f, ax + 4, ay - 2.5f, ax, ay + 3.5f);
    g.setColour(palette::neon);
    g.fillPath(arrow);
}

void KitLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                          const juce::Colour&, bool highlighted,
                                          bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = button.getToggleState();
    juce::Colour fill = on ? palette::neon : palette::panel;
    if (down) fill = fill.darker(0.2f);
    else if (highlighted) fill = fill.brighter(0.08f);
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(on ? palette::neonBright : palette::panelEdge);
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
}

juce::Font KitLookAndFeel::getComboBoxFont(juce::ComboBox&) { return uiFont(13.0f); }
juce::Font KitLookAndFeel::getPopupMenuFont() { return uiFont(13.5f); }

// -------------------------------------------------------------------- knob ---

Knob::Knob(const juce::String& title, bool big) : big_(big)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setPopupDisplayEnabled(true, true, nullptr);
    addAndMakeVisible(slider);

    label_.setText(title, juce::dontSendNotification);
    label_.setJustificationType(juce::Justification::centred);
    label_.setFont(uiFont(big ? 12.5f : 10.5f, big));
    label_.setColour(juce::Label::textColourId, big ? palette::text : palette::dim);
    label_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(label_);
}

void Knob::setTitle(const juce::String& title)
{
    label_.setText(title, juce::dontSendNotification);
}

void Knob::resized()
{
    auto bounds = getLocalBounds();
    label_.setBounds(bounds.removeFromBottom(big_ ? 17 : 13));
    slider.setBounds(bounds);
}

// --------------------------------------------------------------------- pad ---

void PadComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.5f);
    const bool empty = info.note < 0;

    // Base plate.
    juce::Colour base = empty ? palette::background.brighter(0.02f)
                              : palette::panel.brighter(selected ? 0.10f : 0.03f);
    g.setColour(base);
    g.fillRoundedRectangle(bounds, 7.0f);

    // Hit flash: neon bloom from the pad face.
    if (flash > 0.01f) {
        g.setColour(palette::neon.withAlpha(0.42f * flash));
        g.fillRoundedRectangle(bounds, 7.0f);
        g.setColour(palette::neonBright.withAlpha(0.5f * flash));
        g.drawRoundedRectangle(bounds.reduced(1.0f), 6.0f, 2.0f);
    }

    // Rim: neon when selected, subtle otherwise.
    g.setColour(selected ? palette::neon : palette::panelEdge);
    g.drawRoundedRectangle(bounds, 7.0f, selected ? 1.6f : 1.0f);

    if (empty) return;

    auto inner = bounds.reduced(7.0f, 6.0f);

    // Pad number (top left) + note name (top right).
    g.setFont(uiFont(9.5f, true));
    g.setColour(palette::dim);
    g.drawText(juce::String(padNumber),
               inner.removeFromTop(12.0f).toNearestInt(), juce::Justification::topLeft);
    g.setFont(uiFont(9.5f));
    g.drawText(midiNoteName(info.note), bounds.reduced(7.0f, 6.0f).toNearestInt(),
               juce::Justification::topRight);

    // Sound name, centred.
    g.setColour(selected ? palette::text : palette::text.withAlpha(0.85f));
    g.setFont(uiFont(bounds.getWidth() > 86.0f ? 13.0f : 11.5f, true));
    g.drawFittedText(info.name, bounds.reduced(6.0f, 16.0f).toNearestInt(),
                     juce::Justification::centred, 2);

    // Footer: choke badge + layers/RR hint.
    auto footer = bounds.reduced(7.0f, 6.0f);
    footer = footer.removeFromBottom(12.0f);
    if (info.chokeGroup > 0) {
        const auto badge = footer.removeFromLeft(30.0f);
        g.setColour(chokeColour(info.chokeGroup));
        g.fillEllipse(badge.getX(), badge.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setFont(uiFont(8.5f, true));
        g.drawText("CH" + juce::String(info.chokeGroup),
                   badge.withTrimmedLeft(8.0f).toNearestInt(),
                   juce::Justification::centredLeft);
    }
    juce::String hint;
    if (info.velocityLayers > 1) hint << info.velocityLayers << "V";
    if (info.roundRobins > 1) hint << (hint.isEmpty() ? "" : " ") << info.roundRobins << "RR";
    if (hint.isNotEmpty()) {
        g.setColour(palette::dim);
        g.setFont(uiFont(8.5f));
        g.drawText(hint, footer.toNearestInt(), juce::Justification::centredRight);
    }
}

void PadComponent::mouseDown(const juce::MouseEvent&)
{
    if (info.note >= 0 && onPress) onPress();
}

// ------------------------------------------------------------------- editor --

SappKitEditor::SappKitEditor(SappKitProcessor& processor)
    : juce::AudioProcessorEditor(&processor), processor_(processor)
{
    setLookAndFeel(&lookAndFeel_);

    auto& state = processor_.valueTree();

    title_.setText("SappKit", juce::dontSendNotification);
    title_.setFont(titleFont(30.0f));
    title_.setColour(juce::Label::textColourId, palette::neon);
    addAndMakeVisible(title_);

    subtitle_.setText("DRUMS & PERCUSSION", juce::dontSendNotification);
    subtitle_.setFont(uiFont(10.0f));
    subtitle_.setColour(juce::Label::textColourId, palette::dim);
    addAndMakeVisible(subtitle_);

    instrumentName_.setFont(uiFont(15.0f, true));
    instrumentName_.setColour(juce::Label::textColourId, palette::text);
    instrumentName_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(instrumentName_);

    status_.setFont(uiFont(11.0f));
    status_.setColour(juce::Label::textColourId, palette::dim);
    status_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(status_);

    loadButton_.onClick = [this] { chooseSfz(); };
    addAndMakeVisible(loadButton_);
    diagButton_.onClick = [this] { processor_.loadDiagnosticKit(); };
    addAndMakeVisible(diagButton_);
    soundsButton_.onClick = [this] { openSoundsPanel(); };
    addAndMakeVisible(soundsButton_);

    auto header = [&](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(uiFont(10.5f, true));
        label.setColour(juce::Label::textColourId, palette::dim);
        addAndMakeVisible(label);
    };
    header(padsHeader_, "PADS");
    header(busHeader_, "KIT BUS");
    header(editHeader_, "PAD EDIT");

    for (int i = 0; i < sapp::kit::kNumPads; ++i) {
        auto& pad = pads_[size_t(i)];
        pad.onPress = [this, i] {
            selectPad(i);
            const int note = pads_[size_t(i)].info.note;
            if (note >= 0) {
                processor_.keyboardState.noteOn(1, note, 0.85f);
                juce::Timer::callAfterDelay(120, [this, note] {
                    processor_.keyboardState.noteOff(1, note, 0.0f);
                });
            }
        };
        addAndMakeVisible(pad);
    }

    auto kitKnob = [&](const juce::String& id, const juce::String& text, bool big = false) {
        auto k = std::make_unique<Knob>(text, big);
        kitAttachments_.push_back(
            std::make_unique<SliderAttachment>(state, id, k->slider));
        addAndMakeVisible(*k);
        return k;
    };
    punch_ = kitKnob("punch", "PUNCH", true);
    squash_ = kitKnob("squash", "SQUASH", true);
    crush_ = kitKnob("crush", "CRUSH", true);
    roomLevel_ = kitKnob("roomLevel", "ROOM");
    roomSize_ = kitKnob("roomSize", "SIZE");
    width_ = kitKnob("width", "WIDTH");
    humanize_ = kitKnob("humanize", "HUMANIZE");
    master_ = kitKnob("masterGain", "MASTER");

    padTune_ = std::make_unique<Knob>("TUNE");
    padDecay_ = std::make_unique<Knob>("DECAY");
    padPan_ = std::make_unique<Knob>("PAN");
    padLevel_ = std::make_unique<Knob>("LEVEL");
    for (auto* k : {padTune_.get(), padDecay_.get(), padPan_.get(), padLevel_.get()})
        addAndMakeVisible(*k);

    quality_.addItemList({"Draft", "Normal"}, 1);
    qualityAttachment_ = std::make_unique<ComboAttachment>(state, "quality", quality_);
    addAndMakeVisible(quality_);

    limiterAttachment_ = std::make_unique<ButtonAttachment>(state, "limiter", limiter_);
    addAndMakeVisible(limiter_);

    voicesLabel_.setFont(uiFont(11.0f));
    voicesLabel_.setColour(juce::Label::textColourId, palette::dim);
    addAndMakeVisible(voicesLabel_);

    processor_.keyboardState.addListener(this);
    processor_.onInstrumentChanged = [this] { rebuildPads(); };
    rebuildPads();
    selectPad(0);

    startTimerHz(30);
    setResizable(true, true);
    setResizeLimits(820, 470, 1640, 940);
    getConstrainer()->setFixedAspectRatio(980.0 / 560.0);
    setSize(980, 560);
}

SappKitEditor::~SappKitEditor()
{
    processor_.keyboardState.removeListener(this);
    processor_.onInstrumentChanged = nullptr;
    setLookAndFeel(nullptr);
}

SoundsPanel& SappKitEditor::ensureSoundsPanel()
{
    if (soundsPanel_ == nullptr) {
        soundsPanel_ = std::make_unique<SoundsPanel>(
            processor_, [this] { soundsPanel_->setVisible(false); });
        addChildComponent(*soundsPanel_);
    }
    return *soundsPanel_;
}

void SappKitEditor::openSoundsPanel()
{
    auto& panel = ensureSoundsPanel();
    panel.setBounds(getLocalBounds().reduced(14));
    panel.setVisible(true);
    panel.toFront(true);
}

void SappKitEditor::chooseSfz()
{
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Load SFZ drum kit", juce::File(), "*.sfz");
    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser& chooser) {
                                  const auto file = chooser.getResult();
                                  if (file.existsAsFile())
                                      processor_.loadSfzInstrument(file);
                              });
}

void SappKitEditor::rebuildPads()
{
    const auto model = processor_.kitModel();
    for (int i = 0; i < sapp::kit::kNumPads; ++i) {
        auto& pad = pads_[size_t(i)];
        pad.padNumber = i + 1;
        pad.info = i < model.padCount ? model.pads[size_t(i)] : sapp::kit::PadInfo{};
        pad.repaint();
    }
    instrumentName_.setText(processor_.currentInstrumentName(), juce::dontSendNotification);
    if (selectedPad_ >= model.padCount && model.padCount > 0)
        selectPad(0);
    repaint();
}

void SappKitEditor::selectPad(int index)
{
    selectedPad_ = juce::jlimit(0, sapp::kit::kNumPads - 1, index);
    for (int i = 0; i < sapp::kit::kNumPads; ++i) {
        pads_[size_t(i)].selected = i == selectedPad_;
        pads_[size_t(i)].repaint();
    }

    auto& state = processor_.valueTree();
    const int padNumber = selectedPad_ + 1;
    padAttachments_[0] = std::make_unique<SliderAttachment>(
        state, SappKitProcessor::padParamId(padNumber, "Tune"), padTune_->slider);
    padAttachments_[1] = std::make_unique<SliderAttachment>(
        state, SappKitProcessor::padParamId(padNumber, "Decay"), padDecay_->slider);
    padAttachments_[2] = std::make_unique<SliderAttachment>(
        state, SappKitProcessor::padParamId(padNumber, "Pan"), padPan_->slider);
    padAttachments_[3] = std::make_unique<SliderAttachment>(
        state, SappKitProcessor::padParamId(padNumber, "Level"), padLevel_->slider);

    const auto& info = pads_[size_t(selectedPad_)].info;
    editHeader_.setText(info.note >= 0
                            ? "PAD EDIT — " + juce::String(padNumber) + "  " +
                                  info.name.c_str() + "  (" + midiNoteName(info.note) + ")"
                            : juce::String("PAD EDIT"),
                        juce::dontSendNotification);
}

void SappKitEditor::handleNoteOn(juce::MidiKeyboardState*, int, int note, float)
{
    if (note >= 0 && note < 128)
        noteHit_[size_t(note)].store(true, std::memory_order_relaxed);
}

void SappKitEditor::handleNoteOff(juce::MidiKeyboardState*, int, int, float) {}

void SappKitEditor::timerCallback()
{
    status_.setText(processor_.loadStatus(), juce::dontSendNotification);
    instrumentName_.setText(processor_.currentInstrumentName(), juce::dontSendNotification);

    // Flash pads whose notes fired since the last tick; decay the rest.
    for (int i = 0; i < sapp::kit::kNumPads; ++i) {
        auto& pad = pads_[size_t(i)];
        if (pad.info.note >= 0 &&
            noteHit_[size_t(pad.info.note)].exchange(false, std::memory_order_relaxed))
            pad.flash = 1.0f;
        else if (pad.flash > 0.01f)
            pad.flash *= 0.82f;
        else {
            if (pad.flash != 0.0f) { pad.flash = 0.0f; pad.repaint(); }
            continue;
        }
        pad.repaint();
    }

    sapp::sounds::DiagnosticSnapshot snap;
    if (processor_.engine().sampler().diagnostics().read(snap)) {
        voicesLabel_.setText(juce::String(snap.activeVoices) + " voices",
                             juce::dontSendNotification);
        meterL_ = juce::jmax(snap.lastPeakL, meterL_ * 0.86f);
        meterR_ = juce::jmax(snap.lastPeakR, meterR_ * 0.86f);
    }
    repaint(meterArea_);
}

void SappKitEditor::paint(juce::Graphics& g)
{
    // Near-black with a faint neon wash rising from the pads.
    juce::ColourGradient grad(palette::background.brighter(0.045f), 0.0f, 0.0f,
                              palette::background.darker(0.3f), 0.0f, float(getHeight()),
                              false);
    g.setGradientFill(grad);
    g.fillAll();

    const float scale = float(getWidth()) / 980.0f;
    auto s = [scale](int v) { return int(float(v) * scale); };

    g.setColour(palette::neon.withAlpha(0.05f));
    g.fillEllipse(float(s(40)), float(s(140)), float(s(400)), float(s(380)));

    auto panel = [&](juce::Rectangle<int> r) {
        g.setColour(palette::panel.withAlpha(0.6f));
        g.fillRoundedRectangle(r.toFloat(), 8.0f);
        g.setColour(palette::panelEdge);
        g.drawRoundedRectangle(r.toFloat(), 8.0f, 1.0f);
    };
    panel({s(14), s(74), s(420), s(430)});    // pads
    panel({s(448), s(74), s(518), s(240)});   // kit bus
    panel({s(448), s(326), s(518), s(178)});  // pad edit

    // Neon hairline under the header.
    g.setColour(palette::neon.withAlpha(0.4f));
    g.fillRect(s(14), s(64), getWidth() - s(28), 1);

    // Peak meter.
    if (!meterArea_.isEmpty()) {
        g.setColour(palette::shadow);
        g.fillRoundedRectangle(meterArea_.toFloat(), 3.0f);
        auto bar = [&](float level, juce::Rectangle<int> r) {
            const float db = juce::Decibels::gainToDecibels(level, -60.0f);
            const float norm = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
            auto fill = r.toFloat();
            fill = fill.removeFromLeft(fill.getWidth() * norm);
            g.setColour(db > -3.0f ? palette::neonBright : palette::cyan);
            g.fillRoundedRectangle(fill, 2.0f);
        };
        auto inner = meterArea_.reduced(2);
        bar(meterL_, inner.removeFromTop(inner.getHeight() / 2).reduced(0, 1));
        bar(meterR_, inner.reduced(0, 1));
    }
}

void SappKitEditor::resized()
{
    const float scale = float(getWidth()) / 980.0f;
    auto s = [scale](int v) { return int(float(v) * scale); };

    title_.setBounds(s(18), s(8), s(220), s(34));
    subtitle_.setBounds(s(21), s(40), s(220), s(16));
    loadButton_.setBounds(s(250), s(18), s(92), s(28));
    diagButton_.setBounds(s(348), s(18), s(104), s(28));
    soundsButton_.setBounds(s(458), s(18), s(104), s(28));
    instrumentName_.setBounds(getWidth() - s(380), s(10), s(364), s(24));
    status_.setBounds(getWidth() - s(380), s(34), s(364), s(18));
    if (soundsPanel_ != nullptr)
        soundsPanel_->setBounds(getLocalBounds().reduced(14));

    // Pad grid: MPC orientation — pad 1 bottom-left, pad 16 top-right.
    padsHeader_.setBounds(s(26), s(82), s(120), s(16));
    {
        const int gx = s(24), gy = s(102), cell = s(98), gap = s(4);
        for (int i = 0; i < sapp::kit::kNumPads; ++i) {
            const int row = 3 - i / 4;   // bottom row first
            const int col = i % 4;
            pads_[size_t(i)].setBounds(gx + col * (cell + gap), gy + row * (cell + gap),
                                       cell, cell);
        }
    }

    // Kit bus panel.
    busHeader_.setBounds(s(460), s(82), s(200), s(16));
    punch_->setBounds(s(458), s(102), s(112), s(126));
    squash_->setBounds(s(578), s(102), s(112), s(126));
    crush_->setBounds(s(698), s(102), s(112), s(126));
    master_->setBounds(s(830), s(104), s(120), s(122));
    const int smallW = s(76), smallH = s(80);
    roomLevel_->setBounds(s(462), s(230), smallW, smallH);
    roomSize_->setBounds(s(544), s(230), smallW, smallH);
    width_->setBounds(s(626), s(230), smallW, smallH);
    humanize_->setBounds(s(708), s(230), smallW, smallH);
    limiter_.setBounds(s(806), s(244), s(80), s(24));
    quality_.setBounds(s(884), s(244), s(78), s(24));

    // Pad edit strip.
    editHeader_.setBounds(s(460), s(334), s(490), s(16));
    const int knobW = s(104), knobH = s(112);
    padTune_->setBounds(s(470), s(356), knobW, knobH);
    padDecay_->setBounds(s(590), s(356), knobW, knobH);
    padPan_->setBounds(s(710), s(356), knobW, knobH);
    padLevel_->setBounds(s(830), s(356), knobW, knobH);

    // Footer strip.
    voicesLabel_.setBounds(s(16), s(514), s(90), s(20));
    meterArea_ = {s(110), s(518), s(170), s(14)};
}

} // namespace sappkit
