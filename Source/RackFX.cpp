#include "RackFX.h"

namespace btp
{

void RackFX::prepare (double sampleRate)
{
    reset();

    delayBuffer.setSize (2, juce::jmax (4096, (int) (sampleRate * 2.05)), false, true, false);
    delayBuffer.clear();

    modBuffer.setSize (2, juce::jmax (1024, (int) (sampleRate * 0.12)), false, true, false);
    modBuffer.clear();

    dryBuffer.setSize (2, 8192, false, true, false);
    dryBuffer.clear();

    reverb.setSampleRate (juce::jmax (8000.0, sampleRate));
    reverbReady = false;
}

void RackFX::reset()
{
    delayBuffer.clear();
    modBuffer.clear();

    delayWrite = 0;
    modWrite = 0;
    delayDampL = delayDampR = 0.0f;
    delayTimeSmoothed = 0.35;

    chorusPhase = 0.0;
    flangerPhase = 0.0;
    phaserPhase = 0.0;

    for (int i = 0; i < 4; ++i)
        phaserL[i] = phaserR[i] = 0.0f;

    phaserFeedbackL = phaserFeedbackR = 0.0f;
    flangerFbL = flangerFbR = 0.0f;

    crushHoldL = crushHoldR = 0.0f;
    crushCounter = 0;

    distLpL = distLpR = 0.0f;

    reverb.reset();
    reverbReady = false;
}

float RackFX::readDelay (const juce::AudioBuffer<float>& buffer, int channel,
                         int writeIndex, double delaySamples) const noexcept
{
    const int size = buffer.getNumSamples();

    if (size < 4)
        return 0.0f;

    double pos = (double) writeIndex - delaySamples;

    while (pos < 0.0)
        pos += (double) size;

    while (pos >= (double) size)
        pos -= (double) size;

    const int i0 = (int) pos;
    const int i1 = (i0 + 1) % size;
    const float frac = (float) (pos - (double) i0);
    const float* d = buffer.getReadPointer (channel);

    return d[i0] + (d[i1] - d[i0]) * frac;
}

void RackFX::processBlock (juce::AudioBuffer<float>& buffer, const Values& v, double sampleRate)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    if (delayBuffer.getNumSamples() < 4 || modBuffer.getNumSamples() < 4)
        return;

    const bool anyFx = v.distOn || v.crushOn || v.chorusOn || v.flangerOn
                    || v.phaserOn || v.delayOn || v.rackVerbOn;

    if (! anyFx)
        return;

    if (dryBuffer.getNumSamples() < numSamples || dryBuffer.getNumChannels() < 2)
        dryBuffer.setSize (2, numSamples, false, false, true);

    float* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    float* dryL = dryBuffer.getWritePointer (0);
    float* dryR = dryBuffer.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        dryL[i] = left[i];
        dryR[i] = right != nullptr ? right[i] : left[i];
    }

    //==========================================================================
    // Distortion
    if (v.distOn)
    {
        const float drive = 1.0f + juce::jlimit (0.0f, 1.0f, v.distDrive) * 40.0f;
        const float norm = 1.0f / juce::jmax (0.001f, std::tanh (drive));
        const float toneCoeff = 0.02f + juce::jlimit (0.0f, 1.0f, v.distTone) * 0.75f;
        const float mix = juce::jlimit (0.0f, 1.0f, v.distMix);

        for (int i = 0; i < numSamples; ++i)
        {
            const float xL = left[i];
            const float xR = right != nullptr ? right[i] : xL;

            const float wL = std::tanh (xL * drive) * norm;
            const float wR = std::tanh (xR * drive) * norm;

            distLpL += toneCoeff * (wL - distLpL);
            distLpR += toneCoeff * (wR - distLpR);

            left[i] = xL + (distLpL - xL) * mix;

            if (right != nullptr)
                right[i] = xR + (distLpR - xR) * mix;
        }
    }

    //==========================================================================
    // Bitcrush
    if (v.crushOn)
    {
        const float bits = juce::jlimit (1.0f, 16.0f, v.crushBits);
        const float levels = std::pow (2.0f, bits) - 1.0f;
        const float step = 1.0f / juce::jmax (1.0f, levels);
        const int hold = juce::jlimit (1, 32, (int) (18.0f - bits));
        const float mix = juce::jlimit (0.0f, 1.0f, v.crushMix);

        for (int i = 0; i < numSamples; ++i)
        {
            const float xL = left[i];
            const float xR = right != nullptr ? right[i] : xL;

            if (crushCounter <= 0)
            {
                crushHoldL = std::round (xL / step) * step;
                crushHoldR = std::round (xR / step) * step;
                crushCounter = hold;
            }

            --crushCounter;

            left[i] = xL + (crushHoldL - xL) * mix;

            if (right != nullptr)
                right[i] = xR + (crushHoldR - xR) * mix;
        }
    }

    //==========================================================================
    // Chorus + flanger share a short modulated delay line.
    const bool chorusOn = v.chorusOn;
    const bool flangerOn = v.flangerOn;

    if (chorusOn || flangerOn)
    {
        const double chorusInc = juce::jlimit (0.05f, 5.0f, v.chorusRate) / sampleRate;
        const double flangerInc = juce::jlimit (0.05f, 5.0f, v.flangerRate) / sampleRate;

        const float chorusDepth = juce::jlimit (0.0f, 1.0f, v.chorusDepth);
        const float chorusMix = juce::jlimit (0.0f, 1.0f, v.chorusMix);
        const float flangerDepth = juce::jlimit (0.0f, 1.0f, v.flangerDepth);
        const float flangerMix = juce::jlimit (0.0f, 1.0f, v.flangerMix);
        const float flangerFeedback = juce::jlimit (0.0f, 0.9f, v.flangerFeedback);

        const int modSize = modBuffer.getNumSamples();
        float* modL = modBuffer.getWritePointer (0);
        float* modR = modBuffer.getNumChannels() > 1 ? modBuffer.getWritePointer (1) : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            const float xL = left[i];
            const float xR = right != nullptr ? right[i] : xL;

            float outL = xL;
            float outR = xR;
            float flangerWetL = 0.0f, flangerWetR = 0.0f;

            if (chorusOn)
            {
                const double lfoL = std::sin (chorusPhase * juce::MathConstants<double>::twoPi);
                const double lfoR = std::sin ((chorusPhase + 0.25) * juce::MathConstants<double>::twoPi);

                const double baseMs = 16.0;
                const double modMs = 8.0 * (double) chorusDepth;

                const double dL = (baseMs + modMs * (0.5 + 0.5 * lfoL)) * 0.001 * sampleRate;
                const double dR = (baseMs + modMs * (0.5 + 0.5 * lfoR)) * 0.001 * sampleRate;

                const float wetL = readDelay (modBuffer, 0, modWrite, dL);
                const float wetR = readDelay (modBuffer, modBuffer.getNumChannels() > 1 ? 1 : 0, modWrite, dR);

                outL = xL + (wetL - xL) * chorusMix;
                outR = xR + (wetR - xR) * chorusMix;

                chorusPhase += chorusInc;

                if (chorusPhase >= 1.0)
                    chorusPhase -= 1.0;
            }

            if (flangerOn)
            {
                const double lfoL = std::sin (flangerPhase * juce::MathConstants<double>::twoPi);
                const double lfoR = std::sin ((flangerPhase + 0.5) * juce::MathConstants<double>::twoPi);

                const double baseMs = 1.2;
                const double modMs = 5.0 * (double) flangerDepth;

                const double dL = (baseMs + modMs * (0.5 + 0.5 * lfoL)) * 0.001 * sampleRate;
                const double dR = (baseMs + modMs * (0.5 + 0.5 * lfoR)) * 0.001 * sampleRate;

                flangerWetL = readDelay (modBuffer, 0, modWrite, dL);
                flangerWetR = readDelay (modBuffer, modBuffer.getNumChannels() > 1 ? 1 : 0, modWrite, dR);

                outL = outL + (flangerWetL - outL) * flangerMix;
                outR = outR + (flangerWetR - outR) * flangerMix;

                flangerFbL = flangerWetL;
                flangerFbR = flangerWetR;

                flangerPhase += flangerInc;

                if (flangerPhase >= 1.0)
                    flangerPhase -= 1.0;
            }

            left[i] = outL;

            if (right != nullptr)
                right[i] = outR;

            // Write the post-modulation signal into the shared delay line.
            if (modSize > 2)
            {
                modL[modWrite] = juce::jlimit (-4.0f, 4.0f, xL + flangerFbL * flangerFeedback * (flangerOn ? 1.0f : 0.0f));

                if (modR != nullptr)
                    modR[modWrite] = juce::jlimit (-4.0f, 4.0f, xR + flangerFbR * flangerFeedback * (flangerOn ? 1.0f : 0.0f));

                if (++modWrite >= modSize)
                    modWrite = 0;
            }
        }
    }

    //==========================================================================
    // Phaser (four first-order all-pass stages)
    if (v.phaserOn)
    {
        const double inc = juce::jlimit (0.05f, 5.0f, v.phaserRate) / sampleRate;
        const float depth = juce::jlimit (0.0f, 1.0f, v.phaserDepth);
        const float mix = juce::jlimit (0.0f, 1.0f, v.phaserMix);

        for (int i = 0; i < numSamples; ++i)
        {
            const float xL = left[i];
            const float xR = right != nullptr ? right[i] : xL;

            const double lfo = 0.5 + 0.5 * std::sin (phaserPhase * juce::MathConstants<double>::twoPi);
            const float frequency = 250.0f * std::pow (2.0f, depth * 4.0f * (float) lfo);
            const float t = std::tan (juce::MathConstants<float>::pi * frequency / (float) sampleRate);
            const float a = (t - 1.0f) / (t + 1.0f);

            float wL = xL + phaserFeedbackL * 0.22f;
            float wR = xR + phaserFeedbackR * 0.22f;

            for (int s = 0; s < 4; ++s)
            {
                const float yL = -a * wL + phaserL[s];
                phaserL[s] = wL + a * yL;
                wL = yL;

                const float yR = -a * wR + phaserR[s];
                phaserR[s] = wR + a * yR;
                wR = yR;
            }

            phaserFeedbackL = juce::jlimit (-2.0f, 2.0f, wL);
            phaserFeedbackR = juce::jlimit (-2.0f, 2.0f, wR);

            left[i] = xL + (wL - xL) * mix;

            if (right != nullptr)
                right[i] = xR + (wR - xR) * mix;

            phaserPhase += inc;

            if (phaserPhase >= 1.0)
                phaserPhase -= 1.0;
        }
    }

    //==========================================================================
    // Ping-pong (or stereo) delay
    if (v.delayOn)
    {
        const int delaySize = delayBuffer.getNumSamples();
        float* delL = delayBuffer.getWritePointer (0);
        float* delR = delayBuffer.getNumChannels() > 1 ? delayBuffer.getWritePointer (1) : nullptr;

        const double targetTime = juce::jlimit (0.01f, 1.5f, v.delayTime);
        delayTimeSmoothed += (targetTime - delayTimeSmoothed) * 0.18;

        const double delaySamples = juce::jlimit (1.0, (double) delaySize - 2.0,
                                                  delayTimeSmoothed * sampleRate);
        const float feedback = juce::jlimit (0.0f, 0.95f, v.delayFeedback);
        const float mix = juce::jlimit (0.0f, 1.0f, v.delayMix);
        const float toneCoeff = 0.02f + juce::jlimit (0.0f, 1.0f, v.delayTone) * 0.7f;
        const bool pingPong = v.delayPingPong;

        for (int i = 0; i < numSamples; ++i)
        {
            const float xL = left[i];
            const float xR = right != nullptr ? right[i] : xL;

            const float wetL = readDelay (delayBuffer, 0, delayWrite, delaySamples);
            const float wetR = readDelay (delayBuffer, 1, delayWrite, delaySamples);

            delayDampL += toneCoeff * (wetL - delayDampL);
            delayDampR += toneCoeff * (wetR - delayDampR);

            const float fL = pingPong ? delayDampR : delayDampL;
            const float fR = pingPong ? delayDampL : delayDampR;

            left[i] = xL + (wetL - xL) * mix;

            if (right != nullptr)
                right[i] = xR + (wetR - xR) * mix;

            delL[delayWrite] = juce::jlimit (-4.0f, 4.0f, xL + fL * feedback);

            if (delR != nullptr)
                delR[delayWrite] = juce::jlimit (-4.0f, 4.0f, xR + fR * feedback);

            if (++delayWrite >= delaySize)
                delayWrite = 0;
        }
    }

    //==========================================================================
    // Rack reverb
    if (v.rackVerbOn)
    {
        if (! reverbReady)
        {
            reverb.setSampleRate (sampleRate);
            reverbReady = true;
        }

        juce::Reverb::Parameters rp;
        rp.roomSize  = juce::jlimit (0.0f, 1.0f, v.rackVerbSize);
        rp.damping   = 0.45f;
        rp.wetLevel  = juce::jlimit (0.0f, 1.0f, v.rackVerbMix);
        rp.dryLevel  = 1.0f - rp.wetLevel;
        rp.width     = 1.0f;
        rp.freezeMode = 0.0f;
        reverb.setParameters (rp);

        if (right != nullptr)
            reverb.processStereo (left, right, numSamples);
        else
            reverb.processMono (left, numSamples);
    }

    //==========================================================================
    // Routing: series = insert, parallel = send/return with the dry signal.
    const float rackMix = juce::jlimit (0.0f, 1.0f, v.rackMix);
    const bool parallel = v.rackRoute == 1;

    if (parallel)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = juce::jlimit (-4.0f, 4.0f, dryL[i] + left[i] * rackMix);

            if (right != nullptr)
                right[i] = juce::jlimit (-4.0f, 4.0f, dryR[i] + right[i] * rackMix);
        }
    }
    else if (rackMix < 0.999f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = juce::jlimit (-4.0f, 4.0f, dryL[i] * (1.0f - rackMix) + left[i] * rackMix);

            if (right != nullptr)
                right[i] = juce::jlimit (-4.0f, 4.0f, dryR[i] * (1.0f - rackMix) + right[i] * rackMix);
        }
    }

    for (int i = 0; i < numSamples; ++i)
    {
        left[i] = std::isfinite (left[i]) ? left[i] : 0.0f;

        if (right != nullptr)
            right[i] = std::isfinite (right[i]) ? right[i] : 0.0f;
    }
}

} // namespace btp
