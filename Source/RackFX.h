#pragma once

#include <JuceHeader.h>
#include "Params.h"

namespace btp
{

/**
    The full rack on the second editor page: drive, bitcrush, chorus, flanger,
    phaser, ping-pong delay and reverb. All buffers are allocated in prepare().
*/
class RackFX
{
public:
    RackFX() = default;

    void prepare (double sampleRate);
    void reset();

    void processBlock (juce::AudioBuffer<float>& buffer, const Values& values, double sampleRate);

private:
    inline float readDelay (const juce::AudioBuffer<float>& buffer, int channel,
                            int writeIndex, double delaySamples) const noexcept;

    juce::AudioBuffer<float> delayBuffer;
    juce::AudioBuffer<float> modBuffer;
    juce::AudioBuffer<float> dryBuffer;

    int delayWrite = 0;
    int modWrite = 0;

    float delayDampL = 0.0f, delayDampR = 0.0f;
    double delayTimeSmoothed = 0.35;

    double chorusPhase = 0.0;
    double flangerPhase = 0.0;
    double phaserPhase = 0.0;

    float phaserL[4] {};
    float phaserR[4] {};
    float phaserFeedbackL = 0.0f, phaserFeedbackR = 0.0f;
    float flangerFbL = 0.0f, flangerFbR = 0.0f;

    float crushHoldL = 0.0f, crushHoldR = 0.0f;
    int crushCounter = 0;

    float distLpL = 0.0f, distLpR = 0.0f;

    juce::Reverb reverb;
    bool reverbReady = false;

    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackFX)
};

} // namespace btp
