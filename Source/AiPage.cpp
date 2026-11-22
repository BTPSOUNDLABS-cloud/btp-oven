#include "AiPage.h"
#include "ModelPaths.h"

//==============================================================================
/** A large, resizable, zoomable editor for the prompt and lyrics fields.

    Double-clicking either field opens this. The text is committed when the
    window closes or DONE is pressed, so it behaves like editing the field
    itself rather than a separate copy. */
class AiBigEditor final : public juce::DocumentWindow
{
public:
    AiBigEditor()
        : juce::DocumentWindow ("LYRICS", juce::Colour (0xff0b0d10),
                                juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);

        auto* body = new Body (*this);
        setContentOwned (body, true);
        bodyPtr = body;
        setSize (840, 560);
    }

    void open (const juce::String& title, const juce::String& text,
               std::function<void (const juce::String&)> onCommit)
    {
        commit = std::move (onCommit);

        setName (title);
        setTitle (title);
        bodyPtr->setText (text);

        setVisible (true);
        toFront (true);
        bodyPtr->grabFocus();
    }

    void commitAndHide()
    {
        if (commit != nullptr)
            commit (bodyPtr->getText());

        setVisible (false);
    }

    void closeButtonPressed() override { commitAndHide(); }

private:
    class Body final : public juce::Component
    {
    public:
        explicit Body (AiBigEditor& o) : owner (o)
        {
            editor.setMultiLine (true, true);
            editor.setReturnKeyStartsNewLine (true);
            editor.setScrollbarsShown (true);
            editor.setFont (juce::Font (juce::FontOptions (fontSize)));
            editor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff080a08));
            editor.setColour (juce::TextEditor::textColourId, OvenLookAndFeel::cream());
            editor.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff2a2e33));
            editor.setColour (juce::TextEditor::focusedOutlineColourId, OvenLookAndFeel::lime());
            editor.setColour (juce::TextEditor::highlightColourId,
                              OvenLookAndFeel::lime().withAlpha (0.25f));
            addAndMakeVisible (editor);

            auto setup = [this] (juce::TextButton& button, const juce::String& text)
            {
                button.setButtonText (text);
                button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a1f24));
                button.setColour (juce::TextButton::textColourOffId, OvenLookAndFeel::lime());
                addAndMakeVisible (button);
            };

            setup (smaller, "A-");
            setup (bigger,  "A+");
            setup (done,    "DONE");

            smaller.onClick = [this] { zoom (-2.0f); };
            bigger.onClick  = [this] { zoom (2.0f); };
            done.onClick    = [this] { owner.commitAndHide(); };
        }

        void setText (const juce::String& text) { editor.setText (text, false); }
        juce::String getText() const { return editor.getText(); }
        void grabFocus() { editor.grabKeyboardFocus(); }

        void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0b0d10)); }

        void resized() override
        {
            auto area = getLocalBounds().reduced (12);
            auto bar = area.removeFromBottom (34);

            done.setBounds (bar.removeFromRight (110).reduced (2));
            bar.removeFromRight (10);
            bigger.setBounds (bar.removeFromRight (56).reduced (2));
            smaller.setBounds (bar.removeFromRight (56).reduced (2));

            area.removeFromBottom (8);
            editor.setBounds (area);
        }

    private:
        void zoom (float delta)
        {
            fontSize = juce::jlimit (11.0f, 42.0f, fontSize + delta);
            editor.setFont (juce::Font (juce::FontOptions (fontSize)));
        }

        AiBigEditor& owner;
        juce::TextEditor editor;
        juce::TextButton smaller, bigger, done;
        float fontSize = 18.0f;
    };

    Body* bodyPtr = nullptr;
    std::function<void (const juce::String&)> commit;
};

//==============================================================================
/** One MIDI take: title, piano roll, and a drag handle for the .mid file. */
class MidiCard final : public juce::Component
{
public:
    void setSketch (const juce::var& value)
    {
        sketch = value;
        path = value.getProperty ("path", juce::var{}).toString();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        const auto accent = OvenLookAndFeel::lime();
        const bool armed = path.isNotEmpty() && juce::File (path).existsAsFile();

        g.setColour (juce::Colour (0xff0a0d0a));
        g.fillRoundedRectangle (area, 5.0f);
        g.setColour (armed ? accent.withAlpha (0.55f) : juce::Colour (0xff2a2e33));
        g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 1.0f);

        auto header = area.removeFromTop (18.0f).reduced (8.0f, 0.0f);

        if (! armed)
        {
            g.setColour (OvenLookAndFeel::cream().withAlpha (0.35f));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawFittedText ("EMPTY - PRESS COMPOSE 3", header.toNearestInt(),
                              juce::Justification::centredLeft, 1);
            return;
        }

        const auto seed = (juce::int64) sketch.getProperty ("seed", 0);
        const auto count = (int) sketch.getProperty ("note_count", 0);

        g.setColour (accent);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawFittedText ("TAKE " + juce::String (takeNumber) + "   SEED " + juce::String (seed)
                              + "   " + juce::String (count) + " NOTES",
                          header.toNearestInt(), juce::Justification::centredLeft, 1);

        g.setColour (OvenLookAndFeel::cream().withAlpha (0.45f));
        g.drawFittedText ("DRAG TO FL", header.toNearestInt(),
                          juce::Justification::centredRight, 1);

        auto roll = area.reduced (8.0f, 2.0f).withTrimmedBottom (4.0f);
        if (roll.getHeight() < 8.0f)
            return;

        g.setColour (juce::Colour (0xff050705));
        g.fillRoundedRectangle (roll, 3.0f);

        auto* layers = sketch.getProperty ("layers", juce::var{}).getDynamicObject();
        if (layers == nullptr)
            return;

        int lowPitch = 127, highPitch = 0;
        double endBeat = 1.0;

        for (auto& item : layers->getProperties())
        {
            auto* layer = item.value.getDynamicObject();
            if (layer == nullptr)
                continue;

            if (auto* notes = layer->getProperty ("notes").getArray())
            {
                for (auto& note : *notes)
                {
                    const int pitch = (int) note.getProperty ("midi", 60);
                    lowPitch = juce::jmin (lowPitch, pitch);
                    highPitch = juce::jmax (highPitch, pitch);
                    endBeat = juce::jmax (endBeat,
                                          (double) note.getProperty ("start", 0.0)
                                              + (double) note.getProperty ("duration", 0.0));
                }
            }
        }

        if (highPitch <= lowPitch)
            return;

        lowPitch -= 2;
        highPitch += 2;

        for (auto& item : layers->getProperties())
        {
            auto* layer = item.value.getDynamicObject();
            if (layer == nullptr)
                continue;

            auto* notes = layer->getProperty ("notes").getArray();
            if (notes == nullptr)
                continue;

            const auto name = item.name.toString();
            juce::Colour colour = OvenLookAndFeel::lime().withAlpha (0.85f);

            if (name == "bass")       colour = juce::Colour (0xff8fe6c8);
            else if (name == "top")   colour = juce::Colour (0xffe0b46a);
            else if (name == "drums") colour = juce::Colour (0xffff6a8a);
            else if (name == "chords") colour = juce::Colour (0xff9db7ff);

            g.setColour (colour);

            for (auto& note : *notes)
            {
                const double start = (double) note.getProperty ("start", 0.0);
                const double duration = juce::jmax (0.02, (double) note.getProperty ("duration", 0.1));
                const int pitch = (int) note.getProperty ("midi", 60);

                const float x0 = roll.getX() + roll.getWidth() * (float) (start / endBeat);
                const float x1 = roll.getX() + roll.getWidth()
                                     * (float) (juce::jmin (endBeat, start + duration) / endBeat);
                const float y = roll.getBottom()
                                - roll.getHeight() * (float) (pitch - lowPitch)
                                      / (float) (highPitch - lowPitch);

                g.fillRect (juce::Rectangle<float> (x0, y - 1.5f,
                                                    juce::jmax (1.5f, x1 - x0), 3.0f));
            }
        }
    }

    void mouseDrag (const juce::MouseEvent&) override
    {
        if (path.isEmpty() || ! juce::File (path).existsAsFile())
            return;

        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ path }, false, this);
    }

    int takeNumber = 1;

private:
    juce::var sketch;
    juce::String path;
};

//==============================================================================
/** The three MIDI takes, with drag-out. Opened from the AI page. */
class AiMidiWindow final : public juce::DocumentWindow
{
public:
    AiMidiWindow()
        : juce::DocumentWindow ("MIDI SKETCHES", juce::Colour (0xff0b0d10),
                                juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, true);

        auto* body = new Body (*this);
        setContentOwned (body, true);
        bodyPtr = body;
        setSize (940, 500);
    }

    /** Called by COMPOSE 3; the page supplies the prompt and settings. */
    std::function<void()> onCompose;

    void refresh (const juce::var& data, const juce::String& status)
    {
        bodyPtr->setSketches (data, status);
    }

    void closeButtonPressed() override { setVisible (false); }

private:
    class Body final : public juce::Component
    {
    public:
        explicit Body (AiMidiWindow& o) : owner (o)
        {
            one.takeNumber = 1;
            two.takeNumber = 2;
            three.takeNumber = 3;

            for (auto* card : { &one, &two, &three })
                addAndMakeVisible (*card);

            compose.setButtonText ("COMPOSE 3");
            compose.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a1f24));
            compose.setColour (juce::TextButton::textColourOffId, OvenLookAndFeel::lime());
            compose.onClick = [this] { if (owner.onCompose != nullptr) owner.onCompose(); };
            addAndMakeVisible (compose);

            status.setColour (juce::Label::textColourId,
                              OvenLookAndFeel::cream().withAlpha (0.6f));
            status.setFont (juce::Font (juce::FontOptions (11.5f)));
            addAndMakeVisible (status);
        }

        void setSketches (const juce::var& data, const juce::String& text)
        {
            status.setText (text, juce::dontSendNotification);

            auto* array = data.getProperty ("sketches", juce::var{}).getArray();
            MidiCard* cards[3] { &one, &two, &three };

            for (int i = 0; i < 3; ++i)
                cards[i]->setSketch (array != nullptr && i < array->size()
                                         ? (*array)[i]
                                         : juce::var());
        }

        void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0b0d10)); }

        void resized() override
        {
            auto area = getLocalBounds().reduced (12);

            auto top = area.removeFromTop (32);
            compose.setBounds (top.removeFromRight (150).reduced (2));
            status.setBounds (top);

            area.removeFromTop (8);

            const int cardHeight = juce::jmax (70, (area.getHeight() - 16) / 3);

            one.setBounds (area.removeFromTop (cardHeight));
            area.removeFromTop (8);
            two.setBounds (area.removeFromTop (cardHeight));
            area.removeFromTop (8);
            three.setBounds (area);
        }

    private:
        AiMidiWindow& owner;
        MidiCard one, two, three;
        juce::TextButton compose;
        juce::Label status;
    };

    Body* bodyPtr = nullptr;
};

namespace
{
    constexpr float kWidth  = 1148.0f;
    constexpr float kHeight = 692.0f;
    constexpr int kPeakCount = 260;

    juce::String stateName (AiEngine::State s)
    {
        switch (s)
        {
            case AiEngine::State::Unloaded:   return "UNLOADED";
            case AiEngine::State::Loading:    return "LOADING";
            case AiEngine::State::Ready:      return "READY";
            case AiEngine::State::Generating: return "COOKING";
            case AiEngine::State::Error:      return "ERROR";
        }

        return "UNKNOWN";
    }

    juce::File pick (const juce::File& dir, const juce::String& pattern)
    {
        const auto files = dir.findChildFiles (juce::File::findFiles, false, pattern);
        return files.isEmpty() ? juce::File() : files.getFirst();
    }
}

//==============================================================================
/** Simple in-page dialog: name + dataset + epochs + rank.  Avoids a modal
    OS window inside plugin editors. */
class AiPage::LoraTrainDialog : public juce::Component
{
public:
    explicit LoraTrainDialog (AiPage& o) : owner (o)
    {
        setOpaque (false);
        setInterceptsMouseClicks (true, true);

        nameEditor.setMultiLine (false);
        nameEditor.setReturnKeyStartsNewLine (false);
        nameEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff101215));
        nameEditor.setColour (juce::TextEditor::textColourId, OvenLookAndFeel::lime());
        nameEditor.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff2a2e33));
        nameEditor.setColour (juce::TextEditor::focusedOutlineColourId,
                              OvenLookAndFeel::lime().withAlpha (0.6f));
        nameEditor.setText ("lora_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M"));
        addAndMakeVisible (nameEditor);

        auto setupBox = [this] (juce::ComboBox& box)
        {
            box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff101215));
            box.setColour (juce::ComboBox::textColourId, OvenLookAndFeel::lime());
            box.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff2a2e33));
            box.setColour (juce::ComboBox::arrowColourId, OvenLookAndFeel::lime());
            addAndMakeVisible (box);
        };

        setupBox (dataBox);
        dataBox.addItemList ({ "Loops", "Vocals", "One-shots", "Variants", "Stems" }, 1);
        dataBox.setSelectedId (1);

        setupBox (epochsBox);
        epochsBox.addItemList ({ "10", "20", "40", "60", "100" }, 1);
        epochsBox.setSelectedId (3);

        setupBox (rankBox);
        rankBox.addItemList ({ "8", "16", "32" }, 1);
        rankBox.setSelectedId (2);

        startButton.setButtonText ("START TRAINING");
        startButton.setColour (juce::TextButton::buttonColourId,
                               OvenLookAndFeel::lime().withAlpha (0.16f));
        startButton.setColour (juce::TextButton::textColourOffId, OvenLookAndFeel::lime());
        startButton.onClick = [this] { owner.startLoraTraining(); };
        addAndMakeVisible (startButton);

        cancelButton.setButtonText ("CANCEL");
        cancelButton.setColour (juce::TextButton::textColourOffId, OvenLookAndFeel::cream());
        cancelButton.onClick = [this] { setVisible (false); };
        addAndMakeVisible (cancelButton);
    }

    void focusName()
    {
        nameEditor.grabKeyboardFocus();
        nameEditor.selectAll();
    }

    juce::String getName() const     { return nameEditor.getText(); }
    int getDataIndex() const         { return juce::jmax (0, dataBox.getSelectedId() - 1); }
    int getEpochs() const            { return juce::jmax (1, epochsBox.getText().getIntValue()); }
    int getRank() const              { return juce::jmax (1, rankBox.getText().getIntValue()); }

    void paint (juce::Graphics& g) override
    {
        const float s = owner.sc;

        auto R = [s] (float x, float y, float w, float h)
        {
            return juce::Rectangle<int> (juce::roundToInt (x * s), juce::roundToInt (y * s),
                                         juce::roundToInt (w * s), juce::roundToInt (h * s));
        };

        g.setColour (juce::Colours::black.withAlpha (0.62f));
        g.fillRect (getLocalBounds());

        const auto panel = R (344.0f, 196.0f, 460.0f, 258.0f).toFloat();

        juce::ColourGradient grad (juce::Colour (0xff23262b), panel.getX(), panel.getY(),
                                   juce::Colour (0xff121417), panel.getX(), panel.getBottom(), false);
        grad.addColour (0.7, juce::Colour (0xff191c20));
        g.setGradientFill (grad);
        g.fillRoundedRectangle (panel, 8.0f * s);

        g.setColour (OvenLookAndFeel::lime().withAlpha (0.8f));
        g.drawRoundedRectangle (panel, 8.0f * s, 1.6f * s);

        g.setColour (OvenLookAndFeel::lime());
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (9.0f, 12.0f * s), juce::Font::bold)));
        g.drawText ("TRAIN A LORA", R (368.0f, 210.0f, 412.0f, 18.0f),
                    juce::Justification::centredLeft, false);

        g.setColour (OvenLookAndFeel::cream().withAlpha (0.6f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.5f, 9.0f * s), juce::Font::bold)));
        g.drawFittedText ("Captures -> one-time GPU job -> new adapter in the LORA list.",
                          R (368.0f, 234.0f, 412.0f, 24.0f),
                          juce::Justification::topLeft, 2);

        auto label = [&] (const juce::String& text, juce::Rectangle<int> area)
        {
            g.setColour (OvenLookAndFeel::cream().withAlpha (0.5f));
            g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.0f, 8.5f * s), juce::Font::bold)));
            g.drawText (text, area, juce::Justification::centredLeft, false);
        };

        label ("NAME",   R (368.0f, 268.0f, 412.0f, 12.0f));
        label ("DATA",   R (368.0f, 324.0f, 150.0f, 12.0f));
        label ("EPOCHS", R (530.0f, 324.0f, 110.0f, 12.0f));
        label ("RANK",   R (652.0f, 324.0f, 128.0f, 12.0f));

        g.setColour (OvenLookAndFeel::cream().withAlpha (0.35f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.0f, 8.5f * s), juce::Font::bold)));
        g.drawFittedText ("12 GB cards handle loop-length data well at rank 8-16. "
                          "40-100 epochs for a clear style. Logs go to Training\\runs.",
                          R (368.0f, 376.0f, 412.0f, 24.0f),
                          juce::Justification::topLeft, 2);
    }

    void resized() override
    {
        const float s = owner.sc;

        auto R = [s] (float x, float y, float w, float h)
        {
            return juce::Rectangle<int> (juce::roundToInt (x * s), juce::roundToInt (y * s),
                                         juce::roundToInt (w * s), juce::roundToInt (h * s));
        };

        nameEditor.setBounds (R (368.0f, 282.0f, 412.0f, 28.0f));
        dataBox.setBounds    (R (368.0f, 338.0f, 150.0f, 26.0f));
        epochsBox.setBounds  (R (530.0f, 338.0f, 110.0f, 26.0f));
        rankBox.setBounds    (R (652.0f, 338.0f, 128.0f, 26.0f));
        startButton.setBounds (R (368.0f, 404.0f, 230.0f, 34.0f));
        cancelButton.setBounds (R (612.0f, 404.0f, 168.0f, 34.0f));
    }

private:
    AiPage& owner;
    juce::TextEditor nameEditor;
    juce::ComboBox dataBox, epochsBox, rankBox;
    juce::TextButton startButton, cancelButton;
};

//==============================================================================
AiPage::AiPage (BtPOvenAudioProcessor& p)
    : processor (p),
      engine (AiEngine::getInstance())
{
    setOpaque (true);
    setInterceptsMouseClicks (true, true);

    currentModels = engine.getModels();

    const auto lime = OvenLookAndFeel::lime();
    const auto teal = juce::Colour (0xff8fe6c8);

    //==========================================================================
    // Prompt
    promptEditor = std::make_unique<ExpandableEditor> ("prompt");
    promptEditor->setTooltip ("Double-click for a larger, zoomable editor");
    promptEditor->onExpand = [this] { openBigEditor (false); };
    promptEditor->setMultiLine (true, false);
    promptEditor->setReturnKeyStartsNewLine (true);
    promptEditor->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff080a08));
    promptEditor->setColour (juce::TextEditor::textColourId, OvenLookAndFeel::cream());
    promptEditor->setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff2a2e33));
    promptEditor->setColour (juce::TextEditor::focusedOutlineColourId, lime);
    promptEditor->setColour (juce::TextEditor::highlightColourId, lime.withAlpha (0.25f));
    promptEditor->setColour (juce::TextEditor::highlightedTextColourId, OvenLookAndFeel::cream());
    promptEditor->setFont (juce::Font (juce::FontOptions (14.5f)));
    promptEditor->setTextToShowWhenEmpty (
        "Describe the track, vocals, loops or drums you want...", lime.withAlpha (0.45f));
    addAndMakeVisible (*promptEditor);

    lyricsEditor = std::make_unique<ExpandableEditor> ("lyrics");
    lyricsEditor->setTooltip ("Double-click for a larger, zoomable editor");
    lyricsEditor->onExpand = [this] { openBigEditor (true); };
    lyricsEditor->setMultiLine (false, false);
    lyricsEditor->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff080a08));
    lyricsEditor->setColour (juce::TextEditor::textColourId, OvenLookAndFeel::cream());
    lyricsEditor->setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff2a2e33));
    lyricsEditor->setColour (juce::TextEditor::focusedOutlineColourId, lime);
    lyricsEditor->setFont (juce::Font (juce::FontOptions (14.0f)));
    lyricsEditor->setTextToShowWhenEmpty ("lyrics (optional - leave empty for instrumental)", lime.withAlpha (0.45f));
    addAndMakeVisible (*lyricsEditor);

    // Lyrics: write them yourself, or let the LM write from the prompt.
    autoLyricsButton = addAction ("AUTO", { 542.0f, 146.0f, 62.0f, 26.0f }, [this]
    {
        autoLyrics = ! autoLyrics;
        if (autoLyricsButton != nullptr)
            autoLyricsButton->setToggleState (autoLyrics, juce::dontSendNotification);
        processor.setStatusMessage (autoLyrics
                                        ? "AUTO LYRICS ON - EMPTY BOX IS WRITTEN FROM THE PROMPT"
                                        : "AUTO LYRICS OFF");
        saveState();
    });
    autoLyricsButton->setTooltip ("When the lyrics box is empty, write lyrics from the prompt before singing");

    writeLyricsButton = addAction ("WRITE", { 612.0f, 146.0f, 74.0f, 26.0f }, [this] { writeLyrics(); });
    writeLyricsButton->setTooltip ("Write lyrics now from the prompt, vibe and language");

    presetBox = std::make_unique<juce::ComboBox> ("master presets");
    presetBox->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff080a08));
    presetBox->setColour (juce::ComboBox::textColourId, OvenLookAndFeel::cream());
    presetBox->setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff2a2e33));
    presetBox->setColour (juce::ComboBox::arrowColourId, lime);
    presetBox->setTooltip ("Master presets: R&B and dark trap tracks, loops and vocals");
    addAndMakeVisible (*presetBox);

    presetBox->onChange = [this]
    {
        const int index = presetBox->getSelectedId() - 2;

        if (index >= 0 && index < masterPresets.size())
            applyPreset (index);
    };

    //==========================================================================
    // Generation controls
    languageBox = std::make_unique<juce::ComboBox> ("language");
    languageBox->addItemList ({ "English", "Spanish", "Portuguese", "French", "German",
                                "Japanese", "Korean", "Hindi", "Arabic", "Yoruba" }, 1);
    languageBox->setSelectedId (1, juce::dontSendNotification);
    languageBox->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff080a08));
    languageBox->setColour (juce::ComboBox::textColourId, lime);
    languageBox->setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff2a2e33));
    languageBox->setColour (juce::ComboBox::arrowColourId, lime);
    languageBox->setTooltip ("Language hint for generated lyrics and vocals");
    addAndMakeVisible (*languageBox);

    addAction ("LOOP", { 212.0f, 198.0f, 110.0f, 22.0f }, [this] { applyMode (0); });
    addAction ("FULL TRACK", { 330.0f, 198.0f, 130.0f, 22.0f }, [this] { applyMode (1); });
    addAction ("VOCAL ONLY", { 468.0f, 198.0f, 130.0f, 22.0f }, [this] { applyMode (2); });

    midiButton = addAction ("MIDI", { 610.0f, 198.0f, 62.0f, 22.0f }, [this]
    {
        generateMidi = ! generateMidi;

        if (midiButton != nullptr)
            midiButton->setToggleState (generateMidi, juce::dontSendNotification);

        processor.setStatusMessage (generateMidi
                                        ? "MIDI ON - GENERATE ALSO COMPOSES 3 SKETCHES"
                                        : "MIDI OFF");

        saveState();
    });
    midiButton->setTooltip ("Also compose three MIDI sketches from this prompt, then drag one into FL");

    durationSlider  = addControlKnob ("DURATION", { 18.0f, 494.0f, 100.0f, 84.0f });
    stepsSlider     = addControlKnob ("STEPS",    { 122.0f, 494.0f, 100.0f, 84.0f });
    guidanceSlider  = addControlKnob ("GUIDANCE", { 226.0f, 494.0f, 100.0f, 84.0f });
    bpmSlider       = addControlKnob ("BPM",      { 330.0f, 494.0f, 100.0f, 84.0f });
    seedSlider      = addControlKnob ("SEED",     { 434.0f, 494.0f, 100.0f, 84.0f });

    durationSlider->setRange (3.0, 60.0, 0.5);
    durationSlider->setValue (12.0);
    durationSlider->setTooltip ("Length of the generated take in seconds");

    stepsSlider->setRange (1.0, 50.0, 1.0);
    stepsSlider->setValue (8.0);
    stepsSlider->setTooltip ("DiT sampling steps (turbo default = 8)");

    guidanceSlider->setRange (0.0, 10.0, 0.1);
    guidanceSlider->setValue (1.0);
    guidanceSlider->setTooltip ("Classifier-free guidance scale");

    bpmSlider->setRange (0.0, 200.0, 1.0);
    bpmSlider->setValue (140.0);
    bpmSlider->setTooltip ("Tempo (0 = let the model decide)");

    seedSlider->setRange (-1.0, 2000000.0, 1.0);
    seedSlider->setValue (-1.0);
    seedSlider->setTooltip ("Seed (-1 = random)");

    generateButton = addAction ("GENERATE", { 18.0f, 592.0f, 140.0f, 42.0f },
                                [this] { generate(); });
    cancelButton   = addAction ("CANCEL",   { 164.0f, 592.0f, 100.0f, 42.0f },
                                [this] { engine.cancel(); }, true);
    vocalsButton   = addAction ("VOCALS",   { 270.0f, 592.0f, 120.0f, 42.0f }, nullptr);
    vocalsButton->setClickingTogglesState (true);
    vocalsButton->setColour (juce::TextButton::buttonOnColourId, OvenLookAndFeel::lime());
    vocalsButton->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    vocalsButton->setTooltip ("Write lyrics + sung vocals with the 4B LM");

    addAction ("TRAIN", { 400.0f, 592.0f, 100.0f, 42.0f },
               [this]
               {
                   // Prefer the current AI take. If there isn't one, capture the
                   // loop that is currently selected so TRAIN always does something.
                   juce::AudioBuffer<float> audio;
                   double rate = 48000.0;
                   juce::String kind;
                   juce::String prompt = processor.getAiResultPrompt();

                   if (processor.getAiResult().getNumSamples() >= 64)
                   {
                       audio = processor.getAiResult();
                       kind  = processor.getAiResultLyrics().isNotEmpty() ? "Vocals" : "Loops";
                   }
                   else
                   {
                       const int active = juce::jlimit (0, 2, (int) std::lround (
                           processor.apvts.getRawParameterValue ("active_loop")->load() * 2.0f));

                       if (processor.getLoopAudio (active, audio, rate))
                       {
                           kind = "Loops";
                           prompt = "Loop " + juce::String (active == 0 ? "A" : active == 1 ? "B" : "C");
                       }
                   }

                   if (audio.getNumSamples() < 64)
                   {
                       processor.setStatusMessage ("GENERATE A TAKE OR LOAD A LOOP FIRST, THEN TRAIN");
                       return;
                   }

                   const auto f = btpai::saveTrainingCapture (
                       audio, rate, kind, {}, prompt,
                       [&]
                       {
                           const auto lyrics = processor.getAiResultLyrics();
                           const bool vocal = lyrics.isNotEmpty()
                                              || (vocalsButton != nullptr && vocalsButton->getToggleState());

                           const int root  = (int) std::lround (
                               processor.apvts.getRawParameterValue ("key_root")->load());
                           const int scale = (int) std::lround (
                               processor.apvts.getRawParameterValue ("scale_type")->load());

                           auto* meta = new juce::DynamicObject();
                           meta->setProperty ("lyrics", lyrics.isNotEmpty() ? lyrics
                                               : (vocal && lyricsEditor != nullptr
                                                      ? lyricsEditor->getText()
                                                      : juce::String ("[Instrumental]")));
                           meta->setProperty ("bpm", bpmSlider != nullptr ? bpmSlider->getValue() : 0.0);
                           meta->setProperty ("keyscale", juce::String (btp::noteName (root))
                                                              + (scale == 0 ? " major" : " minor"));
                           meta->setProperty ("genre", "BtP Oven capture");
                           meta->setProperty ("is_instrumental", ! vocal);
                           return juce::var (meta);
                       }());

                   // Make sure the LoRA drop-folder exists so the picker is never
                   // silently empty, then rescan it.
                   btpai::getLoraDir().createDirectory();
                   refreshLoras();

                   processor.setStatusMessage (
                       f.existsAsFile()
                           ? juce::String ("CAPTURED ") + kind.toUpperCase()
                               + " -> " + f.getFullPathName()
                               + "  (PRESS TRAIN LORA)"
                           : "CAPTURE FAILED");
               });

    //==========================================================================
    // Engine
    addAction ("LOAD", { 720.0f, 72.0f, 120.0f, 30.0f },
               [this] { engine.startLoad(); });
    addAction ("UNLOAD", { 848.0f, 72.0f, 120.0f, 30.0f },
               [this] { engine.unload(); }, true);
    addAction ("MODEL DIR", { 720.0f, 110.0f, 150.0f, 30.0f },
               [this] { pickModelDir(); });
    addAction ("MAIN PAGE", { 998.0f, 10.0f, 138.0f, 30.0f },
               [this]
               {
                   if (onMainPage != nullptr)
                       onMainPage();
               });

    //==========================================================================
    // Loop C transport / tricks
    addParamButton ("c_play",    "C PLAY",    { 18.0f, 372.0f, 110.0f, 28.0f });
    addParamButton ("c_bypass",  "C BYPASS",  { 134.0f, 372.0f, 110.0f, 28.0f });
    addParamButton ("c_reverse", "REV",       { 252.0f, 372.0f, 84.0f, 28.0f });
    addParamButton ("c_half",    "1/2",       { 342.0f, 372.0f, 84.0f, 28.0f });
    addParamButton ("c_double",  "2X",        { 432.0f, 372.0f, 84.0f, 28.0f });
    addParamButton ("c_stutter", "STUT",      { 522.0f, 372.0f, 84.0f, 28.0f });

    addParamButton ("c_loop", "LOOP", { 900.0f, 332.0f, 90.0f, 28.0f });

    addAction ("TO A", { 700.0f, 372.0f, 104.0f, 28.0f },
               [this] { processor.sendAiResultToLoop (0); });
    addAction ("TO B", { 810.0f, 372.0f, 84.0f, 28.0f },
               [this] { processor.sendAiResultToLoop (1); });
    addAction ("TO C", { 900.0f, 372.0f, 84.0f, 28.0f },
               [this] { processor.sendAiResultToLoop (2); });
    addAction ("CLEAR C", { 990.0f, 372.0f, 140.0f, 28.0f },
               [this] { processor.clearLoop (2); }, true);

    // Region sliders for Loop C
    {
        auto start = std::make_unique<juce::Slider>();
        start->setSliderStyle (juce::Slider::LinearHorizontal);
        start->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        start->setRange (0.0, 1.0, 0.001);
        start->setColour (juce::Slider::trackColourId, lime.withAlpha (0.55f));
        start->setColour (juce::Slider::thumbColourId, lime);
        start->setTooltip ("Loop C start");
        addAndMakeVisible (*start);
        sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.apvts, "c_start", *start));
        regionDesigns.push_back ({ 18.0f, 338.0f, 430.0f, 16.0f });
        regionSliders.push_back (std::move (start));

        regionSliders.back()->onValueChange = [this] { clampRegion (true); };

        auto end = std::make_unique<juce::Slider>();
        end->setSliderStyle (juce::Slider::LinearHorizontal);
        end->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        end->setRange (0.0, 1.0, 0.001);
        end->setColour (juce::Slider::trackColourId, teal.withAlpha (0.55f));
        end->setColour (juce::Slider::thumbColourId, teal);
        end->setTooltip ("Loop C end");
        addAndMakeVisible (*end);
        sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.apvts, "c_end", *end));
        regionDesigns.push_back ({ 456.0f, 338.0f, 430.0f, 16.0f });
        regionSliders.push_back (std::move (end));

        regionSliders.back()->onValueChange = [this] { clampRegion (false); };
    }

    //==========================================================================
    // Dataset manager
    datasetList = std::make_unique<juce::ListBox> ("datasets");
    datasetList->setModel (&datasetModel);
    datasetList->setRowHeight (26);
    datasetList->setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff080a08));
    datasetList->setColour (juce::ListBox::outlineColourId, juce::Colour (0xff2a2e33));
    addAndMakeVisible (*datasetList);

    addAction ("NEW",  { 580.0f, 612.0f, 120.0f, 26.0f }, [this] { newDataset(); });
    addAction ("ADD",  { 708.0f, 612.0f, 140.0f, 26.0f }, [this] { addFilesToDataset(); });
    addAction ("OPEN", { 856.0f, 612.0f, 120.0f, 26.0f }, [this] { openDatasetFolder(); });

    loraBox = std::make_unique<juce::ComboBox> ("lora");
    loraBox->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff101215));
    loraBox->setColour (juce::ComboBox::textColourId, lime);
    loraBox->setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    loraBox->setColour (juce::ComboBox::arrowColourId, lime);
    loraBox->setTooltip ("LoRAs found in " + btpai::getLoraDir().getFullPathName());
    addAndMakeVisible (*loraBox);

    loraScaleSlider = std::make_unique<juce::Slider>();
    loraScaleSlider->setSliderStyle (juce::Slider::LinearHorizontal);
    loraScaleSlider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 13);
    loraScaleSlider->setRange (0.0, 2.0, 0.05);
    loraScaleSlider->setValue (currentModels.loraScale);
    loraScaleSlider->setColour (juce::Slider::textBoxTextColourId, lime.withAlpha (0.85f));
    loraScaleSlider->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    loraScaleSlider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    loraScaleSlider->setTooltip ("LoRA strength");
    addAndMakeVisible (*loraScaleSlider);

    addAction ("LORA DIR", { 914.0f, 644.0f, 100.0f, 28.0f },
               [this]
               {
                   auto dir = btpai::getLoraDir();

                   if (! dir.isDirectory())
                       dir.createDirectory();

                   dir.revealToUser();
                   refreshLoras();

                   processor.setStatusMessage ("LORA FOLDER -> " + dir.getFullPathName()
                                               + "  (DROP .SAFETENSORS HERE)");
               });

    addAction ("APPLY", { 1020.0f, 644.0f, 112.0f, 28.0f }, [this] { applyLora(); });

    loraTrainButton = addAction ("TRAIN LORA", { 1004.0f, 612.0f, 132.0f, 26.0f },
                                 [this] { openLoraTrainDialog(); });
    loraTrainButton->setTooltip ("Turn captured Loops / Vocals into a LoRA adapter");

    loraDialog = std::make_unique<LoraTrainDialog> (*this);
    loraDialog->setVisible (false);
    addChildComponent (*loraDialog);

    refreshDatasets();
    refreshLoras();
    loadMasterPresets();

    restoreState();

    startTimerHz (10);
}

AiPage::~AiPage()
{
    // Capture the working state while the widgets still exist.
    saveState();

    stopTimer();

    // Tear down in dependency order: attachments and lists before the widgets
    // they point at. Default member order destroys the attachments after their
    // sliders, and the list model before its ListBox, which crashes on exit.
    sliderAttachments.clear();
    buttonAttachments.clear();

    datasetList.reset();

    loraDialog.reset();

    loraScaleSlider.reset();
    loraBox.reset();
    loraScaleLabel.reset();

    presetBox.reset();
    lyricsEditor.reset();
    promptEditor.reset();

    for (auto& slider : regionSliders)
        slider.reset();
    regionSliders.clear();

    knobs.clear();
    buttons.clear();
}

//==============================================================================
juce::Rectangle<int> AiPage::R (float x, float y, float w, float h) const
{
    return { juce::roundToInt (x * sc), juce::roundToInt (y * sc),
             juce::roundToInt (w * sc), juce::roundToInt (h * sc) };
}

//==============================================================================
juce::Slider* AiPage::addControlKnob (const juce::String& name, juce::Rectangle<float> design)
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

juce::TextButton* AiPage::addAction (const juce::String& text, juce::Rectangle<float> design,
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

juce::TextButton* AiPage::addParamButton (const juce::String& parameterId, const juce::String& text,
                                          juce::Rectangle<float> design, bool danger)
{
    auto* button = addAction (text, design, nullptr, danger);
    button->setClickingTogglesState (true);
    button->setColour (juce::TextButton::buttonOnColourId, OvenLookAndFeel::lime());
    button->setColour (juce::TextButton::textColourOnId, juce::Colours::black);

    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, parameterId, *button));

    return button;
}

//==============================================================================
void AiPage::refreshDatasets()
{
    datasets = btpai::scanDatasets();
    selectedDataset = juce::jlimit (0, juce::jmax (0, datasets.size() - 1), selectedDataset);
    datasetList->updateContent();
}

void AiPage::refreshLoras()
{
    loras = btpai::findLoras();

    loraBox->clear();
    loraBox->addItem ("NO LORA", 1);

    int selectedId = 1;

    for (int i = 0; i < loras.size(); ++i)
    {
        const int id = i + 2;
        loraBox->addItem (juce::File (loras[i]).getFileName(), id);

        if (currentModels.lora.getFullPathName() == loras[i])
            selectedId = id;
    }

    loraBox->setSelectedId (selectedId, juce::dontSendNotification);
}

//==============================================================================
void AiPage::loadMasterPresets()
{
    masterPresets.clear();

    if (presetBox == nullptr)
        return;

    presetBox->clear();
    presetBox->addItem ("LOAD A PRESET  (R&B / DARK TRAP)", 1);

    // Installed location first, then the user-editable copy, then the build tree.
    juce::Array<juce::File> candidates;
    candidates.add (btppaths::getTrainingDir().getChildFile ("manifests").getChildFile ("master.json"));
    candidates.add (btppaths::getConfigDir().getChildFile ("master.json"));
    candidates.add (juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                        .getParentDirectory().getChildFile ("manifests").getChildFile ("master.json"));
    candidates.add (juce::File::getCurrentWorkingDirectory().getChildFile ("Packaging")
                        .getChildFile ("manifests").getChildFile ("master.json"));

    juce::File source;

    for (const auto& f : candidates)
    {
        if (f.existsAsFile())
        {
            source = f;
            break;
        }
    }

    if (! source.existsAsFile())
    {
        presetBox->setSelectedId (1, juce::dontSendNotification);
        return;
    }

    const auto parsed = juce::JSON::parse (source);

    if (auto* root = parsed.getDynamicObject())
    {
        const auto presets = root->getProperty ("presets");

        if (auto* array = presets.getArray())
        {
            for (const auto& entry : *array)
            {
                auto* obj = entry.getDynamicObject();

                if (obj == nullptr)
                    continue;

                masterPresets.add (entry);

                const auto category = obj->getProperty ("category").toString().toUpperCase();
                const auto name = obj->getProperty ("name").toString();

                presetBox->addItem ("[" + category + "]  " + name,
                                    masterPresets.size() + 1);
            }
        }
    }

    presetBox->setSelectedId (1, juce::dontSendNotification);
    presetBox->setTooltip ("Loaded " + juce::String (masterPresets.size())
                           + " presets from " + source.getFullPathName());
}

void AiPage::applyPreset (int index)
{
    if (index < 0 || index >= masterPresets.size())
        return;

    auto* obj = masterPresets[index].getDynamicObject();

    if (obj == nullptr)
        return;

    promptEditor->setText (obj->getProperty ("prompt").toString(), false);
    lyricsEditor->setText (obj->getProperty ("lyrics").toString(), false);

    if (obj->hasProperty ("duration"))
        durationSlider->setValue ((double) obj->getProperty ("duration"), juce::dontSendNotification);

    if (obj->hasProperty ("steps"))
        stepsSlider->setValue ((double) obj->getProperty ("steps"), juce::dontSendNotification);

    if (obj->hasProperty ("guidance"))
        guidanceSlider->setValue ((double) obj->getProperty ("guidance"), juce::dontSendNotification);

    if (obj->hasProperty ("bpm"))
        bpmSlider->setValue ((double) obj->getProperty ("bpm"), juce::dontSendNotification);

    if (obj->hasProperty ("seed"))
        seedSlider->setValue ((double) obj->getProperty ("seed"), juce::dontSendNotification);

    if (vocalsButton != nullptr)
        vocalsButton->setToggleState ((double) obj->getProperty ("with_vocals") != 0.0,
                                      juce::dontSendNotification);

    processor.setStatusMessage ("PRESET -> " + obj->getProperty ("name").toString().toUpperCase());
}

void AiPage::applyMode (int mode)
{
    if (durationSlider == nullptr || stepsSlider == nullptr)
        return;

    if (mode == 0)          // loopable melody
    {
        durationSlider->setValue (8.0);
        stepsSlider->setValue (8.0);
        modePrefix = "short loopable melody, no vocals";
        if (vocalsButton != nullptr)
            vocalsButton->setToggleState (false, juce::sendNotification);
    }
    else if (mode == 1)     // full track
    {
        durationSlider->setValue (30.0);
        stepsSlider->setValue (10.0);
        modePrefix = "full arrangement";
        if (vocalsButton != nullptr)
            vocalsButton->setToggleState (true, juce::sendNotification);
    }
    else                    // vocal only
    {
        durationSlider->setValue (12.0);
        stepsSlider->setValue (8.0);
        modePrefix = "a cappella lead vocal, instrumental stripped back";
        if (vocalsButton != nullptr)
            vocalsButton->setToggleState (true, juce::sendNotification);
    }

    processor.setStatusMessage (modePrefix.toUpperCase());
}

//==============================================================================
void AiPage::clampRegion (bool startMoved)
{
    auto* startParam = processor.apvts.getParameter ("c_start");
    auto* endParam   = processor.apvts.getParameter ("c_end");

    if (startParam == nullptr || endParam == nullptr)
        return;

    // Parameter values are normalised because the ranges are 0..1.
    const float start = startParam->getValue();
    const float end   = endParam->getValue();
    const float minGap = 0.01f;   // at least 1% of the file

    if (start <= end - minGap)
        return;

    if (startMoved)
        endParam->setValueNotifyingHost (juce::jmin (1.0f, start + minGap));
    else
        startParam->setValueNotifyingHost (juce::jmax (0.0f, end - minGap));
}

//==============================================================================
juce::String AiPage::languageCode() const
{
    if (languageBox == nullptr)
        return {};

    // Codes the ACE-Step FSM accepts. Anything absent means "let the model
    // choose", which is honest: a name we cannot map would silently do nothing.
    const auto name = languageBox->getText();

    if (name == "English")    return "en";
    if (name == "Spanish")    return "es";
    if (name == "Portuguese") return "pt";
    if (name == "French")     return "fr";
    if (name == "German")     return "de";
    if (name == "Japanese")   return "ja";
    if (name == "Korean")     return "ko";
    if (name == "Hindi")      return "hi";
    if (name == "Arabic")     return "ar";

    return {};   // Yoruba has no code in this model; the LM will choose.
}

//==============================================================================
void AiPage::generate()
{
    if (! AiEngine::isCompiledIn())
    {
        processor.setStatusMessage ("AI ENGINE NOT COMPILED INTO THIS BUILD");
        return;
    }

    // AUTO writes lyrics first when the box is empty, then sings them. A box
    // the user has filled in is never overwritten behind their back.
    const bool hasLyrics = lyricsEditor != nullptr
                           && lyricsEditor->getText().trim().isNotEmpty();

    if (autoLyrics && ! hasLyrics && promptEditor->getText().trim().isNotEmpty())
    {
        pendingAudioAfterLyrics = true;
        writeLyrics();
        return;
    }

    submitAudio();

    if (generateMidi)
        requestMidi();
}

void AiPage::requestMidi()
{
    if (promptEditor->getText().trim().isEmpty())
        return;

    const int bpm = juce::jmax (40, juce::roundToInt (bpmSlider->getValue()));
    const double seconds = juce::jmax (1.0, durationSlider->getValue());

    // Keep the sketch roughly as long as the audio being generated.
    const int bars = juce::jlimit (1, 64,
                                   juce::roundToInt (seconds * bpm / 60.0 / 4.0));

    auto seed = (juce::int64) std::llround (seedSlider->getValue());
    if (seed < 0)
        seed = (juce::int64) (juce::Time::currentTimeMillis() % 1000000);

    processor.requestMidi (promptEditor->getText(), bpm, bars, (int) seed, 3);

    if (midiWindow == nullptr)
        openMidiWindow();
}

void AiPage::openMidiWindow()
{
    if (midiWindow == nullptr)
    {
        midiWindow = std::make_unique<AiMidiWindow>();
        midiWindow->onCompose = [this]
        {
            if (! engine.isBusy())
                requestMidi();
        };
    }

    midiWindow->refresh (processor.getMidiResult(), processor.getMidiStatus());
    midiWindow->setVisible (true);
    midiWindow->toFront (true);
}

void AiPage::writeLyrics()
{
    if (! AiEngine::isCompiledIn())
    {
        processor.setStatusMessage ("AI ENGINE NOT COMPILED INTO THIS BUILD");
        return;
    }

    if (promptEditor->getText().trim().isEmpty())
    {
        processor.setStatusMessage ("ADD A PROMPT FIRST - THE WRITER NEEDS A VIBE TO WORK FROM");
        return;
    }

    if (engine.isBusy())
        return;

    AiEngine::Job job;
    job.kind     = AiEngine::Kind::Lyrics;
    job.prompt   = modePrefix.isNotEmpty() ? modePrefix + ", " + promptEditor->getText()
                                           : promptEditor->getText();
    job.duration = (float) durationSlider->getValue();
    job.steps    = juce::roundToInt (stepsSlider->getValue());
    job.guidance = (float) guidanceSlider->getValue();
    job.bpm      = juce::roundToInt (bpmSlider->getValue());
    job.seed     = (juce::int64) std::llround (seedSlider->getValue());
    job.language = languageCode();

    engine.submit (job);
    processor.setStatusMessage ("WRITING LYRICS...");
}

void AiPage::submitAudio()
{
    AiEngine::Job job;
    job.prompt   = promptEditor->getText();
    job.lyrics   = lyricsEditor->getText();
    job.duration = (float) durationSlider->getValue();
    job.steps    = juce::roundToInt (stepsSlider->getValue());
    job.guidance = (float) guidanceSlider->getValue();
    job.shift    = 3.0f;
    job.bpm      = juce::roundToInt (bpmSlider->getValue());
    job.seed     = (juce::int64) std::llround (seedSlider->getValue());
    job.language = languageCode();

    if (modePrefix.isNotEmpty())
        job.prompt = modePrefix + ", " + job.prompt;

    job.withVocals = vocalsButton != nullptr && vocalsButton->getToggleState();

    const int root  = (int) std::lround (processor.apvts.getRawParameterValue ("key_root")->load());
    const int scale = (int) std::lround (processor.apvts.getRawParameterValue ("scale_type")->load());
    job.keyscale = juce::String (btp::noteName (root)) + (scale == 0 ? " major" : " minor");

    engine.submit (job);

    processor.setStatusMessage ("AI GEN STARTED - COOKING LOOP C");
}

void AiPage::applyLora()
{
    currentModels.lora = juce::File();
    currentModels.loraScale = 1.0f;

    const int id = loraBox->getSelectedId();

    if (id >= 2 && id - 2 < loras.size())
        currentModels.lora = juce::File (loras[id - 2]);

    currentModels.loraScale = (float) loraScaleSlider->getValue();

    engine.setModels (currentModels);

    const bool hasLora = currentModels.lora.existsAsFile() || currentModels.lora.isDirectory();

    processor.setStatusMessage (hasLora
                                    ? "LORA APPLIED - RELOADS ON NEXT RUN"
                                    : "LORA CLEARED");
}

void AiPage::openBigEditor (bool forLyrics)
{
    auto* source = forLyrics ? static_cast<juce::TextEditor*> (lyricsEditor.get())
                             : static_cast<juce::TextEditor*> (promptEditor.get());

    if (source == nullptr)
        return;

    if (bigEditor == nullptr)
        bigEditor = std::make_unique<AiBigEditor>();

    bigEditor->open (forLyrics ? "LYRICS" : "PROMPT", source->getText(),
                     [this, forLyrics] (const juce::String& text)
                     {
                         if (forLyrics)
                         {
                             if (lyricsEditor != nullptr)
                                 lyricsEditor->setText (text, false);
                         }
                         else if (promptEditor != nullptr)
                         {
                             promptEditor->setText (text, false);
                         }

                         saveState();
                     });
}

void AiPage::openLoraTrainDialog()
{
    if (loraDialog == nullptr)
        return;

    loraDialog->setVisible (true);
    loraDialog->toFront (true);
    loraDialog->focusName();
}

void AiPage::startLoraTraining()
{
    if (loraTraining)
    {
        processor.setStatusMessage ("LORA TRAINING ALREADY RUNNING - WATCH THE CONSOLE");
        return;
    }

    auto name = loraDialog != nullptr ? loraDialog->getName() : juce::String();
    name = name.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-")
               .trim().replace (" ", "_");

    if (name.isEmpty())
        name = "lora_" + juce::String (juce::Time::getCurrentTime().toMilliseconds());

    static const char* const kinds[] = { "Loops", "Vocals", "OneShots", "Variants", "Stems" };
    const int kindIndex = loraDialog != nullptr ? juce::jlimit (0, 4, loraDialog->getDataIndex()) : 0;

    const auto dataDir = btpai::getTrainingDir().getChildFile (kinds[kindIndex]);
    const auto info = btpai::scanDataset (dataDir);

    if (info.numFiles <= 0)
    {
        processor.setStatusMessage (juce::String ("NO CAPTURES IN ") + kinds[kindIndex]
                                    + " - PRESS TRAIN FIRST");
        return;
    }

    const auto script = btpai::findLoraTrainerScript();

    if (! script.existsAsFile())
    {
        processor.setStatusMessage ("TRAINER MISSING: Tools\\train_lora.py - REINSTALL OR SEE TRAINING GUIDE");
        return;
    }

    const auto root = btpai::findAceStepRoot();

    if (! root.isDirectory())
    {
        processor.setStatusMessage ("ACE-STEP 1.5 NOT FOUND - SEE TRAINING GUIDE (LORA SECTION)");
        return;
    }

    const auto python = btpai::findLoraPython (root);

    if (! python.existsAsFile())
    {
        processor.setStatusMessage ("PYTHON FOR LORA TRAINING NOT FOUND - SEE TRAINING GUIDE");
        return;
    }

    const int epochs = loraDialog != nullptr ? loraDialog->getEpochs() : 40;
    const int rank   = loraDialog != nullptr ? loraDialog->getRank() : 16;

    auto loraRoot = btpai::getLoraDir();
    loraRoot.createDirectory();

    const auto outDir = loraRoot.getChildFile (name);
    outDir.createDirectory();

    auto runs = btpai::getTrainingDir().getChildFile ("runs");
    runs.createDirectory();

    const auto bat = runs.getChildFile ("lora_" + name + ".bat");

    juce::StringArray lines;
    lines.add ("@echo off");
    lines.add ("title BtP Oven - LoRA training - " + name);
    lines.add ("echo BtP Oven LoRA training: " + name);
    lines.add ("echo.");
    lines.add ("cd /d \"" + root.getFullPathName() + "\"");
    lines.add ("\"" + python.getFullPathName() + "\" \"" + script.getFullPathName() + "\""
               + " --name \"" + name + "\""
               + " --audio-dir \"" + dataDir.getFullPathName() + "\""
               + " --output-dir \"" + loraRoot.getFullPathName() + "\""
               + " --acestep-root \"" + root.getFullPathName() + "\""
               + " --python \"" + python.getFullPathName() + "\""
               + " --epochs " + juce::String (epochs)
               + " --rank " + juce::String (rank)
               + " --alpha " + juce::String (rank * 2));
    lines.add ("echo.");
    lines.add ("echo ---------------------------------------------");
    lines.add ("echo Training finished. Press a key to close.");
    lines.add ("pause >nul");

    if (! bat.replaceWithText (lines.joinIntoString ("\r\n"), false, false, "\r\n"))
    {
        processor.setStatusMessage ("COULD NOT WRITE TRAINING SCRIPT -> " + bat.getFullPathName());
        return;
    }

    const juce::String command = "cmd.exe /c start \"BtP Oven LoRA\" \""
                                 + bat.getFullPathName() + "\"";

    if (! loraLauncher.start (command))
    {
        processor.setStatusMessage ("COULD NOT LAUNCH TRAINER - RUN " + bat.getFullPathName());
        return;
    }

    pendingLoraDir = outDir;
    pendingLoraName = name;
    loraTrainingStartMs = juce::Time::getCurrentTime().toMilliseconds();
    loraTraining = true;

    if (loraDialog != nullptr)
        loraDialog->setVisible (false);

    if (loraTrainButton != nullptr)
        loraTrainButton->setButtonText ("TRAINING...");

    processor.setStatusMessage ("LORA TRAINING STARTED: " + name + "  ("
                                + juce::String (info.numFiles) + " FILES, "
                                + juce::String (epochs) + " EPOCHS) - CONSOLE OPEN");
}

void AiPage::pickModelDir()
{
    auto safe = juce::Component::SafePointer<AiPage> (this);

    juce::FileChooser chooser ("Choose the ACE-Step model folder",
                               currentModels.dit.getParentDirectory(),
                               "*");

    chooser.launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
                         [safe] (const juce::FileChooser& fc)
                         {
                             if (safe == nullptr)
                                 return;

                             const auto dir = fc.getResult();

                             if (! dir.isDirectory())
                                 return;

                             auto m = safe->currentModels;

                             auto setIf = [&dir] (juce::File& dst, const juce::String& pattern)
                             {
                                 const auto f = pick (dir, pattern);
                                 if (f != juce::File())
                                     dst = f;
                             };

                             setIf (m.dit, "acestep-v15-turbo*.gguf");
                             setIf (m.textEncoder, "Qwen3-Embedding*.gguf");
                             setIf (m.vae, "vae-*.gguf");
                             setIf (m.lm, "acestep-5Hz-lm*.gguf");

                             if (! m.isComplete())
                             {
                                 safe->processor.setStatusMessage ("MODEL FOLDER INCOMPLETE - NEED TURBO + EMBEDDING + VAE");
                                 return;
                             }

                             safe->currentModels = m;
                             safe->engine.setModels (m);
                             safe->processor.setStatusMessage ("MODEL FOLDER SET - " + dir.getFullPathName());
                         });
}

void AiPage::newDataset()
{
    const int n = datasets.size() + 1;
    btpai::createDataset ("Dataset " + juce::String (n));
    refreshDatasets();
    selectedDataset = datasets.size() - 1;
    datasetList->updateContent();
    processor.setStatusMessage ("DATASET " + juce::String (n) + " CREATED - DRAG AUDIO ONTO THIS PAGE");
}

void AiPage::addFilesToDataset()
{
    if (datasets.isEmpty())
    {
        processor.setStatusMessage ("CREATE A DATASET FIRST");
        return;
    }

    auto safe = juce::Component::SafePointer<AiPage> (this);

    juce::FileChooser chooser ("Add audio to dataset",
                               juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                               "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");

    chooser.launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                         [safe] (const juce::FileChooser& fc)
                         {
                             if (safe == nullptr)
                                 return;

                             const auto files = fc.getResults();

                             if (files.isEmpty())
                                 return;

                             const int idx = safe->selectedDataset;

                             if (idx < 0 || idx >= safe->datasets.size())
                                 return;

                             juce::StringArray paths;

                             for (const auto& f : files)
                                 paths.add (f.getFullPathName());

                             const int landed = btpai::importFilesIntoDataset (safe->datasets[idx].folder, paths);
                             safe->refreshDatasets();
                             safe->processor.setStatusMessage (juce::String (landed)
                                                                 + " FILES ADDED TO " + safe->datasets[idx].name);
                         });
}

void AiPage::openDatasetFolder()
{
    if (selectedDataset < 0 || selectedDataset >= datasets.size())
        return;

    datasets[selectedDataset].folder.revealToUser();
}

//==============================================================================
bool AiPage::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (btpai::isSupportedAudio (f))
            return true;

    return false;
}

void AiPage::fileDragEnter (const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void AiPage::fileDragExit (const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void AiPage::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;

    if (files.isEmpty())
        return;

    if (datasets.isEmpty())
    {
        btpai::createDataset ("My Dataset");
        refreshDatasets();
        selectedDataset = datasets.size() - 1;
    }

    if (selectedDataset < 0 || selectedDataset >= datasets.size())
        return;

    juce::StringArray paths;

    for (const auto& f : files)
        if (btpai::isSupportedAudio (f))
            paths.add (f);

    const int landed = btpai::importFilesIntoDataset (datasets[selectedDataset].folder, paths);
    refreshDatasets();

    processor.setStatusMessage (juce::String (landed) + " FILES IMPORTED TO "
                                + datasets[selectedDataset].name);
}

//==============================================================================
void AiPage::resized()
{
    sc = (float) getWidth() / kWidth;

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

    for (auto& b : buttons)
    {
        if (b.button == nullptr)
            continue;

        b.button->setBounds (R (b.design.getX(), b.design.getY(),
                                b.design.getWidth(), b.design.getHeight()));
    }

    for (int i = 0; i < regionSliders.size(); ++i)
    {
        regionSliders[(size_t) i]->setBounds (R (regionDesigns[(size_t) i].getX(),
                                                  regionDesigns[(size_t) i].getY(),
                                                  regionDesigns[(size_t) i].getWidth(),
                                                  regionDesigns[(size_t) i].getHeight()));
    }

    promptEditor->setBounds (R (14.0f, 32.0f, 672.0f, 94.0f));
    lyricsEditor->setBounds (R (14.0f, 146.0f, 522.0f, 26.0f));

    if (presetBox != nullptr)
        presetBox->setBounds (R (14.0f, 174.0f, 672.0f, 18.0f));

    if (languageBox != nullptr)
        languageBox->setBounds (R (14.0f, 198.0f, 190.0f, 22.0f));

    datasetList->setBounds (R (580.0f, 488.0f, 554.0f, 96.0f));
    loraBox->setBounds (R (566.0f, 644.0f, 240.0f, 28.0f));
    loraScaleSlider->setBounds (R (812.0f, 644.0f, 96.0f, 28.0f));

    promptPanel  = juce::Rectangle<float> (0.0f, 0.0f, 700.0f, 196.0f) * sc;
    enginePanel  = juce::Rectangle<float> (706.0f, 0.0f, 442.0f, 196.0f) * sc;
    lanePanel    = juce::Rectangle<float> (0.0f, 202.0f, 1148.0f, 248.0f) * sc;
    genPanel     = juce::Rectangle<float> (0.0f, 458.0f, 560.0f, 234.0f) * sc;
    datasetPanel = juce::Rectangle<float> (566.0f, 458.0f, 582.0f, 234.0f) * sc;
    waveformArea = juce::Rectangle<float> (16.0f, 236.0f, 1116.0f, 90.0f) * sc;

    if (loraDialog != nullptr)
        loraDialog->setBounds (getLocalBounds());
}

//==============================================================================
void AiPage::paint (juce::Graphics& g)
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

    {
        juce::Random rng (0xb0a7);
        g.setColour (juce::Colours::white.withAlpha (0.012f));

        for (int i = 0; i < 800; ++i)
            g.fillRect ((float) rng.nextInt (juce::jmax (1, getWidth())),
                        (float) rng.nextInt (juce::jmax (1, getHeight())),
                        1.0f, 1.0f);
    }

    paintPanel (g, { 0.0f, 0.0f, 700.0f, 196.0f }, "PROMPT", "DESCRIBE TRACK / VOCALS / LOOPS / DRUMS");
    paintPanel (g, { 706.0f, 0.0f, 442.0f, 196.0f }, "ENGINE", "IN-PROCESS ACE-STEP - NO PYTHON / NO SERVER");
    paintPanel (g, { 0.0f, 202.0f, 1148.0f, 248.0f }, "LOOP C  -  AI TAKE",
                "BYPASS KEEPS IT AUDIBLE BUT SKIPS COOK / TIMING / PITCH TRICKS");
    paintPanel (g, { 0.0f, 458.0f, 560.0f, 234.0f }, "GENERATE");
    paintPanel (g, { 566.0f, 458.0f, 582.0f, 234.0f }, "DATASETS + LORA", "DROP AUDIO HERE TO TRAIN YOUR OWN SOUND");

    // Lyrics label
    g.setColour (OvenLookAndFeel::cream().withAlpha (0.5f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.5f, 9.0f * sc), juce::Font::bold)));
    g.drawText ("LYRICS", R (14.0f, 132.0f, 200.0f, 14.0f), juce::Justification::centredLeft, false);

    paintWaveform (g, waveformArea);
    paintEngineStatus (g, enginePanel);

    // Key / scale hint
    const int root  = (int) std::lround (processor.apvts.getRawParameterValue ("key_root")->load());
    const int scale = (int) std::lround (processor.apvts.getRawParameterValue ("scale_type")->load());

    g.setColour (lime.withAlpha (0.5f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.5f, 9.0f * sc), juce::Font::bold)));
    g.drawText (juce::String (btp::noteName (root)) + (scale == 0 ? " MAJ" : " MIN"),
                R (506.0f, 604.0f, 52.0f, 14.0f), juce::Justification::centredLeft, false);

    // LoRA scale label
    g.setColour (OvenLookAndFeel::cream().withAlpha (0.5f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.0f, 8.5f * sc), juce::Font::bold)));
    g.drawText ("SCALE", R (812.0f, 634.0f, 96.0f, 12.0f), juce::Justification::centredLeft, false);

    // Dataset summary
    if (selectedDataset >= 0 && selectedDataset < datasets.size())
    {
        g.setColour (lime.withAlpha (0.55f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.5f, 9.0f * sc), juce::Font::bold)));
        g.drawText (datasets[selectedDataset].getSummary(),
                    R (580.0f, 590.0f, 554.0f, 16.0f), juce::Justification::centredLeft, false);
    }

    // Drag highlight
    if (dragOver)
    {
        g.setColour (lime.withAlpha (0.9f));
        g.drawRoundedRectangle (datasetPanel.reduced (4.0f), 8.0f * sc, 2.5f * sc);
        g.setColour (lime.withAlpha (0.08f));
        g.fillRoundedRectangle (datasetPanel.reduced (4.0f), 8.0f * sc);
    }
}

//==============================================================================
void AiPage::paintPanel (juce::Graphics& g, juce::Rectangle<float> design,
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
void AiPage::paintWaveform (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto lime = OvenLookAndFeel::lime();
    const auto teal = juce::Colour (0xff8fe6c8);

    g.setColour (juce::Colour (0xff060906).withAlpha (0.75f));
    g.fillRoundedRectangle (area, 4.0f * sc);

    g.setColour (juce::Colour (0xff262a30));
    g.drawRoundedRectangle (area, 4.0f * sc, 1.0f * sc);

    // Eighth-note grid
    g.setColour (juce::Colours::white.withAlpha (0.045f));
    for (int i = 1; i < 8; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / 8.0f;
        g.fillRect (x, area.getY() + 1.0f, 1.0f, area.getHeight() - 2.0f);
    }

    const bool hasContent = processor.hasLoopContent (2);

    const float regionStart = juce::jlimit (0.0f, 1.0f,
                                            processor.apvts.getRawParameterValue ("c_start")->load());
    const float regionEnd = juce::jlimit (regionStart, 1.0f,
                                          processor.apvts.getRawParameterValue ("c_end")->load());

    // Region shading
    {
        const float rx0 = area.getX() + regionStart * area.getWidth();
        const float rx1 = area.getX() + regionEnd * area.getWidth();

        g.setColour (juce::Colours::black.withAlpha (0.46f));

        if (rx0 > area.getX())
            g.fillRect (juce::Rectangle<float> (area.getX(), area.getY(),
                                                rx0 - area.getX(), area.getHeight()));

        if (rx1 < area.getRight())
            g.fillRect (juce::Rectangle<float> (rx1, area.getY(),
                                                area.getRight() - rx1, area.getHeight()));

        g.setColour (teal.withAlpha (0.55f));
        g.fillRect (rx0 - 0.5f * sc, area.getY(), 1.0f * sc, area.getHeight());
        g.fillRect (rx1 - 0.5f * sc, area.getY(), 1.0f * sc, area.getHeight());
    }

    if (hasContent)
    {
        juce::Path wave;
        const float mid = area.getCentreY();
        const float half = area.getHeight() * 0.5f - 2.0f * sc;

        for (int i = 0; i < kPeakCount; ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / (float) (kPeakCount - 1);
            const float p = juce::jlimit (0.0f, 1.0f, processor.getWaveformPeak (2, i, kPeakCount));
            const float shimmer = std::sin (x * 0.035f + shimmerTime * 3.4f) * 1.7f * sc;
            const float y = mid - p * half + shimmer;

            if (i == 0)
                wave.startNewSubPath (x, y);
            else
                wave.lineTo (x, y);
        }

        for (int i = kPeakCount - 1; i >= 0; --i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / (float) (kPeakCount - 1);
            const float p = juce::jlimit (0.0f, 1.0f, processor.getWaveformPeak (2, i, kPeakCount));
            const float shimmer = std::sin (x * 0.035f + shimmerTime * 3.4f) * 1.7f * sc;
            wave.lineTo (x, mid + p * half + shimmer);
        }

        wave.closeSubPath();

        juce::ColourGradient waveGrad (teal.withAlpha (0.85f), 0.0f, mid,
                                       teal.withAlpha (0.10f), 0.0f, area.getBottom(), true);
        g.setGradientFill (waveGrad);
        g.fillPath (wave);

        g.setColour (teal.withAlpha (0.16f));
        g.strokePath (wave, juce::PathStrokeType (5.0f * sc, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        g.setColour (teal.withAlpha (0.95f));
        g.strokePath (wave, juce::PathStrokeType (1.3f * sc, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        const float phase = juce::jlimit (0.0f, 1.0f, processor.getPlayheadPhase (2));
        const float px = area.getX() + phase * area.getWidth();

        g.setColour (OvenLookAndFeel::cream().withAlpha (0.10f));
        g.fillRect (px - 2.5f * sc, area.getY(), 5.0f * sc, area.getHeight());
        g.setColour (OvenLookAndFeel::cream().withAlpha (0.8f));
        g.fillRect (px - 0.7f * sc, area.getY(), 1.4f * sc, area.getHeight());
    }
    else
    {
        g.setColour (OvenLookAndFeel::cream().withAlpha (0.16f));
        g.setFont (juce::Font (juce::FontOptions (12.0f * sc, juce::Font::bold)));
        g.drawText ("NO AI TAKE YET - PRESS GENERATE",
                    area.toNearestInt(), juce::Justification::centred, false);
    }

    // Info: seconds + seed
    g.setColour (OvenLookAndFeel::cream().withAlpha (0.4f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.0f, 8.5f * sc), juce::Font::bold)));

    juce::String info = hasContent
        ? juce::String (processor.getLoopLengthSeconds (2), 1) + " s"
        : "0.0 s";

    if (processor.hasAiResult())
        info += "  /  SEED " + (processor.getAiResultSeed() < 0
                                    ? juce::String ("RND")
                                    : juce::String (processor.getAiResultSeed()));

    g.drawText (info, R (998.0f, 334.0f, 130.0f, 24.0f), juce::Justification::centredRight, false);
}

//==============================================================================
void AiPage::paintEngineStatus (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto lime = OvenLookAndFeel::lime();
    const auto state = engine.getState();

    juce::Colour stateColour = lime;

    switch (state)
    {
        case AiEngine::State::Generating: stateColour = OvenLookAndFeel::ember(); break;
        case AiEngine::State::Error:      stateColour = OvenLookAndFeel::blood(); break;
        case AiEngine::State::Ready:      stateColour = lime; break;
        default:                          stateColour = OvenLookAndFeel::cream().withAlpha (0.6f); break;
    }

    const float x = area.getX() + 14.0f * sc;
    const float y = area.getY() + 30.0f * sc;
    const float w = area.getWidth() - 28.0f * sc;

    g.setColour (stateColour.withAlpha (0.95f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (8.5f, 12.0f * sc), juce::Font::bold)));
    g.drawText (stateName (state),
                juce::Rectangle<float> (x, y, w, 18.0f * sc),
                juce::Justification::centredLeft, false);

    g.setColour (OvenLookAndFeel::cream().withAlpha (0.55f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.5f, 9.0f * sc), juce::Font::bold)));
    g.drawText (engine.getStatusText(),
                juce::Rectangle<float> (x, y + 18.0f * sc, w, 14.0f * sc),
                juce::Justification::centredLeft, true);

    if (state == AiEngine::State::Generating)
    {
        const float elapsed = (float) engine.getElapsedSeconds();
        const auto bar = juce::Rectangle<float> (x, y + 34.0f * sc, w, 8.0f * sc);

        g.setColour (juce::Colour (0xff2a0a0c));
        g.fillRoundedRectangle (bar, 3.0f * sc);

        g.setColour (OvenLookAndFeel::ember().brighter (0.2f).withAlpha (0.85f));
        g.drawText ("COOKING... " + juce::String (elapsed, 1) + " s",
                    juce::Rectangle<float> (x, y + 44.0f * sc, w, 12.0f * sc),
                    juce::Justification::centredLeft, false);
    }
    else if (state == AiEngine::State::Error)
    {
        g.setColour (OvenLookAndFeel::cream().withAlpha (0.5f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.0f, 8.5f * sc), juce::Font::bold)));
        g.drawText (engine.getLastError(),
                    juce::Rectangle<float> (x, y + 34.0f * sc, w, 30.0f * sc),
                    juce::Justification::topLeft, true);
    }

    // Model path
    g.setColour (OvenLookAndFeel::cream().withAlpha (0.35f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (6.5f, 7.5f * sc), juce::Font::bold)));
    g.drawText (currentModels.isComplete()
                    ? currentModels.dit.getParentDirectory().getFullPathName()
                    : "NO MODEL FOLDER SET - PRESS MODEL DIR",
                juce::Rectangle<float> (x, y + 34.0f * sc, w, 14.0f * sc),
                juce::Justification::centredLeft, true);
}

//==============================================================================
// Working state
//==============================================================================
AiPage::SavedUi AiPage::snapshot() const
{
    SavedUi now;
    now.prompt   = promptEditor  != nullptr ? promptEditor->getText() : juce::String();
    now.lyrics   = lyricsEditor  != nullptr ? lyricsEditor->getText() : juce::String();
    now.language = languageBox   != nullptr ? languageBox->getText()  : juce::String();
    now.mode     = modePrefix;
    now.vocals   = vocalsButton  != nullptr && vocalsButton->getToggleState();
    now.autoLyrics = autoLyrics;
    now.midi       = generateMidi;
    now.duration = durationSlider != nullptr ? durationSlider->getValue() : -1.0;
    now.steps    = stepsSlider    != nullptr ? stepsSlider->getValue()    : -1.0;
    now.guidance = guidanceSlider != nullptr ? guidanceSlider->getValue() : -1.0;
    now.bpm      = bpmSlider      != nullptr ? bpmSlider->getValue()      : -1.0;
    now.seed     = seedSlider     != nullptr ? seedSlider->getValue()     : -2.0;
    now.valid    = true;
    return now;
}

void AiPage::saveState()
{
    const auto now = snapshot();

    // The timer calls this ten times a second; only touch the tree on change,
    // otherwise every tick would look like an edit to the host.
    if (lastSaved.valid && lastSaved.sameAs (now))
        return;

    auto tree = processor.apvts.state;
    tree.setProperty ("ai_prompt",   now.prompt,   nullptr);
    tree.setProperty ("ai_lyrics",   now.lyrics,   nullptr);
    tree.setProperty ("ai_language", now.language, nullptr);
    tree.setProperty ("ai_mode",     now.mode,     nullptr);
    tree.setProperty ("ai_duration", now.duration, nullptr);
    tree.setProperty ("ai_steps",    now.steps,    nullptr);
    tree.setProperty ("ai_guidance", now.guidance, nullptr);
    tree.setProperty ("ai_bpm",      now.bpm,      nullptr);
    tree.setProperty ("ai_seed",     now.seed,     nullptr);
    tree.setProperty ("ai_vocals",   now.vocals,   nullptr);
    tree.setProperty ("ai_auto_lyrics", now.autoLyrics, nullptr);
    tree.setProperty ("ai_midi", now.midi, nullptr);

    // A DAW hands this back with the project. The standalone app has no host,
    // and JUCE does not reliably write its settings on exit, so keep our own
    // copy as well. It is a few hundred bytes and only written on change.
    writeStandaloneState (now);

    lastSaved = now;
}

juce::File AiPage::uiStateFile()
{
    return btpai::getConfigDir().getChildFile ("ui_state.json");
}

void AiPage::writeStandaloneState (const SavedUi& ui) const
{
    auto* obj = new juce::DynamicObject();

    obj->setProperty ("prompt",   ui.prompt);
    obj->setProperty ("lyrics",   ui.lyrics);
    obj->setProperty ("language", ui.language);
    obj->setProperty ("mode",     ui.mode);
    obj->setProperty ("duration", ui.duration);
    obj->setProperty ("steps",    ui.steps);
    obj->setProperty ("guidance", ui.guidance);
    obj->setProperty ("bpm",      ui.bpm);
    obj->setProperty ("seed",     ui.seed);
    obj->setProperty ("vocals",   ui.vocals);
    obj->setProperty ("autoLyrics", ui.autoLyrics);
    obj->setProperty ("midi", ui.midi);

    const auto file = uiStateFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::JSON::toString (juce::var (obj), false));
}

bool AiPage::readStandaloneState (SavedUi& out) const
{
    const auto file = uiStateFile();
    if (! file.existsAsFile())
        return false;

    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
        return false;

    auto number = [obj] (const char* key, double fallback)
    {
        const auto value = obj->getProperty (key);
        return value.isVoid() ? fallback : (double) value;
    };

    out.prompt   = obj->getProperty ("prompt").toString();
    out.lyrics   = obj->getProperty ("lyrics").toString();
    out.language = obj->getProperty ("language").toString();
    out.mode     = obj->getProperty ("mode").toString();
    out.vocals   = (bool) obj->getProperty ("vocals");
    out.autoLyrics = (bool) obj->getProperty ("autoLyrics");
    out.midi = (bool) obj->getProperty ("midi");
    out.duration = number ("duration", -1.0);
    out.steps    = number ("steps",    -1.0);
    out.guidance = number ("guidance", -1.0);
    out.bpm      = number ("bpm",      -1.0);
    out.seed     = number ("seed",     -2.0);
    out.valid    = true;
    return true;
}

void AiPage::applyUi (const SavedUi& ui)
{
    if (promptEditor != nullptr)
        promptEditor->setText (ui.prompt, false);

    if (lyricsEditor != nullptr)
        lyricsEditor->setText (ui.lyrics, false);

    if (languageBox != nullptr && ui.language.isNotEmpty())
    {
        for (int i = 0; i < languageBox->getNumItems(); ++i)
        {
            if (languageBox->getItemText (i) == ui.language)
            {
                languageBox->setSelectedItemIndex (i, juce::dontSendNotification);
                break;
            }
        }
    }

    modePrefix = ui.mode;

    if (vocalsButton != nullptr)
        vocalsButton->setToggleState (ui.vocals, juce::dontSendNotification);

    autoLyrics = ui.autoLyrics;
    if (autoLyricsButton != nullptr)
        autoLyricsButton->setToggleState (autoLyrics, juce::dontSendNotification);

    generateMidi = ui.midi;
    if (midiButton != nullptr)
        midiButton->setToggleState (generateMidi, juce::dontSendNotification);

    auto restoreSlider = [] (juce::Slider* slider, double value)
    {
        if (slider != nullptr && value >= -1.0)
            slider->setValue (value, juce::dontSendNotification);
    };

    restoreSlider (durationSlider, ui.duration);
    restoreSlider (stepsSlider,    ui.steps);
    restoreSlider (guidanceSlider, ui.guidance);
    restoreSlider (bpmSlider,      ui.bpm);
    restoreSlider (seedSlider,     ui.seed);
}

void AiPage::restoreState()
{
    if (promptEditor == nullptr || lyricsEditor == nullptr)
        return;

    // Plugin in a DAW: the project's state is authoritative.
    const auto tree = processor.apvts.state;

    SavedUi ui;

    if (tree.hasProperty ("ai_prompt"))
    {
        ui.prompt   = tree.getProperty ("ai_prompt").toString();
        ui.lyrics   = tree.getProperty ("ai_lyrics").toString();
        ui.language = tree.getProperty ("ai_language").toString();
        ui.mode     = tree.getProperty ("ai_mode").toString();
        ui.vocals   = (bool) tree.getProperty ("ai_vocals");
        ui.autoLyrics = (bool) tree.getProperty ("ai_auto_lyrics");
        ui.midi = (bool) tree.getProperty ("ai_midi");
        ui.duration = (double) tree.getProperty ("ai_duration", -1.0);
        ui.steps    = (double) tree.getProperty ("ai_steps",    -1.0);
        ui.guidance = (double) tree.getProperty ("ai_guidance", -1.0);
        ui.bpm      = (double) tree.getProperty ("ai_bpm",      -1.0);
        ui.seed     = (double) tree.getProperty ("ai_seed",     -2.0);
        ui.valid    = true;
    }
    else if (! readStandaloneState (ui))
    {
        // Nothing stored anywhere: first run. Leave the defaults alone, and
        // leave lastSaved invalid so the next tick writes them out once.
        lastSaved = SavedUi();
        return;
    }

    applyUi (ui);
    lastSaved = snapshot();
}

//==============================================================================
void AiPage::timerCallback()
{
    // Persist any edit made since the last tick, so a close at any moment keeps
    // what the user last saw.
    saveState();

    // Lyrics arriving from the LM: show them, then sing them if this was an
    // AUTO generate. A hand-written box is never overwritten.
    const int seq = processor.getAiLyricsSeq();

    if (seq != lastLyricsSeq)
    {
        lastLyricsSeq = seq;

        const auto draft = processor.getAiLyricsDraft();
        const bool gotLyrics = draft.trim().isNotEmpty();

        if (gotLyrics && lyricsEditor != nullptr)
            lyricsEditor->setText (draft, false);

        if (pendingAudioAfterLyrics)
        {
            pendingAudioAfterLyrics = false;

            if (gotLyrics)
                submitAudio();
            else
                processor.setStatusMessage ("LYRICS CAME BACK EMPTY - CHECK THE 4B LM OR TYPE SOME");
        }

        saveState();
    }

    // MIDI sketches return independently of the audio.
    const int midiTick = processor.getMidiSeq();

    if (midiTick != lastMidiSeq)
    {
        lastMidiSeq = midiTick;

        if (midiWindow == nullptr && generateMidi)
            openMidiWindow();
        else if (midiWindow != nullptr)
            midiWindow->refresh (processor.getMidiResult(), processor.getMidiStatus());
    }

    shimmerTime += 1.0f / 10.0f;

    if (shimmerTime > 1000.0f)
        shimmerTime = 0.0f;

    const bool busy = engine.isBusy();

    // A random take resolves to a concrete seed. Show it, so the run becomes
    // reproducible and the deterministic cache can serve a repeat instantly.
    const auto resolvedSeed = processor.getAiResultSeed();
    if (seedSlider != nullptr && ! busy && resolvedSeed > 0
        && (juce::int64) std::llround (seedSlider->getValue()) < 0)
    {
        seedSlider->setValue ((double) resolvedSeed, juce::dontSendNotification);
    }

    if (generateButton != nullptr)
    {
        generateButton->setEnabled (! busy);
        generateButton->setButtonText (busy ? "COOKING..." : "GENERATE");
    }

    if (cancelButton != nullptr)
        cancelButton->setEnabled (busy);

    if (loraTraining)
    {
        const auto adapter = pendingLoraDir.getChildFile ("adapter_model.safetensors");
        const bool ready = adapter.existsAsFile()
                           && adapter.getSize() > 1024
                           && adapter.getLastModificationTime().toMilliseconds()
                                  >= loraTrainingStartMs - 2000;
        const auto elapsed = juce::Time::getCurrentTime().toMilliseconds()
                             - loraTrainingStartMs;

        if (ready)
        {
            loraTraining = false;

            if (loraTrainButton != nullptr)
                loraTrainButton->setButtonText ("TRAIN LORA");

            refreshLoras();

            for (int i = 0; i < loras.size(); ++i)
            {
                if (loras[i] == pendingLoraDir.getFullPathName())
                {
                    loraBox->setSelectedId (i + 2, juce::dontSendNotification);
                    break;
                }
            }

            applyLora();

            processor.setStatusMessage ("LORA READY: " + pendingLoraName
                                        + "  -  GENERATE TO HEAR IT");
        }
        else if (elapsed > 4.0 * 60.0 * 60.0 * 1000.0)
        {
            loraTraining = false;

            if (loraTrainButton != nullptr)
                loraTrainButton->setButtonText ("TRAIN LORA");

            processor.setStatusMessage ("LORA TRAINING STOPPED OR STILL RUNNING - SEE Training\\runs");
        }
    }

    repaint (enginePanel.toNearestInt());
    repaint (waveformArea.toNearestInt());
    repaint (R (998.0f, 334.0f, 130.0f, 24.0f));
}

//==============================================================================
int AiPage::DatasetModel::getNumRows()
{
    return owner.datasets.size();
}

void AiPage::DatasetModel::paintListBoxItem (int row, juce::Graphics& g, int width, int height,
                                             bool rowIsSelected)
{
    if (row < 0 || row >= owner.datasets.size())
        return;

    const auto& d = owner.datasets.getReference (row);

    if (rowIsSelected)
    {
        g.setColour (OvenLookAndFeel::lime().withAlpha (0.14f));
        g.fillAll();
    }

    g.setColour (OvenLookAndFeel::lime().withAlpha (rowIsSelected ? 0.95f : 0.7f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (8.0f, (float) height * 0.4f), juce::Font::bold)));
    g.drawText (d.name, 8, 0, juce::jmax (40, width / 2 - 10), height, juce::Justification::centredLeft, true);

    g.setColour (OvenLookAndFeel::cream().withAlpha (0.4f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.0f, (float) height * 0.34f))));
    g.drawText (d.getSummary(), width / 2, 0, width / 2 - 8, height, juce::Justification::centredRight, true);
}

void AiPage::DatasetModel::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= owner.datasets.size())
        return;

    owner.selectedDataset = row;
    owner.repaint();
}
