#include "OneShotPage.h"

namespace
{
    constexpr float kWidth  = 1148.0f;
    constexpr float kHeight = 692.0f;
    constexpr int   kPeakCount = 240;

    juce::File settingsFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("BtP Oven")
                 .getChildFile ("oneshot_settings.json");
    }

    juce::File defaultLibraryRoot()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                 .getChildFile ("BtP OneShots");
    }

    juce::String slugify (const juce::String& text)
    {
        auto s = text.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ")
                    .trim()
                    .replace (" ", "_");

        if (s.length() > 40)
            s = s.substring (0, 40);

        if (s.isEmpty())
            s = "oneshot";

        return s;
    }

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

const juce::StringArray& OneShotPage::categories()
{
    static const juce::StringArray cats {
        "FX", "Impacts", "Risers", "Pads", "Vocal Chops", "Sirens",
        "Phones", "Cash", "Ambience", "Synth One Shots"
    };
    return cats;
}

//==============================================================================
OneShotPage::OneShotPage (BtPOvenAudioProcessor& p)
    : processor (p),
      engine (AiEngine::getInstance())
{
    setOpaque (true);

    // Persisted library root
    libraryRoot = defaultLibraryRoot();

    const juce::File cfg = settingsFile();

    if (cfg.existsAsFile())
    {
        const auto json = juce::JSON::parse (cfg);

        if (auto* obj = json.getDynamicObject())
        {
            const auto path = obj->getProperty ("library_root").toString();
            if (path.isNotEmpty())
                libraryRoot = juce::File (path);
        }
    }

    // Generate panel
    promptEditor = std::make_unique<juce::TextEditor> ("oneshot_prompt");
    promptEditor->setMultiLine (true, false);
    promptEditor->setReturnKeyStartsNewLine (true);
    promptEditor->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff080a08));
    promptEditor->setColour (juce::TextEditor::textColourId, OvenLookAndFeel::cream());
    promptEditor->setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff2a2e33));
    promptEditor->setColour (juce::TextEditor::focusedOutlineColourId, OvenLookAndFeel::lime());
    promptEditor->setFont (juce::Font (juce::FontOptions (14.0f)));
    promptEditor->setTextToShowWhenEmpty ("dark 808 sub drop / airy pad / riser sweep / cash foley...",
                                          OvenLookAndFeel::lime().withAlpha (0.45f));
    addAndMakeVisible (*promptEditor);

    categoryBox = std::make_unique<juce::ComboBox> ("category");
    categoryBox->addItemList (categories(), 1);
    categoryBox->setSelectedItemIndex (0);
    categoryBox->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff101215));
    categoryBox->setColour (juce::ComboBox::textColourId, OvenLookAndFeel::lime());
    categoryBox->setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    categoryBox->setColour (juce::ComboBox::arrowColourId, OvenLookAndFeel::lime());
    addAndMakeVisible (*categoryBox);

    durationSlider = addControlKnob ("SECS", { 264.0f, 94.0f, 84.0f, 78.0f });
    durationSlider->setRange (3.0, 20.0, 0.5);
    durationSlider->setValue (5.0);
    durationSlider->setTooltip ("One-shot length in seconds");

    generateButton = addAction ("GENERATE", { 360.0f, 104.0f, 150.0f, 34.0f },
                                [this] { generate(); });

    // Preview panel
    addAction ("TO A", { 14.0f, 372.0f, 80.0f, 28.0f }, [this] { processor.sendOneShotToLoop (0); });
    addAction ("TO B", { 102.0f, 372.0f, 80.0f, 28.0f }, [this] { processor.sendOneShotToLoop (1); });
    addAction ("TO C", { 190.0f, 372.0f, 80.0f, 28.0f }, [this] { processor.sendOneShotToLoop (2); });
    addAction ("SAVE", { 278.0f, 372.0f, 120.0f, 28.0f }, [this] { saveLastOneShot(); });
    addAction ("TRAIN", { 406.0f, 372.0f, 80.0f, 28.0f },
               [this]
               {
                   if (! processor.hasOneShot())
                   {
                       processor.setStatusMessage ("NO ONE-SHOT TO CAPTURE YET");
                       return;
                   }

                   const int idx = categoryBox->getSelectedItemIndex();
                   const juce::String cat = (idx >= 0 && idx < categories().size()) ? categories()[idx] : "FX";
                   const auto f = btpai::saveTrainingCapture (processor.getOneShot(), 48000.0,
                                                              "OneShots", cat, processor.getOneShotPrompt());
                   processor.setStatusMessage (f.existsAsFile()
                                               ? "CAPTURED ONE-SHOT -> " + cat
                                               : "CAPTURE FAILED");
               });

    // RAVE variant section
    variationSlider = addControlKnob ("VAR", { 130.0f, 404.0f, 60.0f, 56.0f });
    variationSlider->setRange (0.0, 1.0, 0.05);
    variationSlider->setValue (0.4);
    variationSlider->setTooltip ("RAVE mutation amount");

    addAction ("MUTATE", { 14.0f, 410.0f, 110.0f, 30.0f },
               [this] { processor.mutateLastOneShot ((float) variationSlider->getValue()); });
    addAction ("SAVE VAR", { 486.0f, 410.0f, 60.0f, 30.0f }, [this] { saveRaveVariant(); });
    addAction ("TRAIN VAR", { 552.0f, 410.0f, 66.0f, 30.0f },
               [this]
               {
                   if (! processor.hasRaveResult())
                   {
                       processor.setStatusMessage ("NO VARIANT TO CAPTURE YET");
                       return;
                   }

                   const auto f = btpai::saveTrainingCapture (processor.getRaveResult(), 48000.0,
                                                              "Variants", {}, processor.getOneShotPrompt());
                   processor.setStatusMessage (f.existsAsFile() ? "CAPTURED VARIANT" : "CAPTURE FAILED");
               });
    addAction ("TO C", { 624.0f, 410.0f, 62.0f, 30.0f }, [this] { processor.sendRaveToLoop (2); });

    // Library panel
    addAction ("CHOOSE FOLDER", { 720.0f, 40.0f, 150.0f, 28.0f }, [this] { chooseLibraryRoot(); });
    addAction ("REFRESH", { 876.0f, 40.0f, 84.0f, 28.0f }, [this] { refreshLibrary(); });
    addAction ("OPEN", { 966.0f, 40.0f, 70.0f, 28.0f }, [this] { openLibraryFolder(); });
    addAction ("MAIN PAGE", { 998.0f, 10.0f, 138.0f, 28.0f },
               [this]
               {
                   if (onMainPage != nullptr)
                       onMainPage();
               });

    libraryList = std::make_unique<juce::ListBox> ("library");
    libraryList->setModel (&libraryModel);
    libraryList->setRowHeight (24);
    libraryList->setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff080a08));
    libraryList->setColour (juce::ListBox::outlineColourId, juce::Colour (0xff2a2e33));
    addAndMakeVisible (*libraryList);

    refreshLibrary();

    startTimerHz (10);
}

OneShotPage::~OneShotPage()
{
    stopTimer();
}

//==============================================================================
juce::Rectangle<int> OneShotPage::R (float x, float y, float w, float h) const
{
    return { juce::roundToInt (x * sc), juce::roundToInt (y * sc),
             juce::roundToInt (w * sc), juce::roundToInt (h * sc) };
}

juce::TextButton* OneShotPage::addAction (const juce::String& text, juce::Rectangle<float> design,
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

juce::Slider* OneShotPage::addControlKnob (const juce::String& name, juce::Rectangle<float> design)
{
    auto knob = std::make_unique<ControlKnob>();
    knob->design = design;

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

    auto* raw = knob->slider.get();
    knobs.push_back (std::move (knob));
    return raw;
}

//==============================================================================
void OneShotPage::generate()
{
    if (! AiEngine::isCompiledIn())
    {
        processor.setStatusMessage ("AI ENGINE NOT COMPILED INTO THIS BUILD");
        return;
    }

    AiEngine::Job job;
    job.prompt   = promptEditor->getText();
    job.duration = (float) durationSlider->getValue();
    job.steps    = 8;
    job.guidance = 1.0f;
    job.shift    = 3.0f;
    job.bpm      = 0;
    job.seed     = -1;
    job.kind     = AiEngine::Kind::OneShot;

    const int root  = (int) std::lround (processor.apvts.getRawParameterValue ("key_root")->load());
    const int scale = (int) std::lround (processor.apvts.getRawParameterValue ("scale_type")->load());
    job.keyscale = juce::String (btp::noteName (root)) + (scale == 0 ? " major" : " minor");

    engine.submit (job);
    processor.setStatusMessage ("ONE-SHOT GENERATING...");
}

void OneShotPage::saveLastOneShot()
{
    if (! processor.hasOneShot())
    {
        processor.setStatusMessage ("NO ONE-SHOT TO SAVE YET");
        return;
    }

    const auto& audio = processor.getOneShot();
    const int idx = categoryBox->getSelectedItemIndex();
    const juce::String cat = (idx >= 0 && idx < categories().size()) ? categories()[idx] : "FX";

    const auto dir = libraryRoot.getChildFile (cat);
    dir.createDirectory();

    const juce::String stamp = juce::String (juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S"));
    juce::File file = dir.getChildFile (slugify (processor.getOneShotPrompt()) + "_" + stamp + ".wav");

    if (file.existsAsFile())
        file = dir.getChildFile (slugify (processor.getOneShotPrompt()) + "_" + stamp + "_2.wav");

    std::unique_ptr<juce::FileOutputStream> out (file.createOutputStream());

    if (out == nullptr)
    {
        processor.setStatusMessage ("COULD NOT WRITE TO LIBRARY");
        return;
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (out.get(), 48000.0, (unsigned int) audio.getNumChannels(), 16, {}, 0));

    if (writer != nullptr)
    {
        out.release();   // the writer owns the stream now
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        writer->flush();
    }

    out->flush();

    refreshLibrary();
    processor.setStatusMessage ("SAVED  " + file.getFileName() + "  ->  " + cat);
}

void OneShotPage::saveRaveVariant()
{
    if (! processor.hasRaveResult())
    {
        processor.setStatusMessage ("NO VARIANT TO SAVE YET");
        return;
    }

    const auto& audio = processor.getRaveResult();
    const int idx = categoryBox->getSelectedItemIndex();
    const juce::String cat = (idx >= 0 && idx < categories().size()) ? categories()[idx] : "FX";

    const auto dir = libraryRoot.getChildFile (cat);
    dir.createDirectory();

    const juce::String stamp = juce::String (juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S"));
    juce::File file = dir.getChildFile (slugify (processor.getOneShotPrompt()) + "_var_" + stamp + ".wav");

    std::unique_ptr<juce::FileOutputStream> out (file.createOutputStream());

    if (out == nullptr)
    {
        processor.setStatusMessage ("COULD NOT WRITE TO LIBRARY");
        return;
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (out.get(), 48000.0, (unsigned int) audio.getNumChannels(), 16, {}, 0));

    if (writer != nullptr)
    {
        out.release();   // the writer owns the stream now
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        writer->flush();
    }

    out->flush();

    refreshLibrary();
    processor.setStatusMessage ("SAVED VARIANT  " + file.getFileName());
}

void OneShotPage::chooseLibraryRoot()
{
    auto safe = juce::Component::SafePointer<OneShotPage> (this);

    juce::FileChooser chooser ("Choose your one-shot library folder", libraryRoot, "*");

    chooser.launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr)
                                  return;

                              const auto dir = fc.getResult();

                              if (! dir.isDirectory())
                                  return;

                              safe->libraryRoot = dir;

                              auto* obj = new juce::DynamicObject();
                              obj->setProperty ("library_root", dir.getFullPathName());
                              settingsFile().getParentDirectory().createDirectory();
                              settingsFile().replaceWithText (juce::JSON::toString (juce::var (obj), true));

                              safe->refreshLibrary();
                          });
}

void OneShotPage::refreshLibrary()
{
    libraryFiles.clear();

    if (libraryRoot.isDirectory())
    {
        auto files = libraryRoot.findChildFiles (juce::File::findFiles, true, "*.wav");
        files.sort();

        for (auto& f : files)
            libraryFiles.add (f);
    }

    libraryList->updateContent();
}

void OneShotPage::openLibraryFolder()
{
    if (libraryRoot.isDirectory())
        libraryRoot.revealToUser();
}

void OneShotPage::loadLibraryFile (const juce::File& file)
{
    if (processor.loadFileIntoLoop (2, file))
        processor.setStatusMessage ("LOADED " + file.getFileName() + "  ->  LOOP C");
}

//==============================================================================
void OneShotPage::resized()
{
    sc = (float) getWidth() / kWidth;

    const auto labelFont = juce::Font (juce::FontOptions (juce::jmax (7.5f, 10.0f * sc), juce::Font::bold));

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

    for (auto& b : buttons)
        b.button->setBounds (R (b.design.getX(), b.design.getY(),
                                b.design.getWidth(), b.design.getHeight()));

    promptEditor->setBounds (R (14.0f, 32.0f, 672.0f, 62.0f));
    categoryBox->setBounds (R (14.0f, 104.0f, 240.0f, 28.0f));
    libraryList->setBounds (R (720.0f, 76.0f, 410.0f, 592.0f));

    generatePanel = juce::Rectangle<float> (0.0f, 0.0f, 700.0f, 200.0f) * sc;
    previewPanel  = juce::Rectangle<float> (0.0f, 206.0f, 700.0f, 486.0f) * sc;
    libraryPanel  = juce::Rectangle<float> (706.0f, 0.0f, 442.0f, 692.0f) * sc;
    previewWaveArea = juce::Rectangle<float> (14.0f, 240.0f, 672.0f, 112.0f) * sc;
    variantWaveArea = juce::Rectangle<float> (200.0f, 410.0f, 280.0f, 30.0f) * sc;
}

//==============================================================================
void OneShotPage::paint (juce::Graphics& g)
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
        juce::Random rng (0x5a11);
        g.setColour (juce::Colours::white.withAlpha (0.012f));

        for (int i = 0; i < 800; ++i)
            g.fillRect ((float) rng.nextInt (juce::jmax (1, getWidth())),
                        (float) rng.nextInt (juce::jmax (1, getHeight())),
                        1.0f, 1.0f);
    }

    paintPanel (g, { 0.0f, 0.0f, 700.0f, 200.0f }, "GENERATE ONE-SHOT", "DESCRIBE THE SOUND - TEXT -> AUDIO");
    paintPanel (g, { 0.0f, 206.0f, 700.0f, 486.0f }, "LAST ONE-SHOT", "ROUTE TO A LOOP TO PLAY IT THROUGH THE FX RACK");
    paintPanel (g, { 706.0f, 0.0f, 442.0f, 692.0f }, "LIBRARY", "YOUR ORGANISED ONE-SHOT FOLDER");

    // Preview waveform
    g.setColour (juce::Colour (0xff060906).withAlpha (0.75f));
    g.fillRoundedRectangle (previewWaveArea, 4.0f * sc);
    g.setColour (juce::Colour (0xff262a30));
    g.drawRoundedRectangle (previewWaveArea, 4.0f * sc, 1.0f * sc);

    if (processor.hasOneShot())
    {
        paintWave (g, previewWaveArea.reduced (6.0f * sc), processor.getOneShot(), OvenLookAndFeel::lime());

        g.setColour (OvenLookAndFeel::cream().withAlpha (0.45f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.0f, 8.5f * sc), juce::Font::bold)));
        g.drawText (juce::String (processor.getOneShotSeconds(), 1) + " s  /  "
                    + processor.getOneShotPrompt().substring (0, 48),
                    R (14.0f, 358.0f, 400.0f, 12.0f), juce::Justification::centredLeft, true);
    }
    else
    {
        g.setColour (OvenLookAndFeel::cream().withAlpha (0.18f));
        g.setFont (juce::Font (juce::FontOptions (12.0f * sc, juce::Font::bold)));
        g.drawText ("PRESS GENERATE", previewWaveArea.toNearestInt(), juce::Justification::centred, false);
    }

    // Variant waveform (RAVE)
    g.setColour (juce::Colour (0xff060906).withAlpha (0.75f));
    g.fillRoundedRectangle (variantWaveArea, 3.0f * sc);
    g.setColour (juce::Colour (0xff262a30));
    g.drawRoundedRectangle (variantWaveArea, 3.0f * sc, 1.0f * sc);

    if (processor.hasRaveResult())
        paintWave (g, variantWaveArea.reduced (4.0f * sc), processor.getRaveResult(), OvenLookAndFeel::ember());
    else
    {
        g.setColour (OvenLookAndFeel::cream().withAlpha (0.18f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (6.5f, 8.0f * sc), juce::Font::bold)));
        g.drawText ("RAVE VARIANT", variantWaveArea.toNearestInt(), juce::Justification::centred, false);
    }

    // Engine state hint
    const auto state = engine.getState();
    juce::Colour sc2 = OvenLookAndFeel::cream().withAlpha (0.5f);
    switch (state)
    {
        case AiEngine::State::Ready:      sc2 = OvenLookAndFeel::lime(); break;
        case AiEngine::State::Generating: sc2 = OvenLookAndFeel::ember(); break;
        case AiEngine::State::Error:      sc2 = OvenLookAndFeel::blood(); break;
        default: break;
    }

    g.setColour (sc2.withAlpha (0.8f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.5f, 9.0f * sc), juce::Font::bold)));
    g.drawText (engine.getStatusText(), R (14.0f, 154.0f, 660.0f, 16.0f),
                juce::Justification::centredLeft, true);

    // Library root path
    g.setColour (OvenLookAndFeel::cream().withAlpha (0.35f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (6.5f, 7.5f * sc), juce::Font::bold)));
    g.drawText (juce::String (libraryFiles.size()) + " FILES  /  " + libraryRoot.getFullPathName(),
                R (720.0f, 664.0f, 410.0f, 16.0f), juce::Justification::centredLeft, true);
}

void OneShotPage::paintWave (juce::Graphics& g, juce::Rectangle<float> area,
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

void OneShotPage::paintPanel (juce::Graphics& g, juce::Rectangle<float> design,
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

void OneShotPage::timerCallback()
{
    shimmerTime += 1.0f / 10.0f;

    if (shimmerTime > 1000.0f)
        shimmerTime = 0.0f;

    const bool busy = engine.isBusy();

    if (generateButton != nullptr)
    {
        generateButton->setEnabled (! busy);
        generateButton->setButtonText (busy ? "COOKING..." : "GENERATE");
    }

    repaint (generatePanel.toNearestInt());
    repaint (previewWaveArea.toNearestInt());
    repaint (variantWaveArea.toNearestInt());
}

//==============================================================================
int OneShotPage::LibraryModel::getNumRows()
{
    return owner.libraryFiles.size();
}

void OneShotPage::LibraryModel::paintListBoxItem (int row, juce::Graphics& g, int width, int height,
                                                  bool rowIsSelected)
{
    if (row < 0 || row >= owner.libraryFiles.size())
        return;

    const auto& f = owner.libraryFiles.getReference (row);

    if (rowIsSelected)
    {
        g.setColour (OvenLookAndFeel::lime().withAlpha (0.14f));
        g.fillAll();
    }

    juce::String rel = f.getRelativePathFrom (owner.libraryRoot);

    g.setColour (OvenLookAndFeel::lime().withAlpha (rowIsSelected ? 0.95f : 0.75f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (8.0f, (float) height * 0.4f), juce::Font::bold)));
    g.drawText (rel, 8, 0, width - 16, height, juce::Justification::centredLeft, true);
}

void OneShotPage::LibraryModel::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= owner.libraryFiles.size())
        return;

    owner.loadLibraryFile (owner.libraryFiles.getReference (row));
}
