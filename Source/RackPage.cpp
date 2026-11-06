#include "RackPage.h"

#include "OvenLookAndFeel.h"

namespace
{
    constexpr float kRackWidth  = 1148.0f;
    constexpr float kRackHeight = 692.0f;

    const juce::Rectangle<float> pDelay { 0.0f, 0.0f, 372.0f, 300.0f };
    const juce::Rectangle<float> pDist  { 0.0f, 306.0f, 372.0f, 300.0f };
    const juce::Rectangle<float> pMod   { 378.0f, 0.0f, 372.0f, 300.0f };
    const juce::Rectangle<float> pVerb  { 378.0f, 306.0f, 372.0f, 300.0f };
    const juce::Rectangle<float> pRoute { 756.0f, 0.0f, 392.0f, 300.0f };
    const juce::Rectangle<float> pKey   { 756.0f, 306.0f, 392.0f, 300.0f };
    const juce::Rectangle<float> pEq    { 0.0f, 612.0f, 1148.0f, 80.0f };

    inline bool isBlackKey (int pitchClass) noexcept
    {
        return pitchClass == 1 || pitchClass == 3 || pitchClass == 6
            || pitchClass == 8 || pitchClass == 10;
    }
}

//==============================================================================
RackPage::RackPage (BtPOvenAudioProcessor& p)
    : processor (p)
{
    setOpaque (true);

    auto styleCombo = [] (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff101215));
        box.setColour (juce::ComboBox::textColourId, OvenLookAndFeel::lime());
        box.setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        box.setColour (juce::ComboBox::arrowColourId, OvenLookAndFeel::lime());
    };

    //==========================================================================
    // Delay
    addToggle ("delay_on", "DELAY ON", { 272.0f, 8.0f, 88.0f, 22.0f });
    addToggle ("delay_pingpong", "PING PONG", { 168.0f, 8.0f, 98.0f, 22.0f });

    addKnob ("delay_time",     "TIME",     { 12.0f, 64.0f, 84.0f, 88.0f });
    addKnob ("delay_feedback", "FDBK",     { 102.0f, 64.0f, 84.0f, 88.0f });
    addKnob ("delay_tone",     "TONE",     { 192.0f, 64.0f, 84.0f, 88.0f });
    addKnob ("delay_mix",      "MIX",      { 282.0f, 64.0f, 84.0f, 88.0f });

    //==========================================================================
    // Distortion + bitcrush
    addToggle ("dist_on",  "DIST ON",  { 12.0f, 342.0f, 96.0f, 22.0f });
    addToggle ("crush_on", "CRUSH ON", { 114.0f, 342.0f, 96.0f, 22.0f });

    addKnob ("dist_drive", "DRIVE", { 10.0f, 388.0f, 68.0f, 84.0f });
    addKnob ("dist_tone",  "TONE",  { 80.0f, 388.0f, 68.0f, 84.0f });
    addKnob ("dist_mix",   "MIX",   { 150.0f, 388.0f, 68.0f, 84.0f });
    addKnob ("crush_bits", "BITS",  { 220.0f, 388.0f, 68.0f, 84.0f });
    addKnob ("crush_mix",  "MIX",   { 290.0f, 388.0f, 68.0f, 84.0f });

    //==========================================================================
    // Chorus + flanger
    addToggle ("chorus_on",  "CHORUS",  { 390.0f, 36.0f, 100.0f, 22.0f });
    addToggle ("flanger_on", "FLANGER", { 496.0f, 36.0f, 100.0f, 22.0f });

    addKnob ("chorus_rate",  "CH RATE",  { 388.0f, 72.0f, 84.0f, 82.0f });
    addKnob ("chorus_depth", "CH DEPTH", { 476.0f, 72.0f, 84.0f, 82.0f });
    addKnob ("chorus_mix",   "CH MIX",   { 564.0f, 72.0f, 84.0f, 82.0f });
    addKnob ("flanger_rate", "FL RATE",  { 652.0f, 72.0f, 84.0f, 82.0f });

    addKnob ("flanger_depth",    "FL DEPTH", { 388.0f, 162.0f, 84.0f, 82.0f });
    addKnob ("flanger_feedback", "FL FDBK",  { 476.0f, 162.0f, 84.0f, 82.0f });
    addKnob ("flanger_mix",      "FL MIX",   { 564.0f, 162.0f, 84.0f, 82.0f });

    //==========================================================================
    // Phaser + rack reverb
    addToggle ("phaser_on",    "PHASER", { 390.0f, 342.0f, 94.0f, 22.0f });
    addToggle ("rack_verb_on", "VERB",   { 490.0f, 342.0f, 86.0f, 22.0f });

    addKnob ("phaser_rate",  "PH RATE",  { 388.0f, 378.0f, 84.0f, 82.0f });
    addKnob ("phaser_depth", "PH DEPTH", { 476.0f, 378.0f, 84.0f, 82.0f });
    addKnob ("phaser_mix",   "PH MIX",   { 564.0f, 378.0f, 84.0f, 82.0f });

    addKnob ("rack_verb_size", "VERB SIZE", { 388.0f, 468.0f, 84.0f, 82.0f });
    addKnob ("rack_verb_mix",  "VERB MIX",  { 476.0f, 468.0f, 84.0f, 82.0f });

    //==========================================================================
    // Routing
    routeBox = std::make_unique<juce::ComboBox>();
    routeBox->addItemList ({ "SERIES  (INSERT)", "PARALLEL  (SEND / RETURN)" }, 1);
    styleCombo (*routeBox);
    addAndMakeVisible (*routeBox);

    routeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "rack_route", *routeBox);

    addKnob ("rack_mix", "RACK MIX", { 770.0f, 84.0f, 90.0f, 84.0f });

    addAction ("NUKED", { 902.0f, 84.0f, 108.0f, 36.0f },
               [this]
               {
                   processor.nuke();
                   repaint();
               }, true);

    addAction ("MAIN PAGE", { 1022.0f, 84.0f, 108.0f, 36.0f },
               [this]
               {
                   if (onMainPage != nullptr)
                       onMainPage();
               });

    //==========================================================================
    // Key / scale
    keyBox = std::make_unique<juce::ComboBox>();
    keyBox->addItemList ({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
    styleCombo (*keyBox);
    addAndMakeVisible (*keyBox);

    keyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "key_root", *keyBox);

    scaleBox = std::make_unique<juce::ComboBox>();
    scaleBox->addItemList ({ "MAJOR", "MINOR" }, 1);
    styleCombo (*scaleBox);
    addAndMakeVisible (*scaleBox);

    scaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "scale_type", *scaleBox);

    addToggle ("key_lock", "KEY LOCK", { 1026.0f, 342.0f, 100.0f, 28.0f });

    //==========================================================================
    // Auto-tune (key + scale locked)
    addToggle ("autotune_on", "AUTOTUNE", { 770.0f, 376.0f, 120.0f, 24.0f });

    addKnob ("autotune_speed", "RETUNE", { 770.0f, 406.0f, 84.0f, 76.0f });
    addKnob ("autotune_mix",   "MIX",    { 866.0f, 406.0f, 84.0f, 76.0f });
    addKnob ("autotune_snap",  "SNAP",   { 962.0f, 406.0f, 84.0f, 76.0f });

    //==========================================================================
    // Five-band EQ
    addToggle ("eq_on", "EQ ON", { 126.0f, 620.0f, 84.0f, 22.0f });

    addKnob ("eq_low",      "90 Hz",  { 220.0f, 616.0f, 96.0f, 70.0f });
    addKnob ("eq_low_mid",  "250 Hz", { 366.0f, 616.0f, 96.0f, 70.0f });
    addKnob ("eq_mid",      "1 kHz",  { 512.0f, 616.0f, 96.0f, 70.0f });
    addKnob ("eq_high_mid", "3.5 kHz",{ 658.0f, 616.0f, 96.0f, 70.0f });
    addKnob ("eq_high",     "12 kHz", { 804.0f, 616.0f, 96.0f, 70.0f });

    lastKey = (int) std::lround (processor.apvts.getRawParameterValue ("key_root")->load());
    lastScale = (int) std::lround (processor.apvts.getRawParameterValue ("scale_type")->load());

    startTimerHz (10);
}

RackPage::~RackPage()
{
    stopTimer();
}

//==============================================================================
juce::Rectangle<int> RackPage::R (float x, float y, float w, float h) const
{
    return { juce::roundToInt (x * sc), juce::roundToInt (y * sc),
             juce::roundToInt (w * sc), juce::roundToInt (h * sc) };
}

//==============================================================================
void RackPage::addKnob (const juce::String& parameterId, const juce::String& name,
                        juce::Rectangle<float> designArea)
{
    auto knob = std::make_unique<Knob>();
    knob->design = designArea;

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

juce::TextButton* RackPage::addToggle (const juce::String& parameterId, const juce::String& text,
                                       juce::Rectangle<float> designArea)
{
    auto button = std::make_unique<juce::TextButton> (text);
    button->setClickingTogglesState (true);
    button->setColour (juce::TextButton::buttonOnColourId, OvenLookAndFeel::lime());
    button->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    button->setColour (juce::TextButton::textColourOffId, OvenLookAndFeel::cream());
    addAndMakeVisible (*button);

    auto* raw = button.get();

    auto& entry = buttons.emplace_back();
    entry.button = std::move (button);
    entry.design = designArea;
    entry.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, parameterId, *raw);

    return raw;
}

juce::TextButton* RackPage::addAction (const juce::String& text, juce::Rectangle<float> designArea,
                                       std::function<void()> onClick, bool danger)
{
    auto button = std::make_unique<juce::TextButton> (text);
    button->setColour (juce::TextButton::buttonOnColourId, OvenLookAndFeel::lime());
    button->setColour (juce::TextButton::textColourOnId, OvenLookAndFeel::lime());
    button->setColour (juce::TextButton::textColourOffId, OvenLookAndFeel::cream());

    if (danger)
        button->getProperties().set ("danger", true);

    if (onClick != nullptr)
        button->onClick = std::move (onClick);

    addAndMakeVisible (*button);

    auto* raw = button.get();

    auto& entry = buttons.emplace_back();
    entry.button = std::move (button);
    entry.design = designArea;

    return raw;
}

//==============================================================================
void RackPage::timerCallback()
{
    const int key = (int) std::lround (processor.apvts.getRawParameterValue ("key_root")->load());
    const int scale = (int) std::lround (processor.apvts.getRawParameterValue ("scale_type")->load());

    if (key != lastKey || scale != lastScale)
    {
        lastKey = key;
        lastScale = scale;
        repaint (keyboardArea.toNearestInt());
    }
}

//==============================================================================
void RackPage::resized()
{
    sc = (float) getWidth() / kRackWidth;

    const auto labelFont = juce::Font (juce::FontOptions (juce::jmax (7.5f, 10.0f * sc),
                                                          juce::Font::bold));

    for (auto& knob : knobs)
    {
        auto area = R (knob->design.getX(), knob->design.getY(),
                       knob->design.getWidth(), knob->design.getHeight());

        knob->label->setFont (labelFont);
        knob->label->setBounds (area.removeFromTop (juce::roundToInt (13.0f * sc)));

        knob->slider->setBounds (area);
        knob->slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false,
                                       area.getWidth(), juce::roundToInt (13.0f * sc));
    }

    for (auto& entry : buttons)
        entry.button->setBounds (R (entry.design.getX(), entry.design.getY(),
                                    entry.design.getWidth(), entry.design.getHeight()));

    if (routeBox != nullptr)
        routeBox->setBounds (R (770.0f, 38.0f, 362.0f, 28.0f));

    if (keyBox != nullptr)
        keyBox->setBounds (R (770.0f, 342.0f, 110.0f, 28.0f));

    if (scaleBox != nullptr)
        scaleBox->setBounds (R (888.0f, 342.0f, 130.0f, 28.0f));

    flowArea = R (770.0f, 188.0f, 362.0f, 100.0f).toFloat();
    keyboardArea = R (770.0f, 486.0f, 362.0f, 116.0f).toFloat();
    echoArea = R (14.0f, 166.0f, 344.0f, 116.0f).toFloat();
}

//==============================================================================
void RackPage::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto lime = OvenLookAndFeel::lime();

    g.fillAll (juce::Colour (0xff0c0d10));

    juce::ColourGradient background (juce::Colour (0xff1a1c21), 0.0f, 0.0f,
                                     juce::Colour (0xff090a0c), 0.0f, bounds.getBottom(), false);
    background.addColour (0.45, juce::Colour (0xff14161a));
    g.setGradientFill (background);
    g.fillRect (bounds);

    juce::ColourGradient glow (OvenLookAndFeel::ember().withAlpha (0.05f),
                               bounds.getCentreX(), bounds.getBottom(),
                               juce::Colours::transparentBlack,
                               bounds.getCentreX(), bounds.getBottom() - 240.0f * sc, false);
    g.setGradientFill (glow);
    g.fillRect (bounds);

    // Metal speckle
    {
        juce::Random rng (0x9a71);
        g.setColour (juce::Colours::white.withAlpha (0.012f));

        for (int i = 0; i < 800; ++i)
            g.fillRect ((float) rng.nextInt (juce::jmax (1, getWidth())),
                        (float) rng.nextInt (juce::jmax (1, getHeight())),
                        1.0f, 1.0f);
    }

    paintPanel (g, pDelay, "PING-PONG DELAY", "FEEDBACK TAP - PING PONG CROSS-FEEDS L / R");
    paintPanel (g, pMod,   "CHORUS + FLANGER", "SHARED MOD DELAY LINE");
    paintPanel (g, pDist,  "DISTORTION + BITCRUSH", "SOFT CLIP -> TONE LP   /   QUANTISE + SAMPLE HOLD");
    paintPanel (g, pVerb,  "PHASER + RACK VERB", "FOUR-STAGE ALL-PASS / ROOM");
    paintPanel (g, pRoute, "ROUTING", "SERIES = INSERT   /   PARALLEL = DRY + WET");
    paintPanel (g, pKey,   "KEY + SCALE + AUTOTUNE", "SNAPS THE MELODY TO THE SELECTED KEY / SCALE");
    paintPanel (g, pEq,    "FIVE BAND EQ", "LOW SHELF / PEAK / PEAK / PEAK / HIGH SHELF");

    paintDelayEchoes (g);
    paintSignalFlow (g);
    paintKeyboard (g);

    // EQ frequency hint
    g.setColour (OvenLookAndFeel::cream().withAlpha (0.32f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.0f, 8.5f * sc), juce::Font::bold)));
    g.drawText ("90 / 250 / 1K / 3.5K / 12K",
                R (912.0f, 626.0f, 224.0f, 20.0f), juce::Justification::centredLeft, false);

    g.setColour (lime.withAlpha (0.28f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.0f, 8.0f * sc), juce::Font::bold)));
    g.drawText ("BtP OVEN  -  FULL RACK",
                R (912.0f, 648.0f, 224.0f, 20.0f), juce::Justification::centredLeft, false);
}

//==============================================================================
void RackPage::paintPanel (juce::Graphics& g, juce::Rectangle<float> designArea,
                           const juce::String& title, const juce::String& hint)
{
    const auto r = designArea * sc;
    const auto lime = OvenLookAndFeel::lime();

    juce::ColourGradient grad (juce::Colour (0xff23262b), r.getX(), r.getY(),
                               juce::Colour (0xff141619), r.getX(), r.getBottom(), false);
    grad.addColour (0.75, juce::Colour (0xff1a1c20));

    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 8.0f * sc);

    g.setColour (juce::Colour (0xff07080a));
    g.drawRoundedRectangle (r, 8.0f * sc, 1.6f * sc);

    g.setColour (juce::Colours::white.withAlpha (0.045f));
    g.drawRoundedRectangle (r.reduced (1.5f * sc), 7.0f * sc, 1.0f * sc);

    g.setColour (lime.withAlpha (0.88f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (8.5f, 11.0f * sc), juce::Font::bold)));
    g.drawText (title,
                juce::Rectangle<float> (r.getX() + 14.0f * sc, r.getY() + 6.0f * sc,
                                        r.getWidth() - 28.0f * sc, 16.0f * sc),
                juce::Justification::centredLeft, false);

    g.setColour (lime.withAlpha (0.55f));
    g.fillRect (r.getX() + 14.0f * sc, r.getY() + 23.0f * sc, 22.0f * sc, 1.6f * sc);

    if (hint.isNotEmpty())
    {
        g.setColour (OvenLookAndFeel::cream().withAlpha (0.28f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.0f, 8.0f * sc), juce::Font::bold)));
        g.drawText (hint,
                    juce::Rectangle<float> (r.getX() + 14.0f * sc, r.getBottom() - 18.0f * sc,
                                            r.getWidth() - 28.0f * sc, 12.0f * sc),
                    juce::Justification::centredLeft, false);
    }

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
void RackPage::paintDelayEchoes (juce::Graphics& g)
{
    if (echoArea.isEmpty())
        return;

    const auto lime = OvenLookAndFeel::lime();
    const bool on = processor.apvts.getRawParameterValue ("delay_on")->load() > 0.5f;
    const float feedback = juce::jlimit (0.0f, 0.95f,
                                         processor.apvts.getRawParameterValue ("delay_feedback")->load());
    const bool pingPong = processor.apvts.getRawParameterValue ("delay_pingpong")->load() > 0.5f;

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (echoArea, 4.0f * sc);

    const float midY = echoArea.getCentreY();
    g.setColour (lime.withAlpha (on ? 0.16f : 0.06f));
    g.fillRect (echoArea.getX() + 8.0f * sc, midY, echoArea.getWidth() - 16.0f * sc, 1.0f * sc);

    const int taps = 9;

    for (int i = 0; i < taps; ++i)
    {
        const float t = (float) i / (float) (taps - 1);
        const float x = echoArea.getX() + 12.0f * sc
                      + t * (echoArea.getWidth() - 24.0f * sc);
        const float size = (3.0f + 5.0f * std::pow (0.82f, (float) i)) * sc;
        const float alpha = on ? juce::jlimit (0.05f, 0.85f, std::pow (feedback, (float) i) * 0.9f) : 0.08f;
        const float y = pingPong ? (i % 2 == 0 ? midY - 12.0f * sc : midY + 12.0f * sc) : midY;

        g.setColour (lime.withAlpha (alpha));
        g.fillEllipse (x - size * 0.5f, y - size * 0.5f, size, size);

        if (i < taps - 1)
        {
            const float nx = echoArea.getX() + 12.0f * sc
                           + ((float) (i + 1) / (float) (taps - 1)) * (echoArea.getWidth() - 24.0f * sc);
            const float ny = pingPong ? ((i + 1) % 2 == 0 ? midY - 12.0f * sc : midY + 12.0f * sc) : midY;

            g.setColour (lime.withAlpha (alpha * 0.35f));
            g.drawLine (x, y, nx, ny, 1.0f * sc);
        }
    }
}

//==============================================================================
void RackPage::paintSignalFlow (juce::Graphics& g)
{
    if (flowArea.isEmpty())
        return;

    const auto lime = OvenLookAndFeel::lime();
    const bool parallel = processor.apvts.getRawParameterValue ("rack_route")->load() > 0.5f;

    const bool dist = processor.apvts.getRawParameterValue ("dist_on")->load() > 0.5f
                   || processor.apvts.getRawParameterValue ("crush_on")->load() > 0.5f;
    const bool mod = processor.apvts.getRawParameterValue ("chorus_on")->load() > 0.5f
                  || processor.apvts.getRawParameterValue ("flanger_on")->load() > 0.5f
                  || processor.apvts.getRawParameterValue ("phaser_on")->load() > 0.5f;
    const bool delay = processor.apvts.getRawParameterValue ("delay_on")->load() > 0.5f;
    const bool verb = processor.apvts.getRawParameterValue ("rack_verb_on")->load() > 0.5f;

    g.setColour (juce::Colours::black.withAlpha (0.30f));
    g.fillRoundedRectangle (flowArea, 4.0f * sc);

    auto drawBlock = [&] (float x, float y, float w, float h, const juce::String& text, bool on)
    {
        const juce::Rectangle<float> area (x, y, w, h);

        g.setColour (on ? lime.withAlpha (0.16f) : juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (area, 3.0f * sc);

        g.setColour (on ? lime.withAlpha (0.70f) : juce::Colours::white.withAlpha (0.18f));
        g.drawRoundedRectangle (area, 3.0f * sc, 1.0f * sc);

        g.setColour (on ? lime.withAlpha (0.95f) : OvenLookAndFeel::cream().withAlpha (0.35f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (6.0f, 7.5f * sc), juce::Font::bold)));
        g.drawText (text, area, juce::Justification::centred, false);
    };

    auto drawArrow = [&] (float x1, float y1, float x2, float y2)
    {
        g.setColour (lime.withAlpha (0.35f));
        g.drawLine (x1, y1, x2, y2, 1.0f * sc);

        juce::Path arrow;
        arrow.addTriangle (x2, y2, x2 - 4.0f * sc, y2 - 2.0f * sc, x2 - 4.0f * sc, y2 + 2.0f * sc);
        g.fillPath (arrow);
    };

    if (! parallel)
    {
        const float h = 26.0f * sc;
        const float y = flowArea.getCentreY() - h * 0.5f;
        const float gap = 8.0f * sc;
        float x = flowArea.getX() + 6.0f * sc;

        const std::pair<const char*, bool> chain[] = {
            { "DRIVE", dist }, { "CRUSH", dist }, { "MOD", mod },
            { "DELAY", delay }, { "VERB", verb }
        };

        drawBlock (x, y, 30.0f * sc, h, "IN", true);
        x += 30.0f * sc;

        for (const auto& block : chain)
        {
            drawArrow (x + 1.0f * sc, flowArea.getCentreY(), x + gap - 2.0f * sc, flowArea.getCentreY());
            x += gap;
            drawBlock (x, y, 46.0f * sc, h, block.first, block.second);
            x += 46.0f * sc;
        }

        drawArrow (x + 1.0f * sc, flowArea.getCentreY(), x + gap - 2.0f * sc, flowArea.getCentreY());
        x += gap;
        drawBlock (x, y, 34.0f * sc, h, "OUT", true);
    }
    else
    {
        const float inW = 34.0f * sc;
        const float outW = 34.0f * sc;
        const float topY = flowArea.getY() + 14.0f * sc;
        const float botY = flowArea.getY() + 58.0f * sc;
        const float h = 24.0f * sc;

        drawBlock (flowArea.getX() + 6.0f * sc, flowArea.getCentreY() - h * 0.5f, inW, h, "IN", true);
        drawBlock (flowArea.getRight() - outW - 6.0f * sc, flowArea.getCentreY() - h * 0.5f, outW, h, "OUT", true);

        // Dry path along the top.
        drawArrow (flowArea.getX() + 6.0f * sc + inW, topY + h * 0.5f,
                   flowArea.getRight() - outW - 8.0f * sc, topY + h * 0.5f);
        g.setColour (OvenLookAndFeel::cream().withAlpha (0.30f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (6.0f, 7.0f * sc), juce::Font::bold)));
        g.drawText ("DRY", juce::Rectangle<float> (flowArea.getX() + 70.0f * sc, topY - 7.0f * sc,
                                                   60.0f * sc, 12.0f * sc),
                    juce::Justification::centredLeft, false);

        // Wet chain along the bottom.
        const std::pair<const char*, bool> chain[] = {
            { "DRIVE", dist }, { "CRUSH", dist }, { "MOD", mod },
            { "DELAY", delay }, { "VERB", verb }
        };

        float x = flowArea.getX() + 54.0f * sc;

        for (const auto& block : chain)
        {
            drawBlock (x, botY, 44.0f * sc, h, block.first, block.second);
            x += 50.0f * sc;
        }
    }
}

//==============================================================================
void RackPage::paintKeyboard (juce::Graphics& g)
{
    if (keyboardArea.isEmpty())
        return;

    const auto lime = OvenLookAndFeel::lime();
    const int root = juce::jlimit (0, 11, lastKey);
    const int scaleType = juce::jlimit (0, 1, lastScale);

    static constexpr int major[] = { 0, 2, 4, 5, 7, 9, 11 };
    static constexpr int minor[] = { 0, 2, 3, 5, 7, 8, 10 };
    const int* degrees = scaleType == 1 ? minor : major;

    bool selected[12] = {};

    for (int i = 0; i < 7; ++i)
        selected[(root + degrees[i]) % 12] = true;

    const auto r = keyboardArea;
    const float whiteW = r.getWidth() / 14.0f;
    const float h = r.getHeight() - 18.0f * sc;

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.expanded (3.0f * sc), 4.0f * sc);

    // White keys
    int whiteIndex = 0;

    for (int octave = 0; octave < 2; ++octave)
    {
        for (int pc = 0; pc < 12; ++pc)
        {
            if (isBlackKey (pc))
                continue;

            auto key = juce::Rectangle<float> (r.getX() + (float) whiteIndex * whiteW,
                                               r.getY(), whiteW - 1.0f * sc, h);
            ++whiteIndex;

            const bool on = selected[pc];
            g.setColour (on ? lime.withAlpha (0.58f) : OvenLookAndFeel::cream().withAlpha (0.80f));
            g.fillRect (key);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawRect (key, 1.0f * sc);

            if (pc == 0)
            {
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.setFont (juce::Font (juce::FontOptions (juce::jmax (6.0f, 8.0f * sc), juce::Font::bold)));
                g.drawText (octave == 0 ? "C" : "C'",
                            key.removeFromBottom (16.0f * sc), juce::Justification::centred, false);
            }
        }
    }

    // Black keys
    const int whiteBefore[12] = { 0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6 };

    for (int octave = 0; octave < 2; ++octave)
    {
        for (int pc = 0; pc < 12; ++pc)
        {
            if (! isBlackKey (pc))
                continue;

            const float cx = r.getX() + (float) (whiteBefore[pc] + octave * 7) * whiteW;
            const juce::Rectangle<float> key (cx - whiteW * 0.28f, r.getY(),
                                              whiteW * 0.56f, h * 0.62f);

            const bool on = selected[pc];
            g.setColour (on ? lime.withAlpha (0.80f) : juce::Colour (0xff14161a));
            g.fillRect (key);
            g.setColour (juce::Colours::black.withAlpha (0.75f));
            g.drawRect (key, 1.0f * sc);
        }
    }

    // Root / scale caption below the keys
    g.setColour (lime.withAlpha (0.85f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.0f, 9.0f * sc), juce::Font::bold)));
    g.drawText (juce::String (btp::noteName (root)) + " " + (scaleType == 0 ? "MAJOR" : "MINOR")
                  + "   /   " + juce::String (scaleType == 0 ? "7 NOTES LOCKED" : "NATURAL MINOR"),
                r.withTop (r.getBottom() - 17.0f * sc),
                juce::Justification::centred, false);
}
