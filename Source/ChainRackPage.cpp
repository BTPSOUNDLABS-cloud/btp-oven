#include "ChainRackPage.h"

#include "EngineService.h"

namespace
{
    float sliderMinFor (const juce::String& key, double value)
    {
        if (key.containsIgnoreCase ("hz")) return 20.0f;
        if (key.containsIgnoreCase ("ms")) return 0.0f;
        if (key.containsIgnoreCase ("ratio")) return 1.0f;
        if (key.containsIgnoreCase ("q")) return 0.1f;
        if (std::abs (value) <= 1.0) return 0.0f;
        if (value < 0.0) return -24.0f;
        return 0.0f;
    }

    float sliderMaxFor (const juce::String& key, double value)
    {
        if (key.containsIgnoreCase ("hz"))
            return key.containsIgnoreCase ("shelf") ? 20000.0f : 20000.0f;
        if (key.containsIgnoreCase ("ms")) return 1000.0f;
        if (key.containsIgnoreCase ("ratio")) return 20.0f;
        if (key.containsIgnoreCase ("q")) return 6.0f;
        if (std::abs (value) <= 1.0) return 1.0f;
        if (value < 0.0) return 12.0f;
        return 200.0f;
    }
}

ChainRackPage::ChainRackPage (AudioCapture& audioCapture, EngineService& engineService)
    : capture (audioCapture), service (engineService)
{
    addAndMakeVisible (stageList);
    addAndMakeVisible (bypassButton);
    addAndMakeVisible (backButton);
    addAndMakeVisible (promptBox);
    addAndMakeVisible (applyButton);
    addAndMakeVisible (buildButton);
    addAndMakeVisible (statusLabel);

    stageList.setRowHeight (22);
    stageList.setColour (juce::ListBox::backgroundColourId, panel);
    stageList.setColour (juce::ListBox::outlineColourId, panel.brighter (0.1f));

    statusLabel.setText ("build a chain to fill the rack", juce::dontSendNotification);

    promptBox.setTextToShowWhenEmpty ("drop the delay  /  swap the compressor to CLA-76",
                                      text.withAlpha (0.35f));
    promptBox.onReturnKey = [this] { applyPrompt(); };

    applyButton.onClick = [this] { applyPrompt(); };
    bypassButton.onClick = [this] { toggleBypass(); };
    buildButton.onClick = [this] { buildFromRack(); };
    backButton.onClick = [this] { if (onMainPage) onMainPage(); };

    startTimerHz (8);
}

ChainRackPage::~ChainRackPage()
{
    stopTimer();
}

void ChainRackPage::setPalette (juce::Colour bg, juce::Colour panelColour,
                                juce::Colour textColour, juce::Colour accentColour)
{
    background = bg;
    panel = panelColour;
    text = textColour;
    accent = accentColour;

    stageList.setColour (juce::ListBox::backgroundColourId, panel);
    stageList.setColour (juce::ListBox::textColourId, text);
    statusLabel.setColour (juce::Label::textColourId, text.withAlpha (0.7f));
    promptBox.setColour (juce::TextEditor::backgroundColourId, panel);
    promptBox.setColour (juce::TextEditor::textColourId, text);
    promptBox.setColour (juce::TextEditor::outlineColourId, panel.brighter (0.15f));
    buildButton.setColour (juce::TextButton::buttonColourId, accent);
    buildButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    repaint();
}

void ChainRackPage::timerCallback()
{
    waveform = capture.peaks (240);
    if (waveform.empty())
        return;
    if (isVisible())
        repaint (getLocalBounds().removeFromTop (getHeight() / 3));
}

void ChainRackPage::adoptSpec (const juce::var& specJson)
{
    spec = specJson;
    lastFstPath.clear();
    refreshStages();
    statusLabel.setText ("chain loaded - pick a stage", juce::dontSendNotification);
    repaint();
}

void ChainRackPage::refreshStages()
{
    stageLabels.clear();
    const auto stages = spec.getProperty ("stages", juce::var());
    if (stages.isArray())
    {
        for (int i = 0; i < stages.size(); ++i)
        {
            const auto stage = stages[i];
            const auto role = stage.getProperty ("role", "stage").toString();
            const auto plugin = stage.getProperty ("plugin_hint", "").toString();
            const bool enabled = (bool) stage.getProperty ("enabled", true);
            stageLabels.add ((enabled ? "" : "[bypass]  ")
                             + role + (plugin.isNotEmpty() ? "  ·  " + plugin : ""));
        }
    }

    stageList.updateContent();
    stageList.repaint();

    if (stageLabels.isEmpty())
    {
        selectedStage = -1;
        rebuildControls();
        return;
    }

    selectedStage = juce::jlimit (0, stageLabels.size() - 1,
                                  selectedStage < 0 ? 0 : selectedStage);
    stageList.selectRow (selectedStage, true, true);
    rebuildControls();
}

void ChainRackPage::rebuildControls()
{
    sliders.clear();
    sliderLabels.clear();
    sliderKeys.clear();

    auto stages = spec.getProperty ("stages", juce::var());
    if (! stages.isArray() || selectedStage < 0 || selectedStage >= stages.size())
    {
        resized();
        repaint();
        return;
    }

    auto stage = stages[selectedStage];
    auto params = stage.getProperty ("params", juce::var());

    if (auto* object = params.getDynamicObject())
    {
        int row = 0;
        for (const auto& property : object->getProperties())
        {
            const auto key = property.name.toString();
            const auto value = property.value;
            if (! value.isDouble() && ! value.isInt() && ! value.isInt64())
                continue;

            const double current = (double) value;
            auto label = std::make_unique<juce::Label>();
            label->setText (key.replaceCharacter ('_', ' ') + "  ",
                            juce::dontSendNotification);
            label->setColour (juce::Label::textColourId, text.withAlpha (0.7f));
            label->setFont (juce::Font (juce::FontOptions (10.0f)));
            addAndMakeVisible (*label);

            auto slider = std::make_unique<juce::Slider>();
            slider->setSliderStyle (juce::Slider::LinearHorizontal);
            slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);
            slider->setRange (sliderMinFor (key, current), sliderMaxFor (key, current), 0.01);
            slider->setValue (current, juce::dontSendNotification);
            slider->setColour (juce::Slider::thumbColourId, accent);
            slider->setColour (juce::Slider::trackColourId, accent.withAlpha (0.5f));
            addAndMakeVisible (*slider);

            const int capturedRow = selectedStage;
            juce::Component::SafePointer<juce::Slider> safeSlider (slider.get());
            slider->onValueChange = [this, capturedRow, key, safeSlider]
            {
                if (safeSlider == nullptr) return;
                auto stagesNow = spec.getProperty ("stages", juce::var());
                if (! stagesNow.isArray() || capturedRow >= stagesNow.size())
                    return;

                auto stageNow = stagesNow[capturedRow];
                auto paramsNow = stageNow.getProperty ("params", juce::var());
                auto* paramsObject = paramsNow.getDynamicObject();
                auto* stageObject = stageNow.getDynamicObject();
                if (paramsObject == nullptr || stageObject == nullptr)
                    return;

                paramsObject->setProperty (key, safeSlider->getValue());
                stageObject->setProperty ("params", paramsNow);
                stagesNow.getArray()->set (capturedRow, stageNow);
                if (auto* root = spec.getDynamicObject())
                    root->setProperty ("stages", stagesNow);
            };

            sliderLabels.push_back (std::move (label));
            sliders.push_back (std::move (slider));
            sliderKeys.add (key);
            juce::ignoreUnused (row);
            ++row;
        }
    }

    resized();
    repaint();
}

void ChainRackPage::paintListBoxItem (int row, juce::Graphics& g, int width, int height,
                                      bool selected)
{
    if (row < 0 || row >= stageLabels.size())
        return;

    if (selected)
    {
        g.setColour (accent.withAlpha (0.18f));
        g.fillRect (0, 0, width, height);
    }

    g.setColour (selected ? accent : text.withAlpha (0.85f));
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText (stageLabels[row], 8, 0, width - 12, height, juce::Justification::centredLeft);
}

int ChainRackPage::getNumRows() { return stageLabels.size(); }

void ChainRackPage::selectedRowsChanged (int lastRowSelected)
{
    selectedStage = lastRowSelected;
    rebuildControls();
}

void ChainRackPage::toggleBypass()
{
    auto stages = spec.getProperty ("stages", juce::var());
    if (! stages.isArray() || selectedStage < 0 || selectedStage >= stages.size())
        return;

    auto stage = stages[selectedStage];
    auto* object = stage.getDynamicObject();
    if (object == nullptr)
        return;

    const bool enabled = (bool) stage.getProperty ("enabled", true);
    object->setProperty ("enabled", ! enabled);
    auto copy = stage;
    stages.getArray()->set (selectedStage, copy);
    if (auto* root = spec.getDynamicObject())
        root->setProperty ("stages", stages);

    refreshStages();
    statusLabel.setText ((! enabled ? "enabled" : "bypassed") + juce::String (" stage " )
                         + juce::String (selectedStage + 1), juce::dontSendNotification);
}

void ChainRackPage::applyPrompt()
{
    const auto textValue = promptBox.getText().trim();
    if (textValue.isEmpty())
        return;
    if (! spec.isObject())
    {
        statusLabel.setText ("build a chain first", juce::dontSendNotification);
        return;
    }

    auto request = EngineClient::makeRequest ("edit");
    if (auto* obj = request.getDynamicObject())
    {
        obj->setProperty ("spec", spec);
        obj->setProperty ("text", textValue);
    }

    applyButton.setEnabled (false);
    juce::Component::SafePointer<ChainRackPage> safe (this);

    service.post (request, 15000, [safe] (const EngineReply& reply)
    {
        if (auto* page = safe.getComponent())
        {
            page->applyButton.setEnabled (true);
            if (! reply.ok)
            {
                page->statusLabel.setText (reply.describe(), juce::dontSendNotification);
                return;
            }
            page->spec = reply.data.getProperty ("spec", page->spec);
            page->promptBox.clear();
            page->refreshStages();
            page->statusLabel.setText (reply.data.getProperty ("note", "").toString(),
                                       juce::dontSendNotification);
        }
    });
}

void ChainRackPage::buildFromRack()
{
    if (! spec.isObject())
    {
        statusLabel.setText ("build a chain first", juce::dontSendNotification);
        return;
    }
    if (! capture.hasAudio())
    {
        statusLabel.setText ("play some audio through this insert first",
                             juce::dontSendNotification);
        return;
    }

    auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("voxchain_rack.wav");
    if (! capture.writeWav (temp))
    {
        statusLabel.setText ("could not write the capture", juce::dontSendNotification);
        return;
    }

    auto request = EngineClient::makeRequest ("build");
    if (auto* obj = request.getDynamicObject())
    {
        obj->setProperty ("audio_path", temp.getFullPathName());
        obj->setProperty ("spec", spec);
        obj->setProperty ("profile", spec.getProperty ("profile", "dark-trap").toString());
    }

    buildButton.setEnabled (false);
    statusLabel.setText ("building...", juce::dontSendNotification);

    juce::Component::SafePointer<ChainRackPage> safe (this);
    service.post (request, 25000, [safe] (const EngineReply& reply)
    {
        if (auto* page = safe.getComponent())
            page->onBuildReply (reply);
    });
}

void ChainRackPage::onBuildReply (const EngineReply& reply)
{
    buildButton.setEnabled (true);

    if (! reply.ok)
    {
        statusLabel.setText (reply.describe(), juce::dontSendNotification);
        return;
    }

    const bool verified = (bool) reply.data.getProperty ("verified", false);
    const auto path = reply.data.getProperty ("fst_path", juce::var()).toString();

    if (verified && path.isNotEmpty())
    {
        lastFstPath = path;
        const auto chain = reply.data.getProperty ("chain", juce::var());
        statusLabel.setText ("verified - " + juce::String (chain.isArray() ? chain.size() : 0)
                                 + " plugins. Drag the strip below.",
                             juce::dontSendNotification);
    }
    else
    {
        lastFstPath.clear();
        statusLabel.setText ("build did not verify", juce::dontSendNotification);
    }
    repaint();
}

void ChainRackPage::mouseDrag (const juce::MouseEvent& event)
{
    if (lastFstPath.isEmpty() || ! dragZone.contains (event.getPosition()))
        return;
    if (juce::File (lastFstPath).existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ lastFstPath }, false, this);
}

void ChainRackPage::paint (juce::Graphics& g)
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    g.fillAll (background);

    g.setColour (text);
    g.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    g.drawText ("CHAIN RACK", 12, 8, juce::jmax (0, getWidth() - 120), 22,
                juce::Justification::centredLeft);

    g.setColour (text.withAlpha (0.45f));
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText ("click a stage to edit  ·  bypass what you do not want  ·  build and drag out",
                12, 29, juce::jmax (0, getWidth() - 120), 16,
                juce::Justification::centredLeft);

    // waveform
    auto wave = getLocalBounds().reduced (12);
    wave.removeFromTop (150);
    auto waveArea = wave.removeFromTop (70);
    g.setColour (panel);
    g.fillRoundedRectangle (waveArea.toFloat(), 6.0f);

    if (! waveform.empty())
    {
        const float mid = waveArea.getCentreY();
        const float step = waveArea.getWidth() / (float) waveform.size();
        g.setColour (accent);
        for (size_t i = 0; i < waveform.size(); ++i)
        {
            const float half = juce::jmax (1.0f, waveform[i] * (waveArea.getHeight() * 0.42f));
            g.fillRect (waveArea.getX() + step * (float) i, mid - half,
                        juce::jmax (1.0f, step - 1.0f), half * 2.0f);
        }
    }
    else
    {
        g.setColour (text.withAlpha (0.35f));
        g.drawText ("play audio to see the waveform", waveArea, juce::Justification::centred);
    }

    // drag strip
    g.setColour (lastFstPath.isEmpty() ? panel : accent.withAlpha (0.15f));
    g.fillRoundedRectangle (dragZone.toFloat(), 5.0f);
    g.setColour (lastFstPath.isEmpty() ? text.withAlpha (0.25f) : accent);
    g.drawRoundedRectangle (dragZone.toFloat().reduced (0.5f), 5.0f, 1.0f);
    g.setColour (lastFstPath.isEmpty() ? text.withAlpha (0.35f) : text);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (lastFstPath.isEmpty() ? "build to arm the drag strip"
                                      : "drag here to a mixer track",
                dragZone, juce::Justification::centred);
}

void ChainRackPage::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto header = area.removeFromTop (26);
    backButton.setBounds (header.removeFromRight (64).reduced (0, 1));
    area.removeFromTop (22);

    auto wave = area.removeFromTop (70);
    juce::ignoreUnused (wave);
    area.removeFromTop (8);

    dragZone = area.removeFromBottom (46);
    area.removeFromBottom (8);

    auto promptRow = area.removeFromBottom (28);
    buildButton.setBounds (promptRow.removeFromRight (170).reduced (0, 1));
    promptRow.removeFromRight (6);
    applyButton.setBounds (promptRow.removeFromRight (70).reduced (0, 1));
    promptRow.removeFromRight (6);
    promptBox.setBounds (promptRow);

    area.removeFromBottom (6);
    statusLabel.setBounds (area.removeFromBottom (18));

    auto left = area.removeFromLeft (juce::jmax (180, area.getWidth() / 4));
    stageList.setBounds (left.withTrimmedBottom (30));
    bypassButton.setBounds (left.removeFromBottom (26).reduced (0, 2));

    auto right = area.reduced (12, 0);
    const int rowHeight = 26;
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        auto row = right.removeFromTop (rowHeight);
        sliderLabels[i]->setBounds (row.removeFromLeft (130));
        sliders[i]->setBounds (row);
    }
}
