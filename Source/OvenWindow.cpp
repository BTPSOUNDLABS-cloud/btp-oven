#include "OvenWindow.h"
#include "OvenLookAndFeel.h"

OvenWindow::OvenWindow (BtPOvenAudioProcessor& p)
    : processor (p)
{
    setInterceptsMouseClicks (true, true);
    setOpaque (false);

    const juce::Colour lime = OvenLookAndFeel::lime();
    const juce::Colour teal = juce::Colour (0xff8fe6c8);

    for (int loop = 0; loop < 2; ++loop)
    {
        auto& start = startSliders[(size_t) loop];
        start = std::make_unique<juce::Slider>();
        start->setSliderStyle (juce::Slider::LinearHorizontal);
        start->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        start->setRange (0.0, 1.0, 0.001);
        start->setColour (juce::Slider::backgroundColourId, juce::Colour (0xff1d221b));
        start->setColour (juce::Slider::trackColourId, lime.withAlpha (0.55f));
        start->setColour (juce::Slider::thumbColourId, lime);
        start->setTooltip ("Loop " + juce::String (loop == 0 ? "A" : "B") + " start");
        addAndMakeVisible (*start);

        auto& end = endSliders[(size_t) loop];
        end = std::make_unique<juce::Slider>();
        end->setSliderStyle (juce::Slider::LinearHorizontal);
        end->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        end->setRange (0.0, 1.0, 0.001);
        end->setColour (juce::Slider::backgroundColourId, juce::Colour (0xff1d221b));
        end->setColour (juce::Slider::trackColourId, teal.withAlpha (0.55f));
        end->setColour (juce::Slider::thumbColourId, teal);
        end->setTooltip ("Loop " + juce::String (loop == 0 ? "A" : "B") + " end");
        addAndMakeVisible (*end);

        auto& button = loopButtons[(size_t) loop];
        button = std::make_unique<juce::TextButton> ("LOOP");
        button->setClickingTogglesState (true);
        button->setColour (juce::TextButton::buttonOnColourId, lime);
        button->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        button->setColour (juce::TextButton::textColourOffId, lime.withAlpha (0.7f));
        button->setTooltip ("Loop " + juce::String (loop == 0 ? "A" : "B") + " on / off");
        addAndMakeVisible (*button);

        const auto paramPrefix = juce::String (loop == 0 ? "a_" : "b_");

        startAttachments[(size_t) loop] =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                processor.apvts, paramPrefix + "start", *start);
        endAttachments[(size_t) loop] =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                processor.apvts, paramPrefix + "end", *end);
        loopAttachments[(size_t) loop] =
            std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                processor.apvts, paramPrefix + "loop", *button);
    }

    startTimerHz (30);
}

OvenWindow::~OvenWindow()
{
    stopTimer();
}

void OvenWindow::timerCallback()
{
    for (int loop = 0; loop < 2; ++loop)
        for (int i = 0; i < peakCount; ++i)
            peaks[(size_t) loop][(size_t) i] = processor.getWaveformPeak (loop, i, peakCount);

    shimmerTime += 1.0f / 30.0f;

    if (shimmerTime > 1000.0f)
        shimmerTime = 0.0f;

    repaint();
}

juce::Rectangle<float> OvenWindow::getLaneBounds (int loop) const
{
    const auto full = getLocalBounds().toFloat();
    const float scale = full.getWidth() / 596.0f;
    auto glass = full.reduced (13.0f * scale, 13.0f * scale);
    const float laneGap = 8.0f * scale;

    auto laneA = glass.reduced (8.0f * scale, 7.0f * scale);
    auto laneB = laneA;

    laneA.setHeight ((laneA.getHeight() - laneGap) * 0.5f);
    laneB.setTop (laneA.getBottom() + laneGap);
    laneB.setBottom (glass.getBottom() - 7.0f * scale);

    return loop == 0 ? laneA : laneB;
}

juce::Rectangle<float> OvenWindow::getWaveBounds (juce::Rectangle<float> lane) const
{
    const float scale = getWidth() / 596.0f;
    auto waveArea = lane.reduced (10.0f * scale, 15.0f * scale);
    waveArea.setTop (waveArea.getY() + 4.0f * scale);

    const float controlTop = lane.getBottom() - 22.0f * scale;
    waveArea.setBottom (controlTop - 4.0f * scale);

    return waveArea;
}

void OvenWindow::resized()
{
    sc = (float) getWidth() / 596.0f;

    for (int loop = 0; loop < 2; ++loop)
    {
        const auto lane = getLaneBounds (loop);
        const auto controlTop = lane.getBottom() - 22.0f * sc;

        auto strip = lane.reduced (9.0f * sc, 0.0f).withY (controlTop).withHeight (17.0f * sc);

        const int loopWidth = juce::roundToInt (44.0f * sc);
        auto loopArea = strip.removeFromRight ((float) loopWidth);
        strip.removeFromRight (5.0f * sc);

        const int endWidth = juce::roundToInt (strip.getWidth() * 0.46f);
        auto endArea = strip.removeFromRight ((float) endWidth);
        strip.removeFromRight (5.0f * sc);

        startSliders[(size_t) loop]->setBounds (strip.toNearestInt());
        endSliders[(size_t) loop]->setBounds (endArea.toNearestInt());
        loopButtons[(size_t) loop]->setBounds (loopArea.reduced (1.0f * sc).toNearestInt());
    }
}

void OvenWindow::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    const float scale = full.getWidth() / 596.0f;
    const auto lime = OvenLookAndFeel::lime();

    //==========================================================================
    // Metal oven frame
    juce::ColourGradient frameGrad (juce::Colour (0xff35383f), full.getX(), full.getY(),
                                    juce::Colour (0xff16181c), full.getX(), full.getBottom(), false);
    frameGrad.addColour (0.55, juce::Colour (0xff24262b));

    g.setGradientFill (frameGrad);
    g.fillRoundedRectangle (full.reduced (1.0f), 10.0f * scale);

    g.setColour (juce::Colour (0xff07080a));
    g.drawRoundedRectangle (full.reduced (1.0f), 10.0f * scale, 2.0f);

    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (full.reduced (3.0f), 8.0f * scale, 1.0f);

    //==========================================================================
    auto glass = full.reduced (13.0f * scale, 13.0f * scale);

    {
        juce::ColourGradient glassGrad (juce::Colour (0xff0c1208), glass.getCentreX(), glass.getY(),
                                        juce::Colour (0xff030504), glass.getCentreX(), glass.getBottom(), false);
        glassGrad.addColour (0.48, juce::Colour (0xff0a1405));

        g.setGradientFill (glassGrad);
        g.fillRoundedRectangle (glass, 7.0f * scale);
    }

    g.setColour (juce::Colour (0xff020302));
    g.drawRoundedRectangle (glass, 7.0f * scale, 2.2f * scale);

    // Warm glow behind the glass
    {
        juce::ColourGradient glow (lime.withAlpha (0.09f), glass.getCentreX(), glass.getBottom(),
                                   lime.withAlpha (0.0f), glass.getCentreX(), glass.getY(), false);
        g.setGradientFill (glow);
        g.fillRoundedRectangle (glass, 7.0f * scale);
    }

    //==========================================================================
    juce::Graphics::ScopedSaveState clipState (g);
    juce::Path clipPath;
    clipPath.addRoundedRectangle (glass, 7.0f * scale);
    g.reduceClipRegion (clipPath);

    // Grille lines
    g.setColour (juce::Colours::white.withAlpha (0.022f));
    for (float y = glass.getY(); y < glass.getBottom(); y += 7.0f * scale)
        g.fillRect (glass.getX(), y, glass.getWidth(), 1.0f * scale);

    // Faint "OVEN" watermark
    g.setColour (lime.withAlpha (0.045f));
    g.setFont (juce::Font (juce::FontOptions (150.0f * scale, juce::Font::bold | juce::Font::italic)));
    g.drawText ("OVEN", glass.toNearestInt(), juce::Justification::centred, false);

    //==========================================================================
    auto laneA = getLaneBounds (0);
    auto laneB = getLaneBounds (1);

    const bool cookOn = processor.apvts.getRawParameterValue ("cook_on")->load() > 0.5f;

    for (int loop = 0; loop < 2; ++loop)
    {
        const auto lane = loop == 0 ? laneA : laneB;
        const bool hasContent = processor.hasLoopContent (loop);
        const bool recording = processor.isLoopRecording (loop);
        const bool overdubbing = processor.isLoopOverdubbing (loop);

        // Lane background
        g.setColour (juce::Colour (0xff060906).withAlpha (0.75f));
        g.fillRoundedRectangle (lane, 4.0f * scale);

        if (recording)
        {
            g.setColour (juce::Colour (0xffd31c25).withAlpha (0.10f));
            g.fillRoundedRectangle (lane, 4.0f * scale);
        }

        // Eighth-note grid
        g.setColour (juce::Colours::white.withAlpha (0.045f));
        for (int i = 1; i < 8; ++i)
        {
            const float x = lane.getX() + lane.getWidth() * (float) i / 8.0f;
            g.fillRect (x, lane.getY() + 1.0f, 1.0f, lane.getHeight() - 2.0f);
        }

        auto waveArea = getWaveBounds (lane);

        const bool isActive = (int) std::lround (processor.apvts
                                                    .getRawParameterValue ("active_loop")->load() * 2.0f) == loop
                              || ! processor.hasLoopContent (1 - loop);

        const auto baseColour = loop == 0 ? lime : juce::Colour (0xff8fe6c8);

        const char* startKey = loop == 0 ? "a_start" : "b_start";
        const char* endKey   = loop == 0 ? "a_end"   : "b_end";
        const float regionStart = juce::jlimit (0.0f, 1.0f,
                                                processor.apvts.getRawParameterValue (startKey)->load());
        const float regionEnd = juce::jlimit (regionStart, 1.0f,
                                              processor.apvts.getRawParameterValue (endKey)->load());

        // Region shading: everything outside start / end is dimmed.
        {
            const float rx0 = waveArea.getX() + regionStart * waveArea.getWidth();
            const float rx1 = waveArea.getX() + regionEnd * waveArea.getWidth();

            g.setColour (juce::Colours::black.withAlpha (0.46f));

            if (rx0 > waveArea.getX())
                g.fillRect (juce::Rectangle<float> (waveArea.getX(), waveArea.getY(),
                                                    rx0 - waveArea.getX(), waveArea.getHeight()));

            if (rx1 < waveArea.getRight())
                g.fillRect (juce::Rectangle<float> (rx1, waveArea.getY(),
                                                    waveArea.getRight() - rx1, waveArea.getHeight()));

            // Start / end boundary lines and grab tabs.
            const float tabW = 4.0f * scale;
            const float tabH = 8.0f * scale;

            g.setColour (baseColour.withAlpha (0.55f));
            g.fillRect (rx0 - 0.5f * scale, waveArea.getY(), 1.0f * scale, waveArea.getHeight());
            g.fillRect (rx1 - 0.5f * scale, waveArea.getY(), 1.0f * scale, waveArea.getHeight());

            g.setColour (baseColour.withAlpha (0.85f));
            g.fillRoundedRectangle (rx0 - tabW * 0.5f, waveArea.getY(), tabW, tabH, 1.5f * scale);
            g.fillRoundedRectangle (rx1 - tabW * 0.5f, waveArea.getBottom() - tabH, tabW, tabH, 1.5f * scale);
        }

        if (hasContent)
        {
            juce::Path wave;
            const float mid = waveArea.getCentreY();
            const float half = waveArea.getHeight() * 0.5f - 2.0f * scale;

            for (int i = 0; i < peakCount; ++i)
            {
                const float x = waveArea.getX() + waveArea.getWidth() * (float) i / (float) (peakCount - 1);
                const float p = juce::jlimit (0.0f, 1.0f, peaks[(size_t) loop][(size_t) i]);
                const float shimmer = std::sin (x * 0.035f + shimmerTime * 3.4f) * 1.7f * scale;
                const float y = mid - p * half + shimmer;

                if (i == 0)
                    wave.startNewSubPath (x, y);
                else
                    wave.lineTo (x, y);
            }

            for (int i = peakCount - 1; i >= 0; --i)
            {
                const float x = waveArea.getX() + waveArea.getWidth() * (float) i / (float) (peakCount - 1);
                const float p = juce::jlimit (0.0f, 1.0f, peaks[(size_t) loop][(size_t) i]);
                const float shimmer = std::sin (x * 0.035f + shimmerTime * 3.4f) * 1.7f * scale;
                wave.lineTo (x, mid + p * half + shimmer);
            }

            wave.closeSubPath();

            juce::ColourGradient waveGrad (baseColour.withAlpha (0.85f), 0.0f, mid,
                                           baseColour.withAlpha (0.10f), 0.0f, waveArea.getBottom(), true);
            g.setGradientFill (waveGrad);
            g.fillPath (wave);

            g.setColour (baseColour.withAlpha (0.16f));
            g.strokePath (wave, juce::PathStrokeType (5.0f * scale,
                                                      juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));

            g.setColour (baseColour.withAlpha (isActive ? 0.95f : 0.45f));
            g.strokePath (wave, juce::PathStrokeType (1.3f * scale,
                                                      juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));

            // Playhead
            const float phase = juce::jlimit (0.0f, 1.0f, processor.getPlayheadPhase (loop));
            const float px = waveArea.getX() + phase * waveArea.getWidth();

            g.setColour (OvenLookAndFeel::cream().withAlpha (0.10f));
            g.fillRect (px - 2.5f * scale, waveArea.getY(), 5.0f * scale, waveArea.getHeight());

            g.setColour (OvenLookAndFeel::cream().withAlpha (isActive ? 0.8f : 0.3f));
            g.fillRect (px - 0.7f * scale, waveArea.getY(), 1.4f * scale, waveArea.getHeight());

            // Grain sparks
            if (cookOn)
            {
                juce::Random sparkRng ((juce::int64) (shimmerTime * 30.0f) + loop * 997);

                for (int i = 0; i < 14; ++i)
                {
                    const float sx = waveArea.getX() + sparkRng.nextFloat() * waveArea.getWidth();
                    const float sy = waveArea.getY() + sparkRng.nextFloat() * waveArea.getHeight();
                    const float size = (0.8f + sparkRng.nextFloat() * 1.8f) * scale;

                    g.setColour (baseColour.withAlpha (0.15f + sparkRng.nextFloat() * 0.5f));
                    g.fillEllipse (sx, sy, size, size);
                }
            }
        }
        else
        {
            g.setColour (OvenLookAndFeel::cream().withAlpha (0.16f));
            g.setFont (juce::Font (juce::FontOptions (12.0f * scale, juce::Font::bold)));
            g.drawText ("EMPTY - DROP A LOOP OR PRESS REC",
                        waveArea.toNearestInt(), juce::Justification::centred, false);
        }

        // Lane header
        g.setColour (baseColour.withAlpha (0.9f));
        g.setFont (juce::Font (juce::FontOptions (10.0f * scale, juce::Font::bold)));
        g.drawText ("SAMPLE " + juce::String (loop == 0 ? "A" : "B"),
                    lane.reduced (10.0f * scale, 0.0f).withTrimmedTop (3.0f * scale).toNearestInt(),
                    juce::Justification::topLeft, false);

        if (recording)
        {
            const float blink = 0.55f + 0.45f * std::sin (shimmerTime * 9.0f);
            g.setColour (juce::Colour (0xffff3b3b).withAlpha (blink));
            g.fillEllipse (lane.getRight() - 54.0f * scale, lane.getY() + 4.0f * scale,
                           5.0f * scale, 5.0f * scale);

            g.setFont (juce::Font (juce::FontOptions (10.0f * scale, juce::Font::bold)));
            g.drawText (overdubbing ? "DUB" : "REC",
                        lane.reduced (10.0f * scale, 0.0f).withTrimmedTop (3.0f * scale).toNearestInt(),
                        juce::Justification::topRight, false);

            g.setColour (juce::Colour (0xffd31c25).withAlpha (0.55f + 0.35f * blink));
            g.drawRoundedRectangle (lane, 4.0f * scale, 1.4f * scale);
        }
        else if (hasContent)
        {
            const int startPct = juce::roundToInt (regionStart * 100.0f);
            const int endPct   = juce::roundToInt (regionEnd * 100.0f);

            g.setColour (OvenLookAndFeel::cream().withAlpha (0.4f));
            g.setFont (juce::Font (juce::FontOptions (9.5f * scale)));
            g.drawText (juce::String (processor.getLoopLengthSeconds (loop), 2) + " s"
                          + "   " + juce::String (startPct) + "-" + juce::String (endPct) + "%",
                        lane.reduced (10.0f * scale, 0.0f).withTrimmedTop (3.0f * scale).toNearestInt(),
                        juce::Justification::topRight, false);
        }
    }

    // Heat haze at the bottom of the glass
    {
        juce::ColourGradient heat (juce::Colour (0x00101010), glass.getCentreX(), glass.getBottom() - 70.0f * scale,
                                   lime.withAlpha (0.05f), glass.getCentreX(), glass.getBottom(), false);
        g.setGradientFill (heat);
        g.fillRect (glass);
    }
}
