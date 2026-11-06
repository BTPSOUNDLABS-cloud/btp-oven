#include "AutoTune.h"

namespace btp
{

namespace
{
    constexpr int kMinLag = 40;    // ~1100 Hz at 44.1 kHz
    constexpr int kMaxLag = 800;   // ~55 Hz at 44.1 kHz

    inline float midiToHz (float midi) noexcept
    {
        return 440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f);
    }
}

//==============================================================================
void AutoTune::prepare (double sr)
{
    sampleRate = sr > 0.0 ? sr : 44100.0;

    analysisSize = 2048;
    analysis.assign ((size_t) analysisSize, 0.0f);
    analysisWrite = 0;
    analysisHop = (int) (sampleRate * 0.02);   // re-detect every ~20 ms

    reset();
}

void AutoTune::reset()
{
    if (! analysis.empty())
        std::fill (analysis.begin(), analysis.end(), 0.0f);

    analysisWrite = 0;
    samplesSinceDetect = 0;
    detectedHz = 0.0f;
    smoothedRatio = 1.0f;

    resL = Resampler();
    resR = Resampler();
}

//==============================================================================
int AutoTune::snapMidiToScale (int midiNote, int root, int scaleType) const noexcept
{
    static constexpr int major[] = { 0, 2, 4, 5, 7, 9, 11 };
    static constexpr int minor[] = { 0, 2, 3, 5, 7, 8, 10 };
    const int* degrees = scaleType == 1 ? minor : major;

    const int pc = ((midiNote % 12) + 12) % 12;

    int best = 0;
    int bestDist = 99;

    for (int i = 0; i < 7; ++i)
    {
        const int target = (root + degrees[i]) % 12;
        int dist = std::abs (pc - target);

        if (dist > 6)
            dist = 12 - dist;

        if (dist < bestDist)
        {
            bestDist = dist;
            best = target;
        }
    }

    int delta = best - pc;

    if (delta > 6)
        delta -= 12;
    else if (delta < -6)
        delta += 12;

    return midiNote + delta;
}

//==============================================================================
float AutoTune::detectPitchHz() const noexcept
{
    if (analysisSize < kMaxLag)
        return 0.0f;

    // Linear view of the circular history, oldest -> newest.
    std::vector<float> lin ((size_t) analysisSize);

    for (int i = 0; i < analysisSize; ++i)
        lin[(size_t) i] = analysis[(size_t) ((analysisWrite + i) % analysisSize)];

    float maxCorr = 0.0f;
    int   bestLag = 0;

    for (int lag = kMinLag; lag < kMaxLag; ++lag)
    {
        double sum = 0.0;
        double e0 = 0.0;
        double e1 = 0.0;

        for (int i = 0; i < analysisSize - lag; ++i)
        {
            const float a = lin[(size_t) i];
            const float b = lin[(size_t) (i + lag)];

            sum += (double) a * (double) b;
            e0  += (double) a * (double) a;
            e1  += (double) b * (double) b;
        }

        const float norm = (float) (sum / std::sqrt (e0 * e1 + 1.0e-12));

        if (norm > maxCorr)
        {
            maxCorr = norm;
            bestLag = lag;
        }
    }

    if (maxCorr < 0.5f)
        return 0.0f;

    return (float) (sampleRate / (double) bestLag);
}

//==============================================================================
void AutoTune::processBlock (juce::AudioBuffer<float>& buffer, const Values& v, double sr)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    if (sampleRate < 1000.0 || std::abs (sampleRate - sr) > 0.5)
        prepare (sr);

    if (! v.autoTuneOn)
    {
        reset();
        return;
    }

    const float snap = juce::jlimit (0.0f, 1.0f, v.autoTuneSnap);

    // --- Mono reference for the pitch tracker ---------------------------------
    const float* ch0 = buffer.getReadPointer (0);
    const float* ch1 = numChannels > 1 ? buffer.getReadPointer (1) : ch0;

    for (int i = 0; i < numSamples; ++i)
    {
        const float mono = (ch0[i] + ch1[i]) * 0.5f;
        analysis[(size_t) analysisWrite] = mono;

        if (++analysisWrite >= analysisSize)
            analysisWrite = 0;

        if (++samplesSinceDetect >= analysisHop)
        {
            samplesSinceDetect = 0;

            const float hz = detectPitchHz();
            detectedHz = detectedHz > 0.0f && hz > 0.0f
                             ? detectedHz * 0.7f + hz * 0.3f
                             : hz;
        }
    }

    // --- Target pitch from key + scale ----------------------------------------
    float ratio = 1.0f;

    if (detectedHz > 40.0f && snap > 0.01f)
    {
        const int root  = juce::jlimit (0, 11, v.keyRoot);
        const int scale = juce::jlimit (0, 1, v.scaleType);

        const float midi    = 69.0f + 12.0f * std::log2 (detectedHz / 440.0f);
        const int   snapped = snapMidiToScale ((int) std::lround (midi), root, scale);

        const float targetHz = midiToHz ((float) snapped);
        const float hardRatio = juce::jlimit (0.5f, 2.0f, targetHz / detectedHz);

        ratio = 1.0f + (hardRatio - 1.0f) * snap;
    }
    else
    {
        ratio = 1.0f;
    }

    // Smooth the ratio. Speed maps to a retune time constant in samples.
    const float speedMs = juce::jlimit (0.5f, 200.0f, v.autoTuneSpeed);
    const double tauSamples = (speedMs * 0.001) * sampleRate;
    const double alpha = 1.0 - std::exp (-1.0 / juce::jmax (1.0, tauSamples));

    // --- Resample + mix --------------------------------------------------------
    const float mix = juce::jlimit (0.0f, 1.0f, v.autoTuneMix);

    float* outL = buffer.getWritePointer (0);
    float* outR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        smoothedRatio += (double) ((ratio - smoothedRatio) * alpha);

        const float xL = outL[i];
        const float xR = outR != nullptr ? outR[i] : xL;

        const float wetL = resL.tick (xL, smoothedRatio);
        const float wetR = resR.tick (xR, smoothedRatio);

        outL[i] = xL + (wetL - xL) * mix;

        if (outR != nullptr)
            outR[i] = xR + (wetR - xR) * mix;
    }
}

} // namespace btp
