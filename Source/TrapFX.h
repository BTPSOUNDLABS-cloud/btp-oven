#pragma once

#include <JuceHeader.h>
#include "Params.h"

namespace btp
{

/** Topology-preserving state variable filter. */
struct SVF
{
    float ic1 = 0.0f, ic2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    void set (float cutoffHz, float q, double sampleRate) noexcept
    {
        const float nyquistLimit = (float) (sampleRate * 0.45);
        cutoffHz = juce::jlimit (20.0f, nyquistLimit, cutoffHz);
        q = juce::jmax (0.4f, q);

        const float g = std::tan (juce::MathConstants<float>::pi * cutoffHz / (float) sampleRate);
        k  = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void process (float x, float& lp, float& bp, float& hp) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;

        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;

        lp = v2;
        bp = v1;
        hp = x - k * v1 - v2;
    }
};

//==============================================================================
/** A small RBJ biquad used by the five-band EQ. */
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }

    inline float process (float x) noexcept
    {
        const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }

    void setPeaking (double sampleRate, float frequency, float q, float gainDb) noexcept
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float w0 = juce::MathConstants<float>::twoPi * juce::jlimit (20.0f, (float) sampleRate * 0.45f, frequency)
                       / (float) sampleRate;
        const float alpha = std::sin (w0) / (2.0f * juce::jmax (0.1f, q));
        const float cosw = std::cos (w0);
        const float a0 = 1.0f + alpha / A;

        b0 = (1.0f + alpha * A) / a0;
        b1 = (-2.0f * cosw) / a0;
        b2 = (1.0f - alpha * A) / a0;
        a1 = (-2.0f * cosw) / a0;
        a2 = (1.0f - alpha / A) / a0;
    }

    void setLowShelf (double sampleRate, float frequency, float gainDb) noexcept
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float w0 = juce::MathConstants<float>::twoPi * juce::jlimit (20.0f, (float) sampleRate * 0.45f, frequency)
                       / (float) sampleRate;
        const float cosw = std::cos (w0);
        const float sinw = std::sin (w0);
        const float beta = std::sqrt (A) * sinw / std::sqrt (2.0f);
        const float a0 = (A + 1.0f) + (A - 1.0f) * cosw + 2.0f * beta;

        b0 = (A * ((A + 1.0f) - (A - 1.0f) * cosw + 2.0f * beta)) / a0;
        b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw)) / a0;
        b2 = (A * ((A + 1.0f) - (A - 1.0f) * cosw - 2.0f * beta)) / a0;
        a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cosw)) / a0;
        a2 = ((A + 1.0f) + (A - 1.0f) * cosw - 2.0f * beta) / a0;
    }

    void setHighShelf (double sampleRate, float frequency, float gainDb) noexcept
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float w0 = juce::MathConstants<float>::twoPi * juce::jlimit (20.0f, (float) sampleRate * 0.45f, frequency)
                       / (float) sampleRate;
        const float cosw = std::cos (w0);
        const float sinw = std::sin (w0);
        const float beta = std::sqrt (A) * sinw / std::sqrt (2.0f);
        const float a0 = (A + 1.0f) - (A - 1.0f) * cosw + 2.0f * beta;

        b0 = (A * ((A + 1.0f) + (A - 1.0f) * cosw + 2.0f * beta)) / a0;
        b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw)) / a0;
        b2 = (A * ((A + 1.0f) + (A - 1.0f) * cosw - 2.0f * beta)) / a0;
        a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cosw)) / a0;
        a2 = ((A + 1.0f) - (A - 1.0f) * cosw - 2.0f * beta) / a0;
    }
};

//==============================================================================
/** The shared "oven" FX chain: dark filters, 5-band EQ, formant, texture, gate and reverb. */
class VoiceFX
{
public:
    VoiceFX() = default;

    void prepare (double sampleRate);
    void reset();

    void processBlock (juce::AudioBuffer<float>& buffer, const Values& values, double sampleRate);

private:
    SVF filterL, filterR;
    SVF formantL1, formantL2, formantR1, formantR2;

    Biquad eqL[5], eqR[5];
    std::array<float, 5> lastEqGains { { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } };
    bool eqReady = false;

    juce::AudioBuffer<float> combBuffer;
    int combWrite = 0;
    float combDampL = 0.0f, combDampR = 0.0f;

    juce::Reverb reverb;

    float smoothedCutoff = 4000.0f;
    float smoothedReso = 0.35f;
    bool  coefficientsReady = false;

    double chopPhase = 0.0;
    double sweepPhase = 0.0;

    float gateGain = 1.0f;
    float gateCoeff = 0.01f;

    float vinylL = 0.0f, vinylR = 0.0f;
    float airL = 0.0f, airR = 0.0f;

    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceFX)
};

} // namespace btp
