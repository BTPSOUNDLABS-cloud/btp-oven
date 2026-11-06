#pragma once

#include <JuceHeader.h>
#include "Params.h"

namespace btp
{

//==============================================================================
/**
    A fixed-size stereo loop buffer that can be recorded into by the audio thread.
    Channels: 2. Memory is allocated once in prepare(), never on the audio thread.
*/
class LoopBuffer
{
public:
    LoopBuffer() = default;

    static constexpr double maxLoopSeconds = 60.0;

    void prepare (double sampleRate, double seconds = maxLoopSeconds);
    void release();

    //==========================================================================
    // Message-thread operations
    void clear();
    void startRecording (bool overdubOnExisting);
    void stopRecording();

    bool loadFromBuffer (const juce::AudioBuffer<float>& source, double sourceSampleRate);
    bool exportToFile (const juce::File& file) const;

    //==========================================================================
    // Audio-thread operations
    void writeBlock (const float* left, const float* right, int numSamples) noexcept;

    /** Linearly interpolated stereo read. Positions are wrapped into the loop. */
    float readSample (int channel, double position) const noexcept
    {
        const int len = length.load (std::memory_order_relaxed);
        if (len <= 1)
            return 0.0f;

        double p = std::fmod (position, (double) len);
        if (p < 0.0)
            p += (double) len;

        const int i0 = (int) p;
        const int i1 = (i0 + 1) % len;
        const float frac = (float) (p - (double) i0);
        const float* d = data[channel];

        return d[i0] + (d[i1] - d[i0]) * frac;
    }

    //==========================================================================
    int    getLength()        const noexcept { return length.load (std::memory_order_acquire); }
    double getSampleRate()    const noexcept { return sampleRate; }
    bool   hasContent()       const noexcept { return getLength() > 32; }
    bool   isRecording()      const noexcept { return recording.load (std::memory_order_acquire); }
    bool   isOverdub()        const noexcept { return overdub.load (std::memory_order_acquire); }
    double getLengthSeconds() const noexcept
    {
        const int len = getLength();
        return len > 0 ? (double) len / sampleRate : 0.0;
    }

    /** Peak amplitude for one of numPoints display buckets (message thread, display only). */
    float getPeak (int pointIndex, int numPoints) const;

    const float* getChannelData (int channel) const noexcept { return data[juce::jlimit (0, 1, channel)]; }

private:
    juce::AudioBuffer<float> buffer;
    float* data[2] { nullptr, nullptr };

    double sampleRate = 44100.0;
    int maxLength = 0;

    std::atomic<int>  length    { 0 };
    std::atomic<int>  writePos  { 0 };
    std::atomic<bool> recording { false };
    std::atomic<bool> overdub   { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopBuffer)
};

//==============================================================================
/**
    Reads a LoopBuffer with tape-style speed changes plus the granular "Cook Mode"
    cloud, stutter, tap wobble, drip and ghost pitch.
*/
class LoopVoice
{
public:
    LoopVoice() = default;

    void prepare (double sampleRate);
    void reset();

    void renderNext (const LoopBuffer& loop,
                     const Values& values,
                     const VoiceOptions& options,
                     double sampleRate,
                     float& outL,
                     float& outR) noexcept;

    float getPhase() const noexcept { return lastPhase.load (std::memory_order_relaxed); }

private:
    struct Grain
    {
        bool   active = false;
        double pos = 0.0;
        double step = 1.0;
        int    age = 0;
        int    length = 1;
        float  panL = 0.707f;
        float  panR = 0.707f;
        float  amp = 1.0f;
        float  burn = 0.0f;
    };

    static constexpr int maxGrains = 32;

    void spawnGrain (double readPos, double regionStart, double regionLength,
                     const Values& v, double transposeSemitones, float amp,
                     double sampleRate) noexcept;

    void updateRearrangePattern (int group, int numSlices, int seed) noexcept;

    double wrapInRegion (double position, double regionStart, double regionLength) const noexcept;

    std::array<Grain, maxGrains> grains;

    std::array<int, 64> rearrangePattern {};
    int patternGroup = -1;
    int patternSeed = -1;

    double pos = 0.0;
    double spawnCountdown = 0.0;
    double wobblePhase = 0.0;
    double detunePhase = 0.0;
    double dripPhase = 0.0;

    double ghostTimer = 0.0;
    double ghostCurrent = 0.0;
    double ghostTarget = 0.0;

    bool   stutterActive = false;
    double stutterBase = 0.0;
    double stutterPhase = 0.0;
    double stutterLength = 0.0;

    std::atomic<float> lastPhase { 0.0f };
    juce::Random rng;    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopVoice)
};

} // namespace btp
