#pragma once

#include <JuceHeader.h>

#include "Params.h"
#include "LoopEngine.h"
#include "TrapFX.h"
#include "RackFX.h"
#include "FxRecorder.h"
#include "AutoTune.h"
#include "AiEngine.h"
#include "StemEngine.h"
#include "RvcEngine.h"
#include "RaveEngine.h"

//==============================================================================
class BtPOvenAudioProcessor : public juce::AudioProcessor,
                              private juce::Timer
{
public:
    BtPOvenAudioProcessor();
    ~BtPOvenAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "BtP Oven"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    //==========================================================================
    // Loop transport (message thread)
    void beginRecord (int loopIndex, bool overdub);
    void stopRecord (int loopIndex);
    void clearLoop (int loopIndex);
    bool loadFileIntoLoop (int loopIndex, const juce::File& file);
    bool loadBufferIntoLoop (int loopIndex, const juce::AudioBuffer<float>& audio,
                             double sourceSampleRate);
    bool getLoopAudio (int loopIndex, juce::AudioBuffer<float>& out, double& outSampleRate) const;
    bool exportLoop (int loopIndex, const juce::File& file);

    /** Randomises the whole recipe: loops, cook, timing, filters, EQ and rack. */
    void nuke();

    bool   isLoopRecording (int loopIndex) const;
    bool   isLoopOverdubbing (int loopIndex) const;
    bool   hasLoopContent (int loopIndex) const;
    double getLoopLengthSeconds (int loopIndex) const;
    double getMaxLoopLengthSeconds() const noexcept { return btp::LoopBuffer::maxLoopSeconds; }
    float  getWaveformPeak (int loopIndex, int point, int numPoints) const;
    float  getPlayheadPhase (int loopIndex) const;
    float  getOutputLevel() const { return outputLevel.load (std::memory_order_relaxed); }

    //==========================================================================
    // AI take bookkeeping (message thread)
    bool   hasAiResult() const noexcept { return lastAiAudio.getNumSamples() > 0; }
    double getAiResultSeconds() const noexcept { return aiResultSeconds; }
    juce::int64 getAiResultSeed() const noexcept { return aiResultSeed; }
    juce::String getAiResultPrompt() const { return aiResultPrompt; }
    juce::String getAiResultLyrics() const { return aiResultLyrics; }
    const juce::AudioBuffer<float>& getAiResult() const { return lastAiAudio; }
    bool   sendAiResultToLoop (int loopIndex);

    // One-shot generation (message thread)
    bool   hasOneShot() const noexcept { return lastOneShot.getNumSamples() > 0; }
    double getOneShotSeconds() const noexcept { return oneShotSeconds; }
    juce::String getOneShotPrompt() const { return oneShotPrompt; }
    const juce::AudioBuffer<float>& getOneShot() const { return lastOneShot; }
    bool sendOneShotToLoop (int loopIndex);

    //==========================================================================
    // Stem separation bookkeeping (message thread)
    bool   hasStemResult() const noexcept { return lastStems[0].getNumSamples() > 0; }
    double getStemSeconds() const noexcept { return stemSeconds; }
    const juce::AudioBuffer<float>& getStemBuffer (int stem) const
    {
        return lastStems[(size_t) juce::jlimit (0, 3, stem)];
    }
    void startSeparation (const juce::AudioBuffer<float>& audio, double sourceSampleRate);
    bool sendStemToLoop (int stemIndex, int loopIndex);

    //==========================================================================
    // Voice conversion (RVC) bookkeeping (message thread)
    bool   hasVoiceResult() const noexcept { return lastRvcAudio.getNumSamples() > 0; }
    double getVoiceSeconds() const noexcept { return rvcSeconds; }
    const juce::AudioBuffer<float>& getVoiceResult() const { return lastRvcAudio; }
    void startVoiceConversion (const juce::AudioBuffer<float>& audio, double sourceSampleRate);
    bool sendVoiceToLoop (int loopIndex);

    //==========================================================================
    // RAVE variant generation (message thread)
    bool   hasRaveResult() const noexcept { return lastRaveAudio.getNumSamples() > 0; }
    double getRaveSeconds() const noexcept { return raveSeconds; }
    const juce::AudioBuffer<float>& getRaveResult() const { return lastRaveAudio; }
    void mutateLastOneShot (float variation);
    bool sendRaveToLoop (int loopIndex);

    //==========================================================================
    // FX recorder / "happy accident engine"
    btp::FxRecorder& getFxRecorder() noexcept { return fxRecorder; }
    bool  isFxRecording() const noexcept { return fxRecorder.isRecording(); }
    void  startFxRecording();
    void  stopFxRecording();
    void  clearFx();
    float getFxProgress() const { return fxProgress.load (std::memory_order_relaxed); }
    float getFxPhase() const { return automationPhase.load (std::memory_order_relaxed); }

    //==========================================================================
    const juce::String& getStatusMessage() const noexcept { return statusMessage; }
    double getStatusTime() const noexcept { return statusTime; }
    void   setStatusMessage (const juce::String& text);

private:
    //==========================================================================
    void timerCallback() override;

    void   refreshRawPointers();
    btp::Values readValues() const;
    void   applyAutomation (btp::Values& v, double phase) const;
    double activeLoopLengthSeconds() const;
    void   resetVoices();
    void   pollAiEngine();
    void   pollStemEngine();
    void   pollRvcEngine();
    void   pollRaveEngine();

    //==========================================================================
    std::array<btp::LoopBuffer, 3> loops;
    std::array<btp::LoopVoice, 3>  voices;
    btp::VoiceFX     voiceFX;
    btp::RackFX      rackFX;
    btp::AutoTune    autoTune;
    btp::FxRecorder  fxRecorder;

    std::array<std::atomic<float>*, btp::numParams> rawParams {};

    juce::AudioBuffer<float> dryBuffer;

    // Last AI take, kept so it can be re-sent to any loop.
    juce::AudioBuffer<float> lastAiAudio;
    double       aiResultSeconds = 0.0;
    juce::int64  aiResultSeed = 0;
    juce::String aiResultPrompt;
    juce::String aiResultLyrics;
    bool         aiTakeLoaded = false;

    // Last one-shot take (previewed/saved, not auto-routed to a loop).
    juce::AudioBuffer<float> lastOneShot;
    double oneShotSeconds = 0.0;
    juce::String oneShotPrompt;

    // Last stem separation (drums, bass, other, vocals), kept for routing.
    std::array<juce::AudioBuffer<float>, 4> lastStems;
    double stemSeconds = 0.0;

    // Last voice-converted take, kept for routing.
    juce::AudioBuffer<float> lastRvcAudio;
    double rvcSeconds = 0.0;

    // Last RAVE variant, kept for routing.
    juce::AudioBuffer<float> lastRaveAudio;
    double raveSeconds = 0.0;

    juce::SmoothedValue<float> inGainSm, outGainSm, loopGainA, loopGainB, loopGainC;

    juce::AudioFormatManager formatManager;

    std::atomic<float> outputLevel { 0.0f };
    std::atomic<float> fxProgress { 0.0f };
    std::atomic<float> automationPhase { 0.0f };

    double currentSampleRate = 48000.0;
    double autoPhase = 0.0;
    double fxRecordStartTime = -1.0;
    bool   loopsPrepared = false;

    juce::String statusMessage;
    double statusTime = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BtPOvenAudioProcessor)
};
