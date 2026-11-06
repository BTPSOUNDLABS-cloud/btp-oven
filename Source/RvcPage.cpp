#include "RvcPage.h"

namespace
{
    constexpr float kWidth  = 1148.0f;
    constexpr float kHeight = 692.0f;
    constexpr int   kPeakCount = 240;

    void computePeaks (const juce::AudioBuffer<float>& buffer, int numPoints,
                       std::vector<float>& out)
    {
        out.assign ((size_t) numPoints, 0.0f);

        const int n = buffer.getNumSamples();
        const int ch = juce::jmax (1, buffer.getNumChannels());

        if (n <= 0)
            return;

        const float* l = buffer.getReadPointer (0);
        const float* r = ch > 1 ? buffer.getReadPointer (1) : l;

        for (int p = 0; p < numPoints; ++p)
        {
            const int start = (int) ((double) p * n / numPoints);
            const int end   = (int) ((double) (p + 1) * n / numPoints);
            float peak = 0.0f;

            for (int i = start; i < end; ++i)
                peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));

            out[(size_t) p] = peak;
        }
    }
}

//==============================================================================
RvcPage::RvcPage (BtPOvenAudioProcessor& p)
    : processor (p),
      engine (RvcEngine::getInstance())
{
    setOpaque (true);

    formats.registerBasicFormats();

    // Source
    addAction ("USE A", { 20.0f, 40.0f, 80.0f, 28.0f }, [this] { useLoopAsSource (0); });
    addAction ("USE B", { 106.0f, 40.0f, 80.0f, 28.0f }, [this] { useLoopAsSource (1); });
    addAction ("USE C", { 192.0f, 40.0f, 80.0f, 28.0f }, [this] { useLoopAsSource (2); });
    addAction ("LOAD FILE", { 278.0f, 40.0f, 120.0f, 28.0f }, [this] { loadSourceFile(); });

    // Voice picker
    voiceBox = std::make_unique<juce::ComboBox> ("voice");
    voiceBox->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff101215));
    voiceBox->setColour (juce::ComboBox::textColourId, OvenLookAndFeel::lime());
    voiceBox->setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    voiceBox->setColour (juce::ComboBox::arrowColourId, OvenLookAndFeel::lime());
    addAndMakeVisible (*voiceBox);

    refreshVoices();

    addAction ("CONVERT", { 720.0f, 120.0f, 180.0f, 40.0f }, [this] { convert(); });
    addAction ("MAIN PAGE", { 998.0f, 10.0f, 138.0f, 28.0f },
               [this]
               {
                   if (onMainPage != nullptr)
                       onMainPage();
               });

    // Result
    addAction ("TO A", { 20.0f, 640.0f, 80.0f, 28.0f }, [this] { processor.sendVoiceToLoop (0); });
    addAction ("TO B", { 106.0f, 640.0f, 80.0f, 28.0f }, [this] { processor.sendVoiceToLoop (1); });
    addAction ("TO C", { 192.0f, 640.0f, 80.0f, 28.0f }, [this] { processor.sendVoiceToLoop (2); });
    addAction ("TRAIN & SAVE", { 278.0f, 640.0f, 160.0f, 28.0f },
               [this]
               {
                   if (! processor.hasVoiceResult())
                   {
                       processor.setStatusMessage ("NO VOICE TO CAPTURE YET");
                       return;
                   }

                   const auto f = btpai::saveTrainingCapture (processor.getVoiceResult(), 48000.0, "Vocals");
                   processor.setStatusMessage (f.existsAsFile()
                                               ? "CAPTURED VOCAL -> " + f.getFileName()
                                               : "CAPTURE FAILED");
               });

    startTimerHz (10);
}

RvcPage::~RvcPage()
{
    stopTimer();
}

//==============================================================================
juce::Rectangle<int> RvcPage::R (float x, float y, float w, float h) const
{
    return { juce::roundToInt (x * sc), juce::roundToInt (y * sc),
             juce::roundToInt (w * sc), juce::roundToInt (h * sc) };
}

juce::TextButton* RvcPage::addAction (const juce::String& text, juce::Rectangle<float> design,
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
    buttons.push_back ({ std::move (button), design });
    return raw;
}

//==============================================================================
void RvcPage::refreshVoices()
{
    voiceNames.clear();
    voiceFiles.clear();

    const auto dir = RvcEngine::getVoicesDir();

    if (dir.isDirectory())
    {
        auto files = dir.findChildFiles (juce::File::findFiles, false, "*.onnx");
        files.sort();

        for (const auto& f : files)
        {
            voiceFiles.add (f);
            voiceNames.add (f.getFileNameWithoutExtension());
        }
    }

    voiceBox->clear();
    voiceBox->addItemList (voiceNames.isEmpty() ? juce::StringArray ("NO VOICES FOUND") : voiceNames, 1);
    voiceBox->setSelectedItemIndex (0);

    // Select the currently-loaded voice if present.
    const auto current = engine.getModels().voice;

    for (int i = 0; i < voiceFiles.size(); ++i)
    {
        if (voiceFiles[i] == current)
            voiceBox->setSelectedItemIndex (i, juce::dontSendNotification);
    }
}

void RvcPage::useLoopAsSource (int loopIndex)
{
    juce::AudioBuffer<float> audio;
    double sr = 44100.0;

    if (! processor.getLoopAudio (loopIndex, audio, sr))
    {
        processor.setStatusMessage ("LOOP " + juce::String (loopIndex == 0 ? "A" : loopIndex == 1 ? "B" : "C")
                                    + " IS EMPTY");
        return;
    }

    sourceBuffer = audio;
    sourceRate = sr;
    processor.setStatusMessage ("SOURCE = LOOP " + juce::String (loopIndex == 0 ? "A" : loopIndex == 1 ? "B" : "C"));
    repaint();
}

void RvcPage::loadSourceFile()
{
    auto safe = juce::Component::SafePointer<RvcPage> (this);

    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose a vocal to convert",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectFiles,
                               [safe] (const juce::FileChooser& fc)
                               {
                                   if (safe == nullptr)
                                       return;

                                   const auto file = fc.getResult();

                                   if (! file.existsAsFile())
                                       return;

                                   std::unique_ptr<juce::AudioFormatReader> reader (
                                       safe->formats.createReaderFor (file));

                                   if (reader == nullptr)
                                       return;

                                   juce::AudioBuffer<float> audio ((int) reader->numChannels,
                                                                   (int) reader->lengthInSamples);
                                   reader->read (&audio, 0, (int) reader->lengthInSamples, 0, true, true);

                                   safe->sourceBuffer = audio;
                                   safe->sourceRate = reader->sampleRate;
                                   safe->processor.setStatusMessage ("SOURCE = " + file.getFileName());
                                   safe->repaint();
                               });
}

void RvcPage::convert()
{
    if (! RvcEngine::isCompiledIn())
    {
        processor.setStatusMessage ("RVC ENGINE NOT COMPILED INTO THIS BUILD");
        return;
    }

    if (voiceFiles.isEmpty())
    {
        processor.setStatusMessage ("NO VOICE MODELS FOUND - DROP A .onnx IN " +
                                     RvcEngine::getVoicesDir().getFullPathName());
        return;
    }

    if (sourceBuffer.getNumSamples() < 1024)
    {
        processor.setStatusMessage ("PICK A SOURCE VOCAL FIRST (USE A/B/C OR LOAD FILE)");
        return;
    }

    const int idx = juce::jlimit (0, voiceFiles.size() - 1, voiceBox->getSelectedItemIndex());
    engine.setVoice (voiceFiles[idx]);

    processor.startVoiceConversion (sourceBuffer, sourceRate);
}

//==============================================================================
void RvcPage::resized()
{
    sc = (float) getWidth() / kWidth;

    for (auto& b : buttons)
        b.button->setBounds (R (b.design.getX(), b.design.getY(),
                                b.design.getWidth(), b.design.getHeight()));

    voiceBox->setBounds (R (720.0f, 70.0f, 300.0f, 28.0f));

    sourcePanel = juce::Rectangle<float> (0.0f, 0.0f, 700.0f, 300.0f) * sc;
    voicePanel  = juce::Rectangle<float> (706.0f, 0.0f, 442.0f, 300.0f) * sc;
    resultPanel = juce::Rectangle<float> (0.0f, 306.0f, 1148.0f, 386.0f) * sc;
    sourceWaveArea = juce::Rectangle<float> (20.0f, 80.0f, 660.0f, 200.0f) * sc;
    resultWaveArea = juce::Rectangle<float> (20.0f, 340.0f, 1108.0f, 280.0f) * sc;
}

//==============================================================================
void RvcPage::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

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

    {
        juce::Random rng (0x7b1c);
        g.setColour (juce::Colours::white.withAlpha (0.012f));

        for (int i = 0; i < 800; ++i)
            g.fillRect ((float) rng.nextInt (juce::jmax (1, getWidth())),
                        (float) rng.nextInt (juce::jmax (1, getHeight())),
                        1.0f, 1.0f);
    }

    paintPanel (g, { 0.0f, 0.0f, 700.0f, 300.0f }, "SOURCE VOCAL", "PICK A LOOP OR LOAD A VOCAL TO CONVERT");
    paintPanel (g, { 706.0f, 0.0f, 442.0f, 300.0f }, "VOICE", "CHOOSE A TARGET VOICE");
    paintPanel (g, { 0.0f, 306.0f, 1148.0f, 386.0f }, "RESULT", "ROUTE THE CONVERTED VOICE INTO A LOOP");

    // Source waveform
    g.setColour (juce::Colour (0xff060906).withAlpha (0.75f));
    g.fillRoundedRectangle (sourceWaveArea, 4.0f * sc);
    g.setColour (juce::Colour (0xff262a30));
    g.drawRoundedRectangle (sourceWaveArea, 4.0f * sc, 1.0f * sc);

    if (sourceBuffer.getNumSamples() > 0)
    {
        paintWave (g, sourceWaveArea.reduced (6.0f * sc), sourceBuffer, OvenLookAndFeel::cream());

        g.setColour (OvenLookAndFeel::cream().withAlpha (0.4f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.0f, 8.5f * sc), juce::Font::bold)));
        g.drawText (juce::String ((double) sourceBuffer.getNumSamples() / sourceRate, 1) + " s",
                    R (20.0f, 284.0f, 300.0f, 14.0f), juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (OvenLookAndFeel::cream().withAlpha (0.18f));
        g.setFont (juce::Font (juce::FontOptions (12.0f * sc, juce::Font::bold)));
        g.drawText ("NO SOURCE", sourceWaveArea.toNearestInt(), juce::Justification::centred, false);
    }

    // Engine status
    {
        const auto r = voicePanel.reduced (14.0f * sc, 0.0f);
        const auto state = engine.getState();

        juce::Colour sc2 = OvenLookAndFeel::cream().withAlpha (0.6f);
        switch (state)
        {
            case RvcEngine::State::Ready:      sc2 = OvenLookAndFeel::lime(); break;
            case RvcEngine::State::Converting: sc2 = OvenLookAndFeel::ember(); break;
            case RvcEngine::State::Error:      sc2 = OvenLookAndFeel::blood(); break;
            default: break;
        }

        g.setColour (sc2.withAlpha (0.95f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (8.5f, 12.0f * sc), juce::Font::bold)));
        g.drawText (engine.getStatusText(),
                    juce::Rectangle<float> (r.getX(), r.getY() + 170.0f * sc, r.getWidth(), 50.0f * sc),
                    juce::Justification::topLeft, true);
    }

    // Result waveform
    g.setColour (juce::Colour (0xff060906).withAlpha (0.75f));
    g.fillRoundedRectangle (resultWaveArea, 4.0f * sc);
    g.setColour (juce::Colour (0xff262a30));
    g.drawRoundedRectangle (resultWaveArea, 4.0f * sc, 1.0f * sc);

    if (processor.hasVoiceResult())
    {
        paintWave (g, resultWaveArea.reduced (6.0f * sc), processor.getVoiceResult(), OvenLookAndFeel::lime());

        g.setColour (OvenLookAndFeel::cream().withAlpha (0.4f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.0f, 8.5f * sc), juce::Font::bold)));
        g.drawText (juce::String (processor.getVoiceSeconds(), 1) + " s",
                    R (20.0f, 624.0f, 300.0f, 14.0f), juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (OvenLookAndFeel::cream().withAlpha (0.18f));
        g.setFont (juce::Font (juce::FontOptions (12.0f * sc, juce::Font::bold)));
        g.drawText ("PRESS CONVERT", resultWaveArea.toNearestInt(), juce::Justification::centred, false);
    }
}

void RvcPage::paintWave (juce::Graphics& g, juce::Rectangle<float> area,
                         const juce::AudioBuffer<float>& buffer, const juce::Colour& colour)
{
    if (buffer.getNumSamples() <= 0 || area.isEmpty())
        return;

    std::vector<float> peaks;
    computePeaks (buffer, kPeakCount, peaks);

    const float mid = area.getCentreY();
    const float half = area.getHeight() * 0.5f;

    juce::Path wave;

    for (int i = 0; i < kPeakCount; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / (float) (kPeakCount - 1);
        const float p = juce::jlimit (0.0f, 1.0f, peaks[(size_t) i]);
        const float y = mid - p * half;

        if (i == 0)
            wave.startNewSubPath (x, y);
        else
            wave.lineTo (x, y);
    }

    for (int i = kPeakCount - 1; i >= 0; --i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / (float) (kPeakCount - 1);
        const float p = juce::jlimit (0.0f, 1.0f, peaks[(size_t) i]);
        wave.lineTo (x, mid + p * half);
    }

    wave.closeSubPath();

    juce::ColourGradient grad (colour.withAlpha (0.8f), 0.0f, mid,
                               colour.withAlpha (0.08f), 0.0f, area.getBottom(), true);
    g.setGradientFill (grad);
    g.fillPath (wave);

    g.setColour (colour.withAlpha (0.9f));
    g.strokePath (wave, juce::PathStrokeType (1.2f * sc));
}

void RvcPage::paintPanel (juce::Graphics& g, juce::Rectangle<float> design,
                          const juce::String& title, const juce::String& hint)
{
    const auto r = design * sc;
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
}

void RvcPage::timerCallback()
{
    shimmerTime += 1.0f / 10.0f;

    if (shimmerTime > 1000.0f)
        shimmerTime = 0.0f;

    repaint (voicePanel.toNearestInt());
    repaint (resultWaveArea.toNearestInt());
}
