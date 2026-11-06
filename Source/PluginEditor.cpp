#include "PluginEditor.h"

namespace
{
    constexpr double kDesignWidth = 1180.0;
    constexpr double kDesignHeight = 800.0;

    const float kLaneMin[] = { 5.0f, 0.0f, 0.0f, 0.25f, 0.0f, 0.0f, 0.0f, 1.0f,
                               1.0f, -2.0f, 0.0f, 0.0f, -12.0f, 30.0f, 0.0f, 0.0f };
    const float kLaneMax[] = { 500.0f, 1.0f, 24.0f, 4.0f, 1.0f, 1.0f, 1.0f, 24.0f,
                               32.0f, 2.0f, 50.0f, 1.0f, 12.0f, 18000.0f, 1.0f, 1.0f };
}

//==============================================================================
BtPOvenAudioProcessorEditor::BtPOvenAudioProcessorEditor (BtPOvenAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      ovenWindow (p),
      rackPage (p),
      aiPage (p),
      stemPage (p),
      oneShotPage (p),
      rvcPage (p)
{
    setLookAndFeel (&lnf);
    setOpaque (true);

    addAndMakeVisible (ovenWindow);
    addAndMakeVisible (rackPage);
    rackPage.setVisible (false);
    rackPage.onMainPage = [this]
    {
        if (rackPageVisible)
            toggleRackPage();
    };

    addAndMakeVisible (aiPage);
    aiPage.setVisible (false);
    aiPage.onMainPage = [this]
    {
        if (aiPageVisible)
            toggleAiPage();
    };

    addAndMakeVisible (stemPage);
    stemPage.setVisible (false);
    stemPage.onMainPage = [this]
    {
        if (stemPageVisible)
            toggleStemPage();
    };

    addAndMakeVisible (oneShotPage);
    oneShotPage.setVisible (false);
    oneShotPage.onMainPage = [this]
    {
        if (oneShotPageVisible)
            toggleOneShotPage();
    };

    addAndMakeVisible (rvcPage);
    rvcPage.setVisible (false);
    rvcPage.onMainPage = [this]
    {
        if (rvcPageVisible)
            toggleRvcPage();
    };

    //==========================================================================
    // Cook mode
    addKnob ("grain_size",    "SIZE",    { 636.0f, 112.0f, 124.0f, 80.0f });
    addKnob ("grain_spray",   "SPRAY",   { 764.0f, 112.0f, 124.0f, 80.0f });
    addKnob ("grain_pitch",   "PITCH",   { 892.0f, 112.0f, 124.0f, 80.0f });
    addKnob ("grain_stretch", "STRETCH", { 1020.0f, 112.0f, 124.0f, 80.0f });

    addKnob ("grain_reverse", "REVERSE", { 636.0f, 196.0f, 124.0f, 80.0f });
    addKnob ("grain_density", "DENSITY", { 764.0f, 196.0f, 124.0f, 80.0f });
    addKnob ("grain_burn",    "BURN",    { 892.0f, 196.0f, 124.0f, 80.0f });
    addKnob ("grain_width",   "WIDTH",   { 1020.0f, 196.0f, 124.0f, 80.0f });

    addParamButton ("cook_on", "COOK ON", { 1062.0f, 90.0f, 86.0f, 22.0f });

    //==========================================================================
    // Timing
    addKnob ("chop_depth",  "GATE CHOP",  { 636.0f, 326.0f, 100.0f, 80.0f });
    addKnob ("chop_rate",   "CHOP RATE",  { 740.0f, 326.0f, 100.0f, 80.0f });
    addKnob ("stutter_rate","STUT RATE",  { 844.0f, 326.0f, 100.0f, 80.0f });
    addKnob ("drip",        "DRIP",       { 948.0f, 326.0f, 100.0f, 80.0f });
    addKnob ("drip_rate",   "DRIP RATE",  { 1052.0f, 326.0f, 100.0f, 80.0f });

    addParamButton ("chop_triplet", "TRIPLET", { 1062.0f, 294.0f, 86.0f, 22.0f });

    addParamButton ("autochop", "AUTO CHOP", { 636.0f, 410.0f, 96.0f, 24.0f });
    addParamButton ("rearrange", "REARRANGE", { 832.0f, 410.0f, 96.0f, 24.0f });

    chopDivBox = std::make_unique<juce::ComboBox>();
    chopDivBox->addItemList ({ "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BAR", "4 BAR" }, 1);
    chopDivBox->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff101215));
    chopDivBox->setColour (juce::ComboBox::textColourId, OvenLookAndFeel::lime());
    chopDivBox->setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    chopDivBox->setColour (juce::ComboBox::arrowColourId, OvenLookAndFeel::lime());
    addAndMakeVisible (*chopDivBox);

    chopDivAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "chop_div", *chopDivBox);

    addButton ("ROLL", { 932.0f, 410.0f, 58.0f, 24.0f },
               [this]
               {
                   auto* seedParam = processor.apvts.getParameter ("shuffle_seed");
                   auto* rearrangeParam = processor.apvts.getParameter ("rearrange");

                   const int current = (int) std::lround (
                       processor.apvts.getRawParameterValue ("shuffle_seed")->load());
                   const int next = (current + 1
                                     + juce::Random::getSystemRandom().nextInt (17)) % 1000;

                   if (seedParam != nullptr)
                       seedParam->setValueNotifyingHost (seedParam->convertTo0to1 ((float) next));

                   if (rearrangeParam != nullptr)
                       rearrangeParam->setValueNotifyingHost (1.0f);

                   processor.setStatusMessage ("ROLLED SHUFFLE SEED  ->  " + juce::String (next));
                   repaint();
               },
               false);

    addKnob ("bpm", "BPM", { 1000.0f, 396.0f, 82.0f, 72.0f });

    //==========================================================================
    // Transport
    recA = addButton ("REC A", { 28.0f, 508.0f, 94.0f, 34.0f },
                      [this] { toggleRecord (0, false); }, false, true);
    recB = addButton ("REC B", { 128.0f, 508.0f, 94.0f, 34.0f },
                      [this] { toggleRecord (1, false); }, false, true);
    dubA = addButton ("DUB A", { 228.0f, 508.0f, 94.0f, 34.0f },
                      [this] { toggleRecord (0, true); }, false, true);
    dubB = addButton ("DUB B", { 328.0f, 508.0f, 94.0f, 34.0f },
                      [this] { toggleRecord (1, true); }, false, true);

    addParamButton ("playing", "PLAY", { 428.0f, 508.0f, 94.0f, 34.0f });

    addButton ("LOAD A",   { 28.0f, 548.0f, 78.0f, 30.0f }, [this] { loadLoopFile (0); }, false);
    addButton ("LOAD B",   { 111.0f, 548.0f, 78.0f, 30.0f }, [this] { loadLoopFile (1); }, false);
    addButton ("SAVE A",   { 194.0f, 548.0f, 78.0f, 30.0f }, [this] { exportLoopFile (0); }, false);
    addButton ("SAVE B",   { 277.0f, 548.0f, 78.0f, 30.0f }, [this] { exportLoopFile (1); }, false);
    addButton ("CLEAR A",  { 360.0f, 548.0f, 78.0f, 30.0f },
               [this] { processor.clearLoop (0); repaint(); }, false);
    addButton ("CLEAR B",  { 443.0f, 548.0f, 78.0f, 30.0f },
               [this] { processor.clearLoop (1); repaint(); }, false);

    addParamButton ("a_play",   "A PLAY",   { 28.0f, 578.0f, 118.0f, 24.0f });
    addParamButton ("a_bypass", "A BYPASS", { 151.0f, 578.0f, 118.0f, 24.0f });
    addParamButton ("b_play",   "B PLAY",   { 274.0f, 578.0f, 118.0f, 24.0f });
    addParamButton ("b_bypass", "B BYPASS", { 397.0f, 578.0f, 118.0f, 24.0f });

    addParamButton ("a_reverse", "REV A",  { 28.0f, 606.0f, 58.0f, 24.0f });
    addParamButton ("a_half",    "1/2 A",  { 90.0f, 606.0f, 58.0f, 24.0f });
    addParamButton ("a_double",  "2X A",   { 152.0f, 606.0f, 58.0f, 24.0f });
    addParamButton ("a_stutter", "STUT A", { 214.0f, 606.0f, 58.0f, 24.0f });
    addParamButton ("b_reverse", "REV B",  { 276.0f, 606.0f, 58.0f, 24.0f });
    addParamButton ("b_half",    "1/2 B",  { 338.0f, 606.0f, 58.0f, 24.0f });
    addParamButton ("b_double",  "2X B",   { 400.0f, 606.0f, 58.0f, 24.0f });
    addParamButton ("b_stutter", "STUT B", { 462.0f, 606.0f, 58.0f, 24.0f });

    //==========================================================================
    // Pitch + texture
    addKnob ("octave",      "OCTAVE",      { 556.0f, 508.0f, 66.0f, 78.0f });
    addKnob ("detune",      "DETUNE",      { 626.0f, 508.0f, 66.0f, 78.0f });
    addKnob ("detune_rate", "DET RATE",    { 696.0f, 508.0f, 66.0f, 78.0f });
    addKnob ("formant",     "FORMANT",     { 766.0f, 508.0f, 66.0f, 78.0f });

    addKnob ("wobble", "WOBBLE", { 556.0f, 590.0f, 66.0f, 78.0f });
    addKnob ("vinyl",  "VINYL",  { 626.0f, 590.0f, 66.0f, 78.0f });
    addKnob ("air",    "AIR",    { 696.0f, 590.0f, 66.0f, 78.0f });
    addKnob ("ghost",  "GHOST",  { 766.0f, 590.0f, 66.0f, 78.0f });

    //==========================================================================
    // Filters
    filterBox = std::make_unique<juce::ComboBox>();
    filterBox->addItemList ({ "OFF", "LOWPASS", "HIGHPASS", "BANDPASS", "NOTCH",
                              "PEAK", "COMB", "SMOKE", "BASEMENT" }, 1);
    filterBox->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff101215));
    filterBox->setColour (juce::ComboBox::textColourId, OvenLookAndFeel::lime());
    filterBox->setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    filterBox->setColour (juce::ComboBox::arrowColourId, OvenLookAndFeel::lime());
    addAndMakeVisible (*filterBox);

    filterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "filter_type", *filterBox);

    addKnob ("cutoff",     "CUTOFF", { 866.0f, 540.0f, 67.0f, 78.0f });
    addKnob ("reso",       "RESO",   { 937.0f, 540.0f, 67.0f, 78.0f });
    addKnob ("sweep",      "SWEEP",  { 1009.0f, 540.0f, 67.0f, 78.0f });
    addKnob ("sweep_rate", "SW RATE",{ 1080.0f, 540.0f, 67.0f, 78.0f });

    //==========================================================================
    // FX recorder
    fxArm = addButton ("ARM FX", { 30.0f, 712.0f, 124.0f, 48.0f },
                       [this]
                       {
                           if (processor.isFxRecording())
                               processor.stopFxRecording();
                           else
                               processor.startFxRecording();

                           repaint();
                       },
                       false, true);

    addParamButton ("fx_play", "PLAY", { 160.0f, 712.0f, 124.0f, 48.0f });
    addButton ("CLEAR", { 290.0f, 712.0f, 110.0f, 48.0f },
               [this] { processor.clearFx(); repaint(); }, false);

    addKnob ("fx_speed", "SPEED", { 412.0f, 700.0f, 80.0f, 70.0f });

    //==========================================================================
    // Header: loop select + I/O
    nukeButton = addButton ("NUKED", { 414.0f, 20.0f, 60.0f, 34.0f },
                            [this]
                            {
                                processor.nuke();
                                repaint();
                            },
                            false, true, true);

    rackButton = addButton ("RACK", { 478.0f, 20.0f, 56.0f, 34.0f },
                            [this] { toggleRackPage(); },
                            false, false, true);

    aiButton = addButton ("AI", { 538.0f, 20.0f, 50.0f, 34.0f },
                          [this] { toggleAiPage(); },
                          false, false, true);

    stemButton = addButton ("STEM", { 592.0f, 20.0f, 52.0f, 34.0f },
                            [this] { toggleStemPage(); },
                            false, false, true);

    oneShotButton = addButton ("SHOTS", { 648.0f, 20.0f, 54.0f, 34.0f },
                               [this] { toggleOneShotPage(); },
                               false, false, true);

    rvcButton = addButton ("VOICE", { 706.0f, 20.0f, 56.0f, 34.0f },
                           [this] { toggleRvcPage(); },
                           false, false, true);

    loopA = addButton ("A", { 768.0f, 22.0f, 30.0f, 30.0f },
                       [this]
                       {
                           processor.apvts.getParameter ("active_loop")->setValueNotifyingHost (0.0f);
                       },
                       true, false, true);
    loopA->setRadioGroupId (7001);

    loopB = addButton ("B", { 802.0f, 22.0f, 30.0f, 30.0f },
                       [this]
                       {
                           processor.apvts.getParameter ("active_loop")->setValueNotifyingHost (0.5f);
                       },
                       true, false, true);
    loopB->setRadioGroupId (7001);

    loopC = addButton ("C", { 836.0f, 22.0f, 30.0f, 30.0f },
                       [this]
                       {
                           processor.apvts.getParameter ("active_loop")->setValueNotifyingHost (1.0f);
                       },
                       true, false, true);
    loopC->setRadioGroupId (7001);

    addKnob ("in_gain",  "IN",  { 872.0f, 2.0f, 44.0f, 72.0f }, true);
    addKnob ("out_gain", "OUT", { 920.0f, 2.0f, 44.0f, 72.0f }, true);

    //==========================================================================
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (kDesignWidth / kDesignHeight);
    setResizeLimits (944, 624, 1770, 1170);
    setSize (1100, 746);

    startTimerHz (12);

    processor.setStatusMessage ("DROP A MELODY ON THE OVEN TO START COOKING");
}

BtPOvenAudioProcessorEditor::~BtPOvenAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
juce::Rectangle<int> BtPOvenAudioProcessorEditor::S (float x, float y, float w, float h) const
{
    return { juce::roundToInt (x * sc), juce::roundToInt (y * sc),
             juce::roundToInt (w * sc), juce::roundToInt (h * sc) };
}

//==============================================================================
void BtPOvenAudioProcessorEditor::addKnob (const juce::String& parameterId,
                                           const juce::String& name,
                                           juce::Rectangle<float> designArea,
                                           bool keepVisible)
{
    auto knob = std::make_unique<Knob>();
    knob->design = designArea;
    knob->keepVisible = keepVisible;

    knob->slider = std::make_unique<juce::Slider>();
    knob->slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob->slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 13);
    knob->slider->setColour (juce::Slider::textBoxTextColourId, OvenLookAndFeel::lime().withAlpha (0.85f));
    knob->slider->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    knob->slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob->slider->setMouseDragSensitivity (170);
    knob->slider->setTooltip (name);
    addAndMakeVisible (*knob->slider);

    knob->label = std::make_unique<juce::Label>();
    knob->label->setText (name, juce::dontSendNotification);
    knob->label->setJustificationType (juce::Justification::centred);
    knob->label->setColour (juce::Label::textColourId, OvenLookAndFeel::cream().withAlpha (0.72f));
    knob->label->setInterceptsMouseClicks (false, false);
    addAndMakeVisible (*knob->label);

    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, parameterId, *knob->slider);

    knobs.push_back (std::move (knob));
}

juce::TextButton* BtPOvenAudioProcessorEditor::addButton (const juce::String& text,
                                                          juce::Rectangle<float> designArea,
                                                          std::function<void()> onClick,
                                                          bool isToggle,
                                                          bool danger,
                                                          bool keepVisible)
{
    auto button = std::make_unique<juce::TextButton> (text);
    button->setClickingTogglesState (isToggle);
    button->setColour (juce::TextButton::buttonOnColourId, OvenLookAndFeel::lime());
    button->setColour (juce::TextButton::textColourOnId, OvenLookAndFeel::lime());
    button->setColour (juce::TextButton::textColourOffId, OvenLookAndFeel::cream());

    if (danger)
        button->getProperties().set ("danger", true);

    if (onClick != nullptr)
        button->onClick = std::move (onClick);

    addAndMakeVisible (*button);

    auto* raw = button.get();
    buttons.push_back ({ std::move (button), designArea, keepVisible });
    return raw;
}

juce::TextButton* BtPOvenAudioProcessorEditor::addParamButton (const juce::String& parameterId,
                                                               const juce::String& text,
                                                               juce::Rectangle<float> designArea,
                                                               bool danger)
{
    auto* button = addButton (text, designArea, nullptr, true, danger, false);

    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, parameterId, *button));

    return button;
}

//==============================================================================
void BtPOvenAudioProcessorEditor::toggleRecord (int loopIndex, bool overdub)
{
    if (processor.isLoopRecording (loopIndex))
        processor.stopRecord (loopIndex);
    else
        processor.beginRecord (loopIndex, overdub);

    repaint();
}

void BtPOvenAudioProcessorEditor::loadLoopFile (int loopIndex)
{
    fileChooser = std::make_unique<juce::FileChooser> (
        loopIndex == 0 ? "Load a melody into Loop A" : "Load a melody into Loop B",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");

    auto safe = juce::Component::SafePointer<BtPOvenAudioProcessorEditor> (this);

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [safe, loopIndex] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr)
                                      return;

                                  const auto file = fc.getResult();

                                  if (file.existsAsFile())
                                      safe->processor.loadFileIntoLoop (loopIndex, file);

                                  safe->repaint();
                              });
}

void BtPOvenAudioProcessorEditor::exportLoopFile (int loopIndex)
{
    if (! processor.hasLoopContent (loopIndex))
    {
        processor.setStatusMessage ("NOTHING TO SAVE IN LOOP "
                                    + juce::String (loopIndex == 0 ? "A" : "B"));
        repaint();
        return;
    }

    fileChooser = std::make_unique<juce::FileChooser> (
        "Save Loop " + juce::String (loopIndex == 0 ? "A" : "B"),
        juce::File::getSpecialLocation (juce::File::userMusicDirectory)
            .getChildFile (juce::String ("BtP_Oven_Loop_") + (loopIndex == 0 ? "A" : "B") + ".wav"),
        "*.wav");

    auto safe = juce::Component::SafePointer<BtPOvenAudioProcessorEditor> (this);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe, loopIndex] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr)
                                      return;

                                  auto file = fc.getResult();

                                  if (file.getFullPathName().isEmpty())
                                      return;

                                  if (! file.hasFileExtension ("wav"))
                                      file = file.withFileExtension ("wav");

                                  safe->processor.exportLoop (loopIndex, file);
                                  safe->repaint();
                              });
}

//==============================================================================
bool BtPOvenAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif")
            || f.endsWithIgnoreCase (".aiff") || f.endsWithIgnoreCase (".flac")
            || f.endsWithIgnoreCase (".mp3") || f.endsWithIgnoreCase (".ogg"))
            return true;

    return false;
}

void BtPOvenAudioProcessorEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void BtPOvenAudioProcessorEditor::fileDragExit (const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void BtPOvenAudioProcessorEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    dragOver = false;

    if (files.isEmpty())
        return;

    int loopIndex = (int) std::lround (processor.apvts.getRawParameterValue ("active_loop")->load() * 2.0f);

    const auto ovenBounds = ovenWindow.getBounds();

    if (ovenBounds.contains (x, y))
        loopIndex = y < ovenBounds.getCentreY() ? 0 : 1;

    processor.loadFileIntoLoop (loopIndex, juce::File (files[0]));
    repaint();
}

//==============================================================================
void BtPOvenAudioProcessorEditor::resized()
{
    sc = (float) getWidth() / (float) kDesignWidth;

    ovenWindow.setBounds (S (16.0f, 84.0f, 596.0f, 386.0f));
    rackPage.setBounds (S (16.0f, 84.0f, 1148.0f, 692.0f));
    aiPage.setBounds (S (16.0f, 84.0f, 1148.0f, 692.0f));
    stemPage.setBounds (S (16.0f, 84.0f, 1148.0f, 692.0f));
    oneShotPage.setBounds (S (16.0f, 84.0f, 1148.0f, 692.0f));
    rvcPage.setBounds (S (16.0f, 84.0f, 1148.0f, 692.0f));

    panelCook      = S (624.0f, 84.0f, 538.0f, 196.0f);
    panelTiming    = S (624.0f, 288.0f, 538.0f, 182.0f);
    panelTransport = S (16.0f, 478.0f, 520.0f, 200.0f);
    panelPitch     = S (546.0f, 478.0f, 300.0f, 200.0f);
    panelFilters   = S (856.0f, 478.0f, 306.0f, 200.0f);
    panelFx        = S (16.0f, 686.0f, 1146.0f, 90.0f);

    meterArea     = S (996.0f, 26.0f, 164.0f, 26.0f);
    laneStripArea = S (508.0f, 712.0f, 640.0f, 48.0f);
    statusArea    = S (28.0f, 638.0f, 496.0f, 30.0f);

    const auto labelFont = juce::Font (juce::FontOptions (juce::jmax (8.0f, 10.5f * sc),
                                                          juce::Font::bold));

    for (auto& knob : knobs)
    {
        auto area = S (knob->design.getX(), knob->design.getY(),
                       knob->design.getWidth(), knob->design.getHeight());

        knob->label->setFont (labelFont);
        knob->label->setBounds (area.removeFromTop (juce::roundToInt (13.0f * sc)));

        knob->slider->setBounds (area);
        knob->slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false,
                                       area.getWidth(), juce::roundToInt (14.0f * sc));
    }

    for (auto& b : buttons)
        b.button->setBounds (S (b.design.getX(), b.design.getY(),
                                b.design.getWidth(), b.design.getHeight()));

    if (filterBox != nullptr)
        filterBox->setBounds (S (870.0f, 506.0f, 278.0f, 26.0f));

    if (chopDivBox != nullptr)
        chopDivBox->setBounds (S (736.0f, 410.0f, 92.0f, 24.0f));
}

//==============================================================================
void BtPOvenAudioProcessorEditor::toggleRackPage()
{
    rackPageVisible = ! rackPageVisible;
    aiPageVisible = false;
    stemPageVisible = false;
    oneShotPageVisible = false;
    rvcPageVisible = false;
    setMainPageVisible (! rackPageVisible);
}

void BtPOvenAudioProcessorEditor::toggleAiPage()
{
    aiPageVisible = ! aiPageVisible;
    rackPageVisible = false;
    stemPageVisible = false;
    oneShotPageVisible = false;
    rvcPageVisible = false;
    setMainPageVisible (! aiPageVisible);
}

void BtPOvenAudioProcessorEditor::toggleStemPage()
{
    stemPageVisible = ! stemPageVisible;
    rackPageVisible = false;
    aiPageVisible = false;
    oneShotPageVisible = false;
    rvcPageVisible = false;
    setMainPageVisible (! stemPageVisible);
}

void BtPOvenAudioProcessorEditor::toggleOneShotPage()
{
    oneShotPageVisible = ! oneShotPageVisible;
    rackPageVisible = false;
    aiPageVisible = false;
    stemPageVisible = false;
    rvcPageVisible = false;
    setMainPageVisible (! oneShotPageVisible);
}

void BtPOvenAudioProcessorEditor::toggleRvcPage()
{
    rvcPageVisible = ! rvcPageVisible;
    rackPageVisible = false;
    aiPageVisible = false;
    stemPageVisible = false;
    oneShotPageVisible = false;
    setMainPageVisible (! rvcPageVisible);
}

void BtPOvenAudioProcessorEditor::setMainPageVisible (bool visible)
{
    const bool showMain = visible && ! rackPageVisible && ! aiPageVisible && ! stemPageVisible
                       && ! oneShotPageVisible && ! rvcPageVisible;

    ovenWindow.setVisible (showMain);

    for (auto& knob : knobs)
    {
        if (! knob->keepVisible)
        {
            knob->slider->setVisible (showMain);
            knob->label->setVisible (showMain);
        }
    }

    for (auto& entry : buttons)
        if (! entry.keepVisible)
            entry.button->setVisible (showMain);

    if (filterBox != nullptr)
        filterBox->setVisible (showMain);

    if (chopDivBox != nullptr)
        chopDivBox->setVisible (showMain);

    rackPage.setVisible (rackPageVisible);
    aiPage.setVisible (aiPageVisible);
    stemPage.setVisible (stemPageVisible);
    oneShotPage.setVisible (oneShotPageVisible);
    rvcPage.setVisible (rvcPageVisible);

    if (rackButton != nullptr)
    {
        rackButton->setButtonText (rackPageVisible ? "MAIN" : "RACK");
        rackButton->setToggleState (rackPageVisible, juce::dontSendNotification);
    }

    if (aiButton != nullptr)
    {
        aiButton->setButtonText (aiPageVisible ? "MAIN" : "AI");
        aiButton->setToggleState (aiPageVisible, juce::dontSendNotification);
    }

    if (stemButton != nullptr)
    {
        stemButton->setButtonText (stemPageVisible ? "MAIN" : "STEM");
        stemButton->setToggleState (stemPageVisible, juce::dontSendNotification);
    }

    if (oneShotButton != nullptr)
    {
        oneShotButton->setButtonText (oneShotPageVisible ? "MAIN" : "SHOTS");
        oneShotButton->setToggleState (oneShotPageVisible, juce::dontSendNotification);
    }

    if (rvcButton != nullptr)
    {
        rvcButton->setButtonText (rvcPageVisible ? "MAIN" : "VOICE");
        rvcButton->setToggleState (rvcPageVisible, juce::dontSendNotification);
    }

    repaint();
}

//==============================================================================
void BtPOvenAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.fillAll (juce::Colour (0xff0c0d10));

    juce::ColourGradient background (juce::Colour (0xff1a1c21), 0.0f, 0.0f,
                                     juce::Colour (0xff090a0c), 0.0f, bounds.getBottom(), false);
    background.addColour (0.45, juce::Colour (0xff14161a));
    g.setGradientFill (background);
    g.fillRect (bounds);

    // Warm floor glow under the oven
    {
        juce::ColourGradient glow (OvenLookAndFeel::ember().withAlpha (0.05f),
                                   bounds.getCentreX(), bounds.getBottom(),
                                   juce::Colours::transparentBlack,
                                   bounds.getCentreX(), bounds.getBottom() - 220.0f * sc, false);
        g.setGradientFill (glow);
        g.fillRect (bounds);
    }

    // Metal speckle
    {
        juce::Random rng (0x51ee);
        g.setColour (juce::Colours::white.withAlpha (0.012f));

        for (int i = 0; i < 900; ++i)
            g.fillRect ((float) rng.nextInt (juce::jmax (1, getWidth())),
                        (float) rng.nextInt (juce::jmax (1, getHeight())),
                        1.0f, 1.0f);
    }

    paintHeader (g);

    if (! rackPageVisible && ! aiPageVisible && ! stemPageVisible && ! oneShotPageVisible && ! rvcPageVisible)
    {
        paintPanel (g, panelCook, "COOK MODE", "");
        paintPanel (g, panelTiming, "TRAP TIMING", "CHOP  /  STUTTER  /  DRIP");
        paintPanel (g, panelTransport, "DUAL LOOPS", "");
        paintPanel (g, panelPitch, "PITCH + TEXTURE", "");
        paintPanel (g, panelFilters, "DARK FILTERS", "SMOKE = LP+SAT   BASEMENT = BP+VERB");
        paintPanel (g, panelFx, "FX RECORDER", "HAPPY ACCIDENT ENGINE", true);

        // Drop shadow around the oven window
        {
            juce::DropShadow shadow (juce::Colours::black.withAlpha (0.7f),
                                     juce::roundToInt (14.0f * sc), { 0, juce::roundToInt (4.0f * sc) });
            shadow.drawForRectangle (g, S (16.0f, 84.0f, 596.0f, 386.0f));
        }

        if (dragOver)
        {
            g.setColour (OvenLookAndFeel::lime().withAlpha (0.9f));
            g.drawRoundedRectangle (S (10.0f, 78.0f, 608.0f, 398.0f).toFloat(), 12.0f * sc, 2.5f * sc);

            g.setColour (OvenLookAndFeel::lime().withAlpha (0.08f));
            g.fillRoundedRectangle (S (10.0f, 78.0f, 608.0f, 398.0f).toFloat(), 12.0f * sc);
        }

        paintLaneStrip (g);
        paintStatus (g);
    }

    paintLevelMeter (g);
}

//==============================================================================
void BtPOvenAudioProcessorEditor::paintHeader (juce::Graphics& g)
{
    const float height = 76.0f * sc;
    const auto header = juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), height);

    juce::ColourGradient grad (juce::Colour (0xff2b2e34), 0.0f, 0.0f,
                               juce::Colour (0xff14161a), 0.0f, height, false);
    grad.addColour (0.6, juce::Colour (0xff1e2025));
    g.setGradientFill (grad);
    g.fillRect (header);

    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (0.0f, 1.0f, header.getWidth(), 1.0f);

    g.setColour (juce::Colour (0xff07080a));
    g.fillRect (0.0f, height - 2.0f, header.getWidth(), 2.0f);

    // Red glow behind the brand
    {
        juce::ColourGradient glow (OvenLookAndFeel::blood().withAlpha (0.22f),
                                   120.0f * sc, 40.0f * sc,
                                   juce::Colours::transparentBlack,
                                   260.0f * sc, 40.0f * sc, true);
        g.setGradientFill (glow);
        g.fillRect (0.0f, 0.0f, 380.0f * sc, height);
    }

    const auto brandFont = juce::Font (juce::FontOptions (44.0f * sc,
                                                          juce::Font::bold | juce::Font::italic));
    const auto ovenFont = juce::Font (juce::FontOptions (48.0f * sc,
                                                         juce::Font::bold | juce::Font::italic));

    // "BtP" with drips
    g.setFont (brandFont);
    g.setColour (juce::Colour (0xff7d0a10));
    g.drawText ("BtP", juce::Rectangle<float> (27.0f * sc, 6.0f * sc, 130.0f * sc, 48.0f * sc),
                juce::Justification::centredLeft, false);

    g.setColour (OvenLookAndFeel::blood().brighter (0.15f));
    g.drawText ("BtP", juce::Rectangle<float> (25.0f * sc, 4.0f * sc, 130.0f * sc, 48.0f * sc),
                juce::Justification::centredLeft, false);

    {
        const float drips[][2] = { { 38.0f, 15.0f }, { 55.0f, 25.0f }, { 72.0f, 11.0f }, { 93.0f, 20.0f } };

        for (const auto& d : drips)
        {
            const float x = d[0] * sc;
            const float len = d[1] * sc;
            const float top = 40.0f * sc;

            g.setColour (juce::Colour (0xff8a0d13));
            g.fillRoundedRectangle (x, top, 3.6f * sc, len, 1.8f * sc);
            g.fillEllipse (x - 0.6f * sc, top + len - 2.0f * sc, 4.8f * sc, 4.8f * sc);
        }
    }

    // "OVEN" with dark outline
    g.setFont (ovenFont);
    g.setColour (juce::Colour (0xff050604));
    g.drawText ("OVEN", juce::Rectangle<float> (112.0f * sc, 2.0f * sc, 230.0f * sc, 50.0f * sc),
                juce::Justification::centredLeft, false);
    g.setColour (OvenLookAndFeel::lime());
    g.drawText ("OVEN", juce::Rectangle<float> (110.0f * sc, 0.0f, 230.0f * sc, 50.0f * sc),
                juce::Justification::centredLeft, false);
    // Subtitle
    g.setColour (OvenLookAndFeel::cream().withAlpha (0.55f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.5f, 9.5f * sc), juce::Font::bold)));
    g.drawText ("TRAP  MELODY  MUTATION  ENGINE",
                juce::Rectangle<float> (26.0f * sc, 50.0f * sc, 340.0f * sc, 18.0f * sc),
                juce::Justification::centredLeft, false);

    // Loop selector caption
    g.setColour (OvenLookAndFeel::cream().withAlpha (0.5f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.5f, 9.0f * sc), juce::Font::bold)));
    g.drawText ("ACTIVE LOOP", juce::Rectangle<float> (780.0f * sc, 4.0f * sc, 110.0f * sc, 14.0f * sc),
                juce::Justification::centredLeft, false);

    // Meter caption
    g.setColour (OvenLookAndFeel::cream().withAlpha (0.45f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.5f, 8.5f * sc), juce::Font::bold)));
    g.drawText ("OUTPUT", juce::Rectangle<float> (996.0f * sc, 8.0f * sc, 164.0f * sc, 12.0f * sc),
                juce::Justification::centredLeft, false);
}

//==============================================================================
void BtPOvenAudioProcessorEditor::paintPanel (juce::Graphics& g, juce::Rectangle<int> area,
                                              const juce::String& title, const juce::String& hint,
                                              bool hintOnTitleRow)
{
    if (area.isEmpty())
        return;

    const auto r = area.toFloat();

    juce::ColourGradient grad (juce::Colour (0xff23262b), r.getX(), r.getY(),
                               juce::Colour (0xff141619), r.getX(), r.getBottom(), false);
    grad.addColour (0.75, juce::Colour (0xff1a1c20));

    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 8.0f * sc);

    g.setColour (juce::Colour (0xff07080a));
    g.drawRoundedRectangle (r, 8.0f * sc, 1.6f * sc);

    g.setColour (juce::Colours::white.withAlpha (0.045f));
    g.drawRoundedRectangle (r.reduced (1.5f * sc), 7.0f * sc, 1.0f * sc);

    // Title
    if (title.isNotEmpty())
    {
        g.setColour (OvenLookAndFeel::lime().withAlpha (0.88f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (8.5f, 11.0f * sc), juce::Font::bold)));
        g.drawText (title,
                    juce::Rectangle<float> (r.getX() + 14.0f * sc, r.getY() + 6.0f * sc,
                                            r.getWidth() - 28.0f * sc, 16.0f * sc),
                    juce::Justification::centredLeft, false);

        g.setColour (OvenLookAndFeel::lime().withAlpha (0.55f));
        g.fillRect (r.getX() + 14.0f * sc, r.getY() + 23.0f * sc, 22.0f * sc, 1.6f * sc);
    }

    if (hint.isNotEmpty())
    {
        g.setColour (OvenLookAndFeel::cream().withAlpha (0.28f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.0f, 8.0f * sc), juce::Font::bold)));

        const auto hintArea = hintOnTitleRow
            ? juce::Rectangle<float> (r.getX() + 14.0f * sc, r.getY() + 6.0f * sc,
                                      r.getWidth() - 28.0f * sc, 16.0f * sc)
            : juce::Rectangle<float> (r.getX() + 14.0f * sc, r.getBottom() - 18.0f * sc,
                                      r.getWidth() - 28.0f * sc, 12.0f * sc);

        g.drawText (hint, hintArea,
                    hintOnTitleRow ? juce::Justification::centredRight
                                   : juce::Justification::centredLeft, false);
    }

    // Screws
    const float inset = 8.0f * sc;
    const float screwR = 2.6f * sc;
    const juce::Point<float> screws[] = {
        { r.getX() + inset, r.getY() + inset },
        { r.getRight() - inset, r.getY() + inset },
        { r.getX() + inset, r.getBottom() - inset },
        { r.getRight() - inset, r.getBottom() - inset }
    };

    for (const auto& s : screws)
    {
        g.setColour (juce::Colour (0xff08090b));
        g.fillEllipse (s.x - screwR, s.y - screwR, screwR * 2.0f, screwR * 2.0f);
        g.setColour (juce::Colour (0xff454a52));
        g.drawEllipse (s.x - screwR, s.y - screwR, screwR * 2.0f, screwR * 2.0f, 0.8f);
        g.drawLine (s.x - screwR * 0.6f, s.y, s.x + screwR * 0.6f, s.y, 0.9f);
    }
}

//==============================================================================
void BtPOvenAudioProcessorEditor::paintLevelMeter (juce::Graphics& g)
{
    if (meterArea.isEmpty())
        return;

    const auto r = meterArea.toFloat();

    g.setColour (juce::Colour (0xff050607));
    g.fillRoundedRectangle (r, 3.0f * sc);

    g.setColour (juce::Colour (0xff2c3036));
    g.drawRoundedRectangle (r, 3.0f * sc, 1.0f * sc);

    const int segments = 24;
    const float gap = 2.0f * sc;
    const float segW = (r.getWidth() - 6.0f * sc - gap * (float) (segments - 1)) / (float) segments;
    const float level = juce::jlimit (0.0f, 1.0f, processor.getOutputLevel());

    for (int i = 0; i < segments; ++i)
    {
        const float t = (float) i / (float) (segments - 1);
        const bool lit = level > t * 0.97f;

        auto colour = t < 0.62f ? OvenLookAndFeel::lime()
                    : t < 0.86f ? OvenLookAndFeel::ember()
                                : juce::Colour (0xffff2e2e);

        g.setColour (colour.withAlpha (lit ? 0.95f : 0.10f));
        g.fillRect (r.getX() + 3.0f * sc + (float) i * (segW + gap),
                    r.getY() + 4.0f * sc,
                    segW, r.getHeight() - 8.0f * sc);
    }
}

//==============================================================================
void BtPOvenAudioProcessorEditor::paintLaneStrip (juce::Graphics& g)
{
    if (laneStripArea.isEmpty())
        return;

    const auto r = laneStripArea.toFloat();
    const auto& recorder = processor.getFxRecorder();

    g.setColour (juce::Colour (0xff060708));
    g.fillRoundedRectangle (r, 4.0f * sc);

    g.setColour (juce::Colour (0xff262a30));
    g.drawRoundedRectangle (r, 4.0f * sc, 1.0f * sc);

    const int numPoints = 220;
    const float laneHeight = r.getHeight() / (float) btp::numLanes;
    const bool fxOn = processor.apvts.getRawParameterValue ("fx_play")->load() > 0.5f;

    for (int lane = 0; lane < btp::numLanes; ++lane)
    {
        const float y = r.getY() + (float) lane * laneHeight;

        g.setColour (juce::Colours::white.withAlpha (0.045f));
        g.fillRect (r.getX() + 2.0f * sc, y + laneHeight - 1.0f, r.getWidth() - 4.0f * sc, 1.0f);

        for (int i = 0; i < numPoints; ++i)
        {
            const float v = recorder.getNormalised (lane, i, numPoints,
                                                    kLaneMin[lane], kLaneMax[lane]);

            if (v < 0.0f)
                continue;

            const float x = r.getX() + 2.0f * sc
                          + (r.getWidth() - 4.0f * sc) * (float) i / (float) numPoints;

            const auto colour = fxOn ? OvenLookAndFeel::lime()
                                     : juce::Colour (0xff8fe6c8);

            g.setColour (colour.withAlpha (0.20f + 0.70f * v));
            g.fillRect (x, y + laneHeight * 0.18f, r.getWidth() / (float) numPoints + 0.6f,
                        laneHeight * 0.64f);
        }
    }
}

//==============================================================================
void BtPOvenAudioProcessorEditor::paintStatus (juce::Graphics& g)
{
    if (statusArea.isEmpty())
        return;

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double age = now - processor.getStatusTime();
    const float alpha = age < 6.0 ? 0.95f : 0.45f;

    g.setColour (OvenLookAndFeel::lime().withAlpha (alpha * 0.85f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.5f, 9.5f * sc), juce::Font::bold)));
    g.drawText ("> " + processor.getStatusMessage(),
                statusArea.toFloat(), juce::Justification::topLeft, true);

    // FX record progress
    if (processor.isFxRecording())
    {
        const auto bar = statusArea.toFloat().removeFromBottom (10.0f * sc);

        g.setColour (juce::Colour (0xff2a0a0c));
        g.fillRoundedRectangle (bar, 3.0f * sc);

        auto filled = bar;
        filled.setWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, processor.getFxProgress()));

        g.setColour (OvenLookAndFeel::blood().brighter (0.35f));
        g.fillRoundedRectangle (filled, 3.0f * sc);
    }
    else if (processor.apvts.getRawParameterValue ("fx_play")->load() > 0.5f)
    {
        g.setColour (OvenLookAndFeel::lime().withAlpha (0.75f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.5f, 9.0f * sc), juce::Font::bold)));
        g.drawText ("AUTO", statusArea.toFloat(), juce::Justification::bottomRight, false);
    }
}

//==============================================================================
void BtPOvenAudioProcessorEditor::timerCallback()
{
    const bool rec0 = processor.isLoopRecording (0);
    const bool rec1 = processor.isLoopRecording (1);

    auto updateRecordButton = [] (juce::TextButton* button, bool recording, const juce::String& idleText)
    {
        if (button == nullptr)
            return;

        const auto text = recording ? juce::String ("STOP") : idleText;

        if (button->getButtonText() != text)
            button->setButtonText (text);

        button->setToggleState (recording, juce::dontSendNotification);
    };

    updateRecordButton (recA, rec0, "REC A");
    updateRecordButton (recB, rec1, "REC B");
    updateRecordButton (dubA, rec0 && processor.isLoopOverdubbing (0), "DUB A");
    updateRecordButton (dubB, rec1 && processor.isLoopOverdubbing (1), "DUB B");

    if (fxArm != nullptr)
    {
        if (fxArm->getButtonText() != (processor.isFxRecording() ? "CAPTURING" : "ARM FX"))
            fxArm->setButtonText (processor.isFxRecording() ? "CAPTURING" : "ARM FX");

        fxArm->setToggleState (processor.isFxRecording(), juce::dontSendNotification);
    }

    const int active = (int) std::lround (processor.apvts.getRawParameterValue ("active_loop")->load() * 2.0f);

    if (loopA != nullptr)
        loopA->setToggleState (active == 0, juce::dontSendNotification);

    if (loopB != nullptr)
        loopB->setToggleState (active == 1, juce::dontSendNotification);

    if (loopC != nullptr)
        loopC->setToggleState (active == 2, juce::dontSendNotification);

    repaint (meterArea);
    repaint (laneStripArea);
    repaint (statusArea);
}
