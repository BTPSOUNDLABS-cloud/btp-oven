#include "StemPage.h"

namespace
{
    constexpr float kWidth  = 1148.0f;
    constexpr float kHeight = 692.0f;
    constexpr int   kPeakCount = 260;

    const char* kStemNames[4] = { "DRUMS", "BASS", "OTHER", "VOCALS" };
    const juce::Colour kStemColours[4] = {
        juce::Colour (0xffff6b2c),   // drums  (ember)
        juce::Colour (0xff8fe6c8),   // bass   (teal)
        juce::Colour (0xffc9b8ff),   // other  (violet)
        OvenLookAndFeel::lime()      // vocals (lime)
    };

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
StemPage::StemPage (BtPOvenAudioProcessor& p)
    : processor (p),
      engine (StemEngine::getInstance())
{
    setOpaque (true);

    formats.registerBasicFormats();

    // Source
    addAction ("USE A", { 20.0f, 40.0f, 90.0f, 30.0f }, [this] { useLoopAsSource (0); });
    addAction ("USE B", { 118.0f, 40.0f, 90.0f, 30.0f }, [this] { useLoopAsSource (1); });
    addAction ("USE C", { 216.0f, 40.0f, 90.0f, 30.0f }, [this] { useLoopAsSource (2); });
    addAction ("LOAD FILE", { 314.0f, 40.0f, 120.0f, 30.0f }, [this] { loadSourceFile(); });
    addAction ("SEPARATE", { 460.0f, 40.0f, 200.0f, 30.0f }, [this] { separate(); });

    // Engine
    addAction ("LOAD MODEL", { 720.0f, 70.0f, 130.0f, 30.0f },
               [this] { engine.startLoad(); });
    addAction ("MODEL FILE", { 858.0f, 70.0f, 150.0f, 30.0f },
               [this]
               {
                   auto safe = juce::Component::SafePointer<StemPage> (this);

                   juce::FileChooser chooser ("Choose the Demucs model .bin",
                                              engine.getModelFile(), "*.bin");

                   chooser.launchAsync (juce::FileBrowserComponent::openMode
                                             | juce::FileBrowserComponent::canSelectFiles,
                                         [safe] (const juce::FileChooser& fc)
                                         {
                                             if (safe == nullptr)
                                                 return;

                                             const auto f = fc.getResult();

                                             if (f.existsAsFile())
                                                 safe->engine.setModelFile (f);
                                         });
               });
    addAction ("MAIN PAGE", { 998.0f, 10.0f, 138.0f, 30.0f },
               [this]
               {
                   if (onMainPage != nullptr)
                       onMainPage();
               });

    // Stem rows
    const float rowY[4] = { 236.0f, 350.0f, 464.0f, 578.0f };

    for (int s = 0; s < 4; ++s)
    {
        addAction ("TO A", { 20.0f, rowY[s] + 66.0f, 80.0f, 26.0f },
                   [this, s] { processor.sendStemToLoop (s, 0); });
        addAction ("TO B", { 108.0f, rowY[s] + 66.0f, 80.0f, 26.0f },
                   [this, s] { processor.sendStemToLoop (s, 1); });
        addAction ("TO C", { 196.0f, rowY[s] + 66.0f, 80.0f, 26.0f },
                   [this, s] { processor.sendStemToLoop (s, 2); });
        addAction ("EXPORT", { 284.0f, rowY[s] + 66.0f, 100.0f, 26.0f },
                   [this, s] { exportStem (s); });
        addAction ("TRAIN", { 392.0f, rowY[s] + 66.0f, 80.0f, 26.0f },
                   [this, s]
                   {
                       const auto& stem = processor.getStemBuffer (s);

                       if (stem.getNumSamples() < 64)
                       {
                           processor.setStatusMessage ("NO STEM TO CAPTURE YET");
                           return;
                       }

                       const auto f = btpai::saveTrainingCapture (stem, 44100.0, "Stems", kStemNames[s]);
                       processor.setStatusMessage (f.existsAsFile()
                                                   ? "CAPTURED STEM -> " + juce::String (kStemNames[s])
                                                   : "CAPTURE FAILED");
                   });
    }

    startTimerHz (10);
}

StemPage::~StemPage()
{
    stopTimer();
}

//==============================================================================
juce::Rectangle<int> StemPage::R (float x, float y, float w, float h) const
{
    return { juce::roundToInt (x * sc), juce::roundToInt (y * sc),
             juce::roundToInt (w * sc), juce::roundToInt (h * sc) };
}

juce::TextButton* StemPage::addAction (const juce::String& text, juce::Rectangle<float> design,
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
void StemPage::useLoopAsSource (int loopIndex)
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

    processor.setStatusMessage ("SOURCE = LOOP " + juce::String (loopIndex == 0 ? "A" : loopIndex == 1 ? "B" : "C")
                                + "  /  " + juce::String ((double) audio.getNumSamples() / sr, 1) + " s");
    repaint();
}

void StemPage::loadSourceFile()
{
    auto safe = juce::Component::SafePointer<StemPage> (this);

    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose audio to separate",
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
                                   {
                                       safe->processor.setStatusMessage ("COULD NOT READ FILE");
                                       return;
                                   }

                                   juce::AudioBuffer<float> audio ((int) reader->numChannels,
                                                                   (int) reader->lengthInSamples);
                                   reader->read (&audio, 0, (int) reader->lengthInSamples, 0, true, true);

                                   safe->sourceBuffer = audio;
                                   safe->sourceRate = reader->sampleRate;

                                   safe->processor.setStatusMessage ("SOURCE = " + file.getFileName()
                                                                     + "  /  " + juce::String (safe->sourceRate / 1000.0, 1) + " kHz");
                                   safe->repaint();
                               });
}

void StemPage::separate()
{
    if (! StemEngine::isCompiledIn())
    {
        processor.setStatusMessage ("STEM ENGINE NOT COMPILED INTO THIS BUILD");
        return;
    }

    if (sourceBuffer.getNumSamples() < 1024)
    {
        processor.setStatusMessage ("PICK A SOURCE FIRST (USE A/B/C OR LOAD FILE)");
        return;
    }

    processor.startSeparation (sourceBuffer, sourceRate);
}

void StemPage::exportStem (int stemIndex)
{
    const auto& stem = processor.getStemBuffer (stemIndex);

    if (stem.getNumSamples() < 64)
    {
        processor.setStatusMessage ("NO STEM TO EXPORT YET");
        return;
    }

    auto safe = juce::Component::SafePointer<StemPage> (this);

    fileChooser = std::make_unique<juce::FileChooser> (
        "Export " + juce::String (kStemNames[stemIndex]),
        juce::File::getSpecialLocation (juce::File::userMusicDirectory)
            .getChildFile (juce::String ("BtP_Oven_") + kStemNames[stemIndex] + ".wav"),
        "*.wav");

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                   | juce::FileBrowserComponent::canSelectFiles
                                   | juce::FileBrowserComponent::warnAboutOverwriting,
                               [safe, stemIndex] (const juce::FileChooser& fc)
                               {
                                   if (safe == nullptr)
                                       return;

                                   auto file = fc.getResult();

                                   if (file.getFullPathName().isEmpty())
                                       return;

                                   if (! file.hasFileExtension ("wav"))
                                       file = file.withFileExtension ("wav");

                                   std::unique_ptr<juce::FileOutputStream> out (file.createOutputStream());

                                   if (out == nullptr)
                                       return;

                                   const auto& stem = safe->processor.getStemBuffer (stemIndex);

                                   juce::WavAudioFormat wav;
                                   std::unique_ptr<juce::AudioFormatWriter> writer (
                                       wav.createWriterFor (out.get(), 44100.0,
                                                            (unsigned int) stem.getNumChannels(),
                                                            16, {}, 0));

                                   if (writer != nullptr)
                                   {
                                       out.release();   // the writer owns the stream now
                                       writer->writeFromAudioSampleBuffer (stem, 0, stem.getNumSamples());
                                       writer->flush();
                                   }
                                   safe->processor.setStatusMessage ("EXPORTED " + file.getFileName());
                               });
}

//==============================================================================
void StemPage::resized()
{
    sc = (float) getWidth() / kWidth;

    for (auto& b : buttons)
        b.button->setBounds (R (b.design.getX(), b.design.getY(),
                                b.design.getWidth(), b.design.getHeight()));

    sourcePanel  = juce::Rectangle<float> (0.0f, 0.0f, 700.0f, 196.0f) * sc;
    enginePanel  = juce::Rectangle<float> (706.0f, 0.0f, 442.0f, 196.0f) * sc;
    stemsPanel   = juce::Rectangle<float> (0.0f, 202.0f, 1148.0f, 490.0f) * sc;
    sourceWaveArea = juce::Rectangle<float> (20.0f, 96.0f, 640.0f, 84.0f) * sc;
}

//==============================================================================
void StemPage::paint (juce::Graphics& g)
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
        juce::Random rng (0x57e1);
        g.setColour (juce::Colours::white.withAlpha (0.012f));

        for (int i = 0; i < 800; ++i)
            g.fillRect ((float) rng.nextInt (juce::jmax (1, getWidth())),
                        (float) rng.nextInt (juce::jmax (1, getHeight())),
                        1.0f, 1.0f);
    }

    paintPanel (g, { 0.0f, 0.0f, 700.0f, 196.0f }, "SOURCE", "PICK A LOOP OR LOAD A TRACK TO SPLIT");
    paintPanel (g, { 706.0f, 0.0f, 442.0f, 196.0f }, "STEM ENGINE", "DEMUCS V4 - IN-PROCESS / NO PYTHON");
    paintPanel (g, { 0.0f, 202.0f, 1148.0f, 490.0f }, "STEMS", "VOCALS / DRUMS / BASS / OTHER  -  ROUTE INTO LOOPS OR EXPORT");

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
        g.drawText (juce::String ((double) sourceBuffer.getNumSamples() / sourceRate, 1) + " s  @  "
                    + juce::String (sourceRate / 1000.0, 1) + " kHz",
                    R (20.0f, 182.0f, 300.0f, 14.0f), juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (OvenLookAndFeel::cream().withAlpha (0.18f));
        g.setFont (juce::Font (juce::FontOptions (12.0f * sc, juce::Font::bold)));
        g.drawText ("NO SOURCE", sourceWaveArea.toNearestInt(), juce::Justification::centred, false);
    }

    // Engine status
    {
        const auto r = enginePanel.reduced (14.0f * sc, 0.0f);
        const auto state = engine.getState();

        juce::Colour sc2 = OvenLookAndFeel::cream().withAlpha (0.6f);

        switch (state)
        {
            case StemEngine::State::Ready:       sc2 = OvenLookAndFeel::lime(); break;
            case StemEngine::State::Separating:  sc2 = OvenLookAndFeel::ember(); break;
            case StemEngine::State::Error:       sc2 = OvenLookAndFeel::blood(); break;
            default: break;
        }

        g.setColour (sc2.withAlpha (0.95f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (8.5f, 12.0f * sc), juce::Font::bold)));
        g.drawText (engine.getStatusText(),
                    juce::Rectangle<float> (r.getX(), r.getY() + 28.0f * sc, r.getWidth(), 30.0f * sc),
                    juce::Justification::centredLeft, true);

        // Progress bar
        if (state == StemEngine::State::Separating)
        {
            const auto bar = juce::Rectangle<float> (r.getX(), r.getY() + 62.0f * sc, r.getWidth(), 12.0f * sc);
            g.setColour (juce::Colour (0xff2a0a0c));
            g.fillRoundedRectangle (bar, 3.0f * sc);

            auto filled = bar;
            filled.setWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, engine.getProgress()));

            g.setColour (OvenLookAndFeel::ember().brighter (0.2f));
            g.fillRoundedRectangle (filled, 3.0f * sc);
        }

        g.setColour (OvenLookAndFeel::cream().withAlpha (0.35f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (6.5f, 7.5f * sc), juce::Font::bold)));
        g.drawText (engine.getModelFile().getFullPathName(),
                    juce::Rectangle<float> (r.getX(), r.getY() + 80.0f * sc, r.getWidth(), 26.0f * sc),
                    juce::Justification::topLeft, true);
    }

    // Stem rows
    if (processor.hasStemResult())
        for (int s = 0; s < 4; ++s)
            paintStemRow (g, s);
    else
    {
        g.setColour (OvenLookAndFeel::cream().withAlpha (0.16f));
        g.setFont (juce::Font (juce::FontOptions (14.0f * sc, juce::Font::bold)));
        g.drawText ("SEPARATE A SOURCE TO SEE THE 4 STEMS",
                    stemsPanel.toNearestInt(), juce::Justification::centred, false);
    }
}

void StemPage::paintStemRow (juce::Graphics& g, int stemIndex)
{
    static const float rowY[4] = { 236.0f, 350.0f, 464.0f, 578.0f };

    const auto r = juce::Rectangle<float> (20.0f, rowY[stemIndex], 1108.0f, 62.0f) * sc;
    const auto colour = kStemColours[stemIndex];

    g.setColour (juce::Colour (0xff060906).withAlpha (0.75f));
    g.fillRoundedRectangle (r, 4.0f * sc);
    g.setColour (colour.withAlpha (0.35f));
    g.drawRoundedRectangle (r, 4.0f * sc, 1.0f * sc);

    g.setColour (colour.withAlpha (0.95f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (8.5f, 11.0f * sc), juce::Font::bold)));
    g.drawText (kStemNames[stemIndex],
                juce::Rectangle<float> (r.getX() + 10.0f * sc, r.getY(), 90.0f * sc, r.getHeight()),
                juce::Justification::centredLeft, false);

    const auto waveArea = juce::Rectangle<float> (r.getX() + 100.0f * sc, r.getY() + 8.0f * sc,
                                                  r.getWidth() - 120.0f * sc, r.getHeight() - 16.0f * sc);
    paintWave (g, waveArea, processor.getStemBuffer (stemIndex), colour);
}

void StemPage::paintWave (juce::Graphics& g, juce::Rectangle<float> area,
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

void StemPage::paintPanel (juce::Graphics& g, juce::Rectangle<float> design,
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

void StemPage::timerCallback()
{
    shimmerTime += 1.0f / 10.0f;

    if (shimmerTime > 1000.0f)
        shimmerTime = 0.0f;

    repaint (enginePanel.toNearestInt());
    repaint (stemsPanel.toNearestInt());
}
