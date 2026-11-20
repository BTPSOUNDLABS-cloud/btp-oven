#include "ChainPage.h"

#include <thread>

ChainPage::ChainPage (Mode pageMode, AudioCapture& audioCapture, EngineService& engineService)
    : mode (pageMode), service (engineService), capture (audioCapture)
{
    addAndMakeVisible (genreBox);
    addAndMakeVisible (subGenreBox);
    addAndMakeVisible (profileBox);
    addAndMakeVisible (toneSlider);
    addAndMakeVisible (toneLabel);
    addAndMakeVisible (buildButton);
    addAndMakeVisible (healthButton);
    addAndMakeVisible (backButton);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (meter);
    addAndMakeVisible (dragOut);

    if (mode == Mode::Master)
    {
        genreBox.addItemList ({ "Rap", "Hip Hop", "Trap", "Drill", "R&B", "Pop" }, 1);
        genreBox.setSelectedId (3);
        subGenreBox.addItemList ({ "Dark", "Melodic", "Hard", "Aggressive" }, 1);
        subGenreBox.setSelectedId (1);
    }
    else
    {
        genreBox.setVisible (false);
        subGenreBox.setVisible (false);
    }

    updateProfileChoices();
    genreBox.onChange = [this] { updateProfileChoices(); };
    subGenreBox.onChange = [this] { updateProfileChoices(); };

    toneSlider.setRange (0, 10, 1);
    toneSlider.setValue (6, juce::dontSendNotification);
    toneSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    toneSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 20);

    toneLabel.setText ("tone", juce::dontSendNotification);
    toneLabel.setFont (juce::Font (juce::FontOptions (11.0f)));

    statusLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    statusLabel.setText ("start the engine: python -m voxchain serve",
                         juce::dontSendNotification);

    buildButton.setColour (juce::TextButton::buttonColourId, accent);
    buildButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    buildButton.onClick = [this] { build(); };

    healthButton.onClick = [this]
    {
        healthButton.setEnabled (false);
        statusLabel.setText ("checking engine...", juce::dontSendNotification);
        juce::Component::SafePointer<ChainPage> safe (this);

        service.health (3000, [safe] (const EngineReply& reply)
        {
            if (auto* page = safe.getComponent())
            {
                page->healthButton.setEnabled (true);
                if (reply.ok)
                    page->statusLabel.setText ("engine "
                                                   + reply.data.getProperty ("version", "").toString()
                                                   + " ready",
                                               juce::dontSendNotification);
                else
                    page->statusLabel.setText (reply.describe(), juce::dontSendNotification);
            }
        });
    };

    backButton.onClick = [this]
    {
        if (onMainPage) onMainPage();
    };

    dragOut.getFilePath = [this] { return lastFstPath; };
}

ChainPage::~ChainPage()
{
    // Every request runs on a detached thread guarded by a SafePointer, so
    // there is nothing to join here and nothing can outlive the component.
}

void ChainPage::setPalette (juce::Colour backgroundColour, juce::Colour panelColour,
                            juce::Colour textColour, juce::Colour accentColour,
                            juce::Colour goodColour)
{
    background = backgroundColour;
    panel = panelColour;
    text = textColour;
    accent = accentColour;
    good = goodColour;

    for (auto* label : { &toneLabel, &statusLabel })
        label->setColour (juce::Label::textColourId, text.withAlpha (0.75f));

    buildButton.setColour (juce::TextButton::buttonColourId, accent);
    buildButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);

    meter.setColours (accent, panel, text, panel.brighter (0.08f));
    dragOut.accent = accent;
    dragOut.text = text;
    dragOut.repaint();
    repaint();
}

void ChainPage::paint (juce::Graphics& g)
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    g.fillAll (background);

    // Panel first: drawing it after the text paints over our own labels.
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (4.0f), 8.0f);

    const int textWidth = juce::jmax (0, getWidth() - 120);

    g.setColour (text);
    g.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    g.drawText (mode == Mode::Master ? "MASTER CHAIN" : "VOCAL CHAIN",
                10, 8, textWidth, 22, juce::Justification::centredLeft);

    g.setColour (text.withAlpha (0.5f));
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText (mode == Mode::Master
                    ? "reads the insert, proposes a master chain, exports a preset"
                    : "reads the vocal, proposes a chain, exports a preset",
                10, 29, textWidth, 16, juce::Justification::centredLeft);
}

void ChainPage::resized()
{
    auto area = getLocalBounds().reduced (14);
    auto header = area.removeFromTop (28);
    backButton.setBounds (header.removeFromRight (64).reduced (0, 2));
    area.removeFromTop (20);

    auto row1 = area.removeFromTop (26);
    if (mode == Mode::Master)
    {
        genreBox.setBounds (row1.removeFromLeft (juce::jmax (90, row1.getWidth() / 3)).reduced (0, 1));
        subGenreBox.setBounds (row1.removeFromLeft (juce::jmax (90, row1.getWidth() / 2)).reduced (4, 1));
    }
    profileBox.setBounds (row1.reduced (4, 1));

    area.removeFromTop (8);
    auto row2 = area.removeFromTop (26);
    toneLabel.setBounds (row2.removeFromLeft (36));
    toneSlider.setBounds (row2.removeFromLeft (juce::jmax (120, row2.getWidth() - 120)));
    healthButton.setBounds (row2.removeFromRight (104).reduced (2, 0));

    area.removeFromTop (10);
    auto row3 = area.removeFromTop (30);
    buildButton.setBounds (row3.removeFromLeft (150));
    statusLabel.setBounds (row3.reduced (10, 0));

    area.removeFromTop (10);
    dragOut.setBounds (area.removeFromBottom (46));
    area.removeFromBottom (10);
    meter.setBounds (area);
}

void ChainPage::updateProfileChoices()
{
    profileBox.clear (juce::dontSendNotification);

    if (mode == Mode::Vocal)
    {
        profileBox.addItem ("Dark Trap", 1);
        profileBox.addItem ("Melodic Rap", 2);
        profileBox.addItem ("Rage", 3);
        profileBox.addItem ("Smooth R&B", 4);
        profileBox.addItem ("Drill", 5);
        profileBox.addItem ("Pop / Radio", 6);
        profileBox.addItem ("Clean / Neutral", 7);
        profileBox.setSelectedId (1, juce::dontSendNotification);
        return;
    }

    switch (genreBox.getSelectedId())
    {
        case 4:  profileBox.addItem ("Drill", 1); break;
        case 5:  profileBox.addItem ("Smooth R&B", 1); break;
        case 6:  profileBox.addItem ("Pop / Radio", 1); break;
        default: profileBox.addItem ("Dark Trap", 1); break;
    }
    profileBox.setSelectedId (1, juce::dontSendNotification);
}

juce::String ChainPage::profileId() const
{
    if (mode == Mode::Master)
    {
        switch (genreBox.getSelectedId())
        {
            case 4: return "drill";
            case 5: return "rnb-smooth";
            case 6: return "pop-radio";
            default: return "dark-trap";
        }
    }

    switch (profileBox.getSelectedId())
    {
        case 2: return "melodic-rap";
        case 3: return "rage";
        case 4: return "rnb-smooth";
        case 5: return "drill";
        case 6: return "pop-radio";
        case 7: return "neutral";
        default: return "dark-trap";
    }
}

void ChainPage::build()
{
    if (! capture.hasAudio())
    {
        statusLabel.setText ("play some audio through this insert first",
                             juce::dontSendNotification);
        return;
    }

    auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("voxchain_capture.wav");
    if (! capture.writeWav (temp))
    {
        statusLabel.setText ("could not write the capture", juce::dontSendNotification);
        return;
    }

    auto request = EngineClient::makeRequest ("build");
    if (auto* obj = request.getDynamicObject())
    {
        obj->setProperty ("audio_path", temp.getFullPathName());
        obj->setProperty ("profile", profileId());
        obj->setProperty ("tone", (int) toneSlider.getValue());
        obj->setProperty ("include_recipe", true);
        obj->setProperty ("context", mode == Mode::Master ? "master" : "main");
    }

    auto measureRequest = EngineClient::makeRequest ("measure");
    if (auto* obj = measureRequest.getDynamicObject())
    {
        obj->setProperty ("audio_path", temp.getFullPathName());
        obj->setProperty ("profile", profileId());
    }

    buildButton.setEnabled (false);
    lastFstPath.clear();
    lastVerified = false;
    dragOut.repaint();
    statusLabel.setText ("building...", juce::dontSendNotification);

    juce::Component::SafePointer<ChainPage> safe (this);

    service.post (measureRequest, 15000, [safe, request] (const EngineReply& measure)
    {
        if (auto* page = safe.getComponent())
            page->onMeasure (measure, request);
    });
}

void ChainPage::onMeasure (const EngineReply& measure, const juce::var& buildRequest)
{
    juce::Component::SafePointer<ChainPage> safe (this);
    service.post (buildRequest, 25000, [safe, measure] (const EngineReply& built)
    {
        if (auto* page = safe.getComponent())
            page->onResults (measure, built);
    });
}

void ChainPage::onResults (const EngineReply& measure, const EngineReply& built)
{
    buildButton.setEnabled (true);

    if (! measure.ok)
        statusLabel.setText (measure.describe(), juce::dontSendNotification);
    else
        meter.setFromValues (measure.data.getProperty ("values", juce::var()),
                             (float) (double) measure.data.getProperty ("score", 0.0));

    if (! built.ok)
    {
        statusLabel.setText (built.describe(), juce::dontSendNotification);
        dragOut.repaint();
        return;
    }

    lastVerified = (bool) built.data.getProperty ("verified", false);
    const auto chain = built.data.getProperty ("chain", juce::var());
    const auto path = built.data.getProperty ("fst_path", juce::var()).toString();

    if (lastVerified && path.isNotEmpty())
    {
        lastFstPath = path;
        const int count = chain.isArray() ? chain.size() : 0;
        statusLabel.setText ("verified - " + juce::String (count) + " plugins, drag it out",
                             juce::dontSendNotification);
        if (onChainBuilt)
            onChainBuilt (built.data.getProperty ("spec", juce::var()));
    }
    else
    {
        lastFstPath.clear();
        const auto issues = built.data.getProperty ("issues", juce::var());
        auto message = issues.isArray() && issues.size() > 0
                           ? issues[0].toString()
                           : "build did not verify";
        statusLabel.setText (message, juce::dontSendNotification);
    }

    dragOut.repaint();
}
