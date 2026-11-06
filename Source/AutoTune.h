#pragma once

#include <JuceHeader.h>
#include "Params.h"

namespace btp
{

/**
    Monophonic auto-tune: detects the dominant pitch with autocorrelation and
    snaps it to the plugin's selected key + scale using a crossfaded time-domain
    resampler. All state is allocated in prepare(); the audio thread never allocates.
*/
class AutoTune
{
public:
    AutoTune() = default;

    void prepare (double sampleRate);
    void reset();

    void processBlock (juce::AudioBuffer<float>& buffer, const Values& values, double sampleRate);

private:
    // One channel of the crossfaded resampling pitch shifter.
    struct Resampler
    {
        static constexpr int    RING    = 4096;
        static constexpr double DELAY   = 1024.0;   // ~23 ms at 44.1 kHz
        static constexpr double GUARD   = 256.0;
        static constexpr double MAXGAP  = 1792.0;
        static constexpr double SPLICE  = 512.0;
        static constexpr int    FADE    = 512;

        float  ring[RING] {};
        double writeCount = 0.0;
        double readCount  = -DELAY;
        double read2      = -DELAY;
        int    fade       = 0;

        float readAt (double p) const noexcept
        {
            p = std::fmod (p, (double) RING);
            if (p < 0.0)
                p += (double) RING;

            const int i0 = (int) p;
            const int i1 = (i0 + 1) % RING;
            const float f = (float) (p - (double) i0);

            return ring[i0] * (1.0f - f) + ring[i1] * f;
        }

        float tick (float x, double ratio) noexcept
        {
            ring[(size_t) std::fmod (writeCount, (double) RING)] = x;
            writeCount += 1.0;

            float y = readAt (readCount);
            readCount += ratio;

            if (fade > 0)
            {
                const float y2 = readAt (read2);
                read2 += ratio;

                const float t = 1.0f - (float) fade / (float) FADE;
                y = y * (1.0f - t) + y2 * t;

                if (--fade == 0)
                    readCount = read2;
            }
            else
            {
                const double delay = writeCount - readCount;

                if (ratio > 1.0001 && delay < GUARD)
                {
                    read2 = readCount - SPLICE;
                    fade = FADE;
                }
                else if (ratio < 0.9999 && delay > MAXGAP)
                {
                    read2 = readCount + SPLICE;
                    fade = FADE;
                }
            }

            return y;
        }
    };

    float detectPitchHz() const noexcept;
    int   snapMidiToScale (int midiNote, int root, int scaleType) const noexcept;

    // Pitch detection state
    std::vector<float> analysis;     // circular mono history
    int    analysisWrite = 0;
    int    analysisSize  = 0;
    int    samplesSinceDetect = 0;
    int    analysisHop   = 0;
    float  detectedHz    = 0.0f;
    float  smoothedRatio = 1.0f;

    Resampler resL, resR;

    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoTune)
};

} // namespace btp
