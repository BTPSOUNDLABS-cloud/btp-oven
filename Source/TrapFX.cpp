#include "TrapFX.h"

namespace btp
{

namespace
{
    constexpr float kTwoPiF = juce::MathConstants<float>::twoPi;
}

void VoiceFX::prepare (double sampleRate)
{
    reset();

    gateCoeff = 1.0f - std::exp (-1.0f / (float) (0.0015 * juce::jmax (8000.0, sampleRate)));

    reverb.setSampleRate (juce::jmax (8000.0, sampleRate));

    juce::Reverb::Parameters rp;
    rp.roomSize  = 0.72f;
    rp.damping   = 0.42f;
    rp.wetLevel  = 0.34f;
    rp.dryLevel  = 0.85f;
    rp.width     = 1.0f;
    rp.freezeMode = 0.0f;
    reverb.setParameters (rp);

    combBuffer.setSize (2, juce::jmax (2048, (int) (sampleRate * 1.05)), false, true, false);
    combBuffer.clear();
}

void VoiceFX::reset()
{
    filterL.reset();
    filterR.reset();
    formantL1.reset();
    formantL2.reset();
    formantR1.reset();
    formantR2.reset();

    for (auto& b : eqL) b.reset();
    for (auto& b : eqR) b.reset();
    eqReady = false;
    lastEqGains.fill (0.0f);

    combBuffer.clear();
    combWrite = 0;
    combDampL = combDampR = 0.0f;

    reverb.reset();

    smoothedCutoff = 4000.0f;
    smoothedReso = 0.35f;
    coefficientsReady = false;
    chopPhase = 0.0;
    sweepPhase = 0.0;
    gateGain = 1.0f;
    vinylL = vinylR = 0.0f;
    airL = airR = 0.0f;
}

void VoiceFX::processBlock (juce::AudioBuffer<float>& buffer, const Values& v, double sampleRate)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    float* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    //==========================================================================
    // Filter coefficients (once per block, smoothly followed)
    const auto mode = (FilterMode) juce::jlimit (0, (int) FilterMode::Basement, v.filterType);

    const float sweepMod = v.sweep > 0.0f
                             ? std::pow (2.0f, std::sin ((float) sweepPhase) * 2.0f * v.sweep)
                             : 1.0f;

    const float targetCutoff = juce::jlimit (20.0f, (float) (sampleRate * 0.45), v.cutoff * sweepMod);

    if (! coefficientsReady)
    {
        smoothedCutoff = targetCutoff;
        smoothedReso = v.reso;
        coefficientsReady = true;
    }
    else
    {
        smoothedCutoff += (targetCutoff - smoothedCutoff) * 0.25f;
        smoothedReso += (v.reso - smoothedReso) * 0.25f;
    }

    const float q = 0.55f + smoothedReso * 13.0f;

    const bool combMode = (mode == FilterMode::Comb);

    if (mode != FilterMode::Off && ! combMode)
    {
        filterL.set (smoothedCutoff, q, sampleRate);
        filterR.set (smoothedCutoff, q, sampleRate);
    }

    //==========================================================================
    // Five-band EQ
    const float eqGains[5] = { v.eqLow, v.eqLowMid, v.eqMid, v.eqHighMid, v.eqHigh };
    const bool eqOn = v.eqOn && (std::abs (eqGains[0]) + std::abs (eqGains[1]) + std::abs (eqGains[2])
                                 + std::abs (eqGains[3]) + std::abs (eqGains[4]) > 0.01f);
    bool eqChanged = ! eqReady;

    for (int i = 0; i < 5; ++i)
        if (std::abs (eqGains[i] - lastEqGains[(size_t) i]) > 0.01f)
            eqChanged = true;

    if (eqOn && eqChanged)
    {
        for (int i = 0; i < 5; ++i)
            lastEqGains[(size_t) i] = eqGains[i];

        eqL[0].setLowShelf  (sampleRate, 90.0f,   eqGains[0]);
        eqL[1].setPeaking   (sampleRate, 250.0f,  0.9f, eqGains[1]);
        eqL[2].setPeaking   (sampleRate, 1000.0f, 1.0f, eqGains[2]);
        eqL[3].setPeaking   (sampleRate, 3500.0f, 0.9f, eqGains[3]);
        eqL[4].setHighShelf (sampleRate, 12000.0f, eqGains[4]);

        for (int i = 0; i < 5; ++i)
        {
            eqR[i] = eqL[i];
            eqL[i].reset();
            eqR[i].reset();
        }

        eqReady = true;
    }
    else if (! eqOn)
    {
        eqReady = false;
    }

    //==========================================================================
    // Comb filter state
    const int combSize = combBuffer.getNumSamples();
    float* combL = combSize > 0 ? combBuffer.getWritePointer (0) : nullptr;
    float* combR = combSize > 0 && combBuffer.getNumChannels() > 1 ? combBuffer.getWritePointer (1) : nullptr;

    const int combDelay = combSize > 4
                            ? juce::jlimit (1, combSize - 2,
                                            (int) (sampleRate / (double) juce::jlimit (40.0f, 4000.0f, smoothedCutoff)))
                            : 1;
    const float combFeedback = juce::jlimit (0.05f, 0.94f, 0.15f + smoothedReso * 0.84f);

    const bool formantActive = std::abs (v.formant) > 0.05f;
    const float formantAmount = juce::jlimit (0.0f, 0.9f, std::abs (v.formant) / 10.0f);

    if (formantActive)
    {
        const float ratio = std::pow (2.0f, v.formant / 12.0f);
        formantL1.set (700.0f * ratio, 4.5f, sampleRate);
        formantL2.set (1150.0f * ratio, 6.5f, sampleRate);
        formantR1.set (700.0f * ratio, 4.5f, sampleRate);
        formantR2.set (1150.0f * ratio, 6.5f, sampleRate);
    }

    const float chopDepth = juce::jlimit (0.0f, 1.0f, v.chopDepth);
    const float chopInc = (v.chopRate * (v.chopTriplet ? 1.5f : 1.0f)) / (float) sampleRate;

    const float vinyl = juce::jlimit (0.0f, 1.0f, v.vinyl);
    const float air = juce::jlimit (0.0f, 1.0f, v.air);

    //==========================================================================
    for (int i = 0; i < numSamples; ++i)
    {
        float l = left[i];
        float r = right != nullptr ? right[i] : l;

        if (combMode && combL != nullptr)
        {
            const int readIndex = (combWrite - combDelay + combSize) % combSize;

            const float dL = combL[readIndex];
            const float dR = combR != nullptr ? combR[readIndex] : dL;

            combDampL += 0.35f * (dL - combDampL);
            combDampR += 0.35f * (dR - combDampR);

            const float yL = std::tanh (l + combDampL * combFeedback);
            const float yR = std::tanh (r + combDampR * combFeedback);

            combL[combWrite] = yL;

            if (combR != nullptr)
                combR[combWrite] = yR;

            l = yL * 0.78f;
            r = yR * 0.78f;

            if (++combWrite >= combSize)
                combWrite = 0;
        }
        else if (mode != FilterMode::Off)
        {
            float lp, bp, hp;
            filterL.process (l, lp, bp, hp);

            switch (mode)
            {
                case FilterMode::Lowpass:  l = lp; break;
                case FilterMode::Highpass: l = hp; break;
                case FilterMode::Bandpass: l = bp * 1.4f; break;
                case FilterMode::Notch:    l = hp + lp; break;
                case FilterMode::Peak:     l += bp * (1.0f + smoothedReso * 3.5f) * 0.8f; break;
                case FilterMode::Smoke:
                    l = std::tanh (lp * 2.4f) * 0.82f + lp * 0.18f;
                    break;
                case FilterMode::Basement:
                    l = bp * 0.9f + lp * 0.3f;
                    break;
                default: break;
            }

            filterR.process (r, lp, bp, hp);

            switch (mode)
            {
                case FilterMode::Lowpass:  r = lp; break;
                case FilterMode::Highpass: r = hp; break;
                case FilterMode::Bandpass: r = bp * 1.4f; break;
                case FilterMode::Notch:    r = hp + lp; break;
                case FilterMode::Peak:     r += bp * (1.0f + smoothedReso * 3.5f) * 0.8f; break;
                case FilterMode::Smoke:
                    r = std::tanh (lp * 2.4f) * 0.82f + lp * 0.18f;
                    break;
                case FilterMode::Basement:
                    r = bp * 0.9f + lp * 0.3f;
                    break;
                default: break;
            }
        }

        if (eqOn)
        {
            for (auto& b : eqL) l = b.process (l);
            for (auto& b : eqR) r = b.process (r);
        }

        if (formantActive)
        {
            float lp, bp, hp;
            float fl = 0.0f, fr = 0.0f;

            formantL1.process (l, lp, bp, hp); fl += bp * 0.85f;
            formantL2.process (l, lp, bp, hp); fl += bp * 0.55f;

            formantR1.process (r, lp, bp, hp); fr += bp * 0.85f;
            formantR2.process (r, lp, bp, hp); fr += bp * 0.55f;

            l += fl * formantAmount;
            r += fr * formantAmount;
        }

        // Texture: vinyl noise/crackle + air hiss
        if (vinyl > 0.0001f || air > 0.0001f)
        {
            const float noiseL = rng.nextFloat() * 2.0f - 1.0f;
            const float noiseR = rng.nextFloat() * 2.0f - 1.0f;

            vinylL += 0.14f * (noiseL - vinylL);
            vinylR += 0.14f * (noiseR - vinylR);

            float crackleL = 0.0f, crackleR = 0.0f;

            if (rng.nextFloat() < 0.0016f * vinyl)
                crackleL = (rng.nextFloat() * 2.0f - 1.0f) * 0.45f * vinyl;

            if (rng.nextFloat() < 0.0016f * vinyl)
                crackleR = (rng.nextFloat() * 2.0f - 1.0f) * 0.45f * vinyl;

            airL += 0.5f * (noiseL - airL);
            airR += 0.5f * (noiseR - airR);

            l += (vinylL * 0.11f + crackleL) * vinyl + (noiseL - airL) * 0.05f * air;
            r += (vinylR * 0.11f + crackleR) * vinyl + (noiseR - airR) * 0.05f * air;
        }

        // Trap gate chop
        float targetGate = 1.0f;

        if (chopDepth > 0.001f)
        {
            chopPhase += chopInc;

            if (chopPhase >= 1.0)
                chopPhase -= 1.0;

            const double onFraction = 1.0 - (double) chopDepth * 0.88;
            targetGate = (chopPhase < onFraction) ? 1.0f : 0.0f;
        }

        gateGain += (targetGate - gateGain) * gateCoeff;

        l = (std::isfinite (l) ? l : 0.0f) * gateGain;
        r = (std::isfinite (r) ? r : 0.0f) * gateGain;

        left[i] = l;

        if (right != nullptr)
            right[i] = r;
    }

    sweepPhase += kTwoPiF * v.sweepRate * (float) numSamples / (float) sampleRate;

    if (sweepPhase > kTwoPiF)
        sweepPhase -= kTwoPiF;

    //==========================================================================
    if (mode == FilterMode::Basement)
    {
        if (right != nullptr)
            reverb.processStereo (left, right, numSamples);
        else
            reverb.processMono (left, numSamples);
    }
}

} // namespace btp
