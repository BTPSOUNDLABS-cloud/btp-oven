#pragma once

#include <JuceHeader.h>
#include "AudioCapture.h"
#include "BandMeter.h"
#include "EngineClient.h"
#include "EngineService.h"

/** Hand the generated .fst to the OS drag-and-drop. */
class DragOutArea : public juce::Component
{
public:
    std::function<juce::String()> getFilePath;
    juce::Colour accent { 0xffb6ff2e }, text { 0xffe9e3d2 };

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        const bool armed = getFilePath != nullptr && getFilePath().isNotEmpty();

        g.setColour (armed ? accent.withAlpha (0.16f) : juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (area, 6.0f);
        g.setColour (armed ? accent : juce::Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);

        g.setColour (armed ? text : text.withAlpha (0.45f));
        g.setFont (12.0f);
        g.drawFittedText (armed ? "drag the chain to a mixer track" : "build a chain first",
                          getLocalBounds(), juce::Justification::centred, 2);
    }

    void mouseDrag (const juce::MouseEvent&) override
    {
        if (getFilePath == nullptr) return;
        const auto path = getFilePath();
        if (path.isEmpty() || ! juce::File (path).existsAsFile()) return;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ path }, false, this);
    }
};

/**
    Vocal/master chain page for BtP Oven.

    Shares the EngineClient contract with the standalone app: measure live audio,
    ask the local engine for a chain, show the result, drag the preset out.

    Colours are injected so the page matches Oven rather than shipping a second
    visual language.
*/
class ChainPage : public juce::Component
{
public:
    enum class Mode { Vocal, Master };

    ChainPage (Mode pageMode, AudioCapture& audioCapture, EngineService& engineService);
    ~ChainPage() override;

    /** Called by the BACK button so the host can return to its main page. */
    std::function<void()> onMainPage;

    /** Fires after a verified build so a rack page can adopt the chain. */
    std::function<void(const juce::var&)> onChainBuilt;

    void setPalette (juce::Colour background, juce::Colour panel, juce::Colour textColour,
                     juce::Colour accentColour, juce::Colour goodColour);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void build();
    void onMeasure (const EngineReply& measure, const juce::var& buildRequest);
    void onResults (const EngineReply& measure, const EngineReply& build);
    juce::String profileId() const;
    void updateProfileChoices();

    Mode mode;
    EngineService& service;
    AudioCapture& capture;

    juce::Colour background { 0xff0a0b0d }, panel { 0xff1b1d21 }, text { 0xffe9e3d2 },
                 accent { 0xffb6ff2e }, good { 0xffb6ff2e };

    juce::ComboBox genreBox, subGenreBox, profileBox;
    juce::Slider toneSlider;
    juce::Label toneLabel, statusLabel;
    juce::TextButton buildButton { "BUILD CHAIN" };
    juce::TextButton healthButton { "TEST ENGINE" };
    juce::TextButton backButton { "MAIN" };
    BandMeter meter;
    DragOutArea dragOut;

    juce::String lastFstPath;
    bool lastVerified = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainPage)
};
