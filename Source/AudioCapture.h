#pragma once

#include <JuceHeader.h>

/**
    Keeps the last N seconds of whatever the plugin receives.

    Written against a try-lock: if the audio thread cannot get the lock it skips
    this block rather than blocking. A missing block in a capture is acceptable;
    a glitch in the user's vocal is not.
*/
class AudioCapture
{
public:
    void prepare (double sampleRate, int numChannels, double seconds = 8.0)
    {
        const juce::SpinLock::ScopedLockType scoped (lock);
        sampleRateUsed = sampleRate;
        channels = juce::jmax (1, numChannels);
        storage.setSize (channels, (int) std::lround (sampleRate * seconds),
                         false, true, false);
        storage.clear();
        writePos = 0;
        anyAudio.store (false);
    }

    void push (const juce::AudioBuffer<float>& buffer) noexcept
    {
        const juce::SpinLock::ScopedTryLockType scoped (lock);
        if (! scoped.isLocked() || storage.getNumSamples() == 0)
            return;

        const int total = storage.getNumSamples();
        const int n = buffer.getNumSamples();
        const int ch = juce::jmin (channels, buffer.getNumChannels());

        for (int c = 0; c < ch; ++c)
        {
            const auto* src = buffer.getReadPointer (c);
            auto* dst = storage.getWritePointer (c);
            int p = writePos;
            for (int i = 0; i < n; ++i)
            {
                dst[p] = src[i];
                p = (p + 1) % total;
            }
        }

        writePos = (writePos + n) % total;
        anyAudio.store (true);
    }

    bool hasAudio() const noexcept { return anyAudio.load(); }

    /** Normalised peak buckets for drawing. Read-only, cheap, safe to call often. */
    std::vector<float> peaks (int buckets) const
    {
        std::vector<float> out;
        const juce::SpinLock::ScopedTryLockType scoped (lock);
        if (! scoped.isLocked() || storage.getNumSamples() == 0 || buckets <= 0)
            return out;

        const int total = storage.getNumSamples();
        out.resize ((size_t) buckets, 0.0f);
        const int per = juce::jmax (1, total / buckets);
        const auto* left = storage.getReadPointer (0);

        float loudest = 0.0f;
        for (int b = 0; b < buckets; ++b)
        {
            int start = writePos + b * per;
            float peak = 0.0f;
            for (int i = 0; i < per; ++i)
                peak = juce::jmax (peak, std::abs (left[(start + i) % total]));
            out[(size_t) b] = peak;
            loudest = juce::jmax (loudest, peak);
        }
        if (loudest > 0.0f)
            for (auto& value : out) value /= loudest;
        return out;
    }

    /** Write the capture in time order. Returns false if there is nothing to write. */
    bool writeWav (const juce::File& target) const
    {
        const juce::SpinLock::ScopedLockType scoped (lock);
        if (! anyAudio.load() || storage.getNumSamples() == 0)
            return false;

        const int total = storage.getNumSamples();
        juce::AudioBuffer<float> ordered (channels, total);

        for (int c = 0; c < channels; ++c)
        {
            const auto* src = storage.getReadPointer (c);
            auto* dst = ordered.getWritePointer (c);
            int written = 0;
            for (int i = writePos; i < total; ++i)
                dst[written++] = src[i];
            for (int i = 0; i < writePos; ++i)
                dst[written++] = src[i];
        }

        target.deleteFile();
        std::unique_ptr<juce::FileOutputStream> stream (target.createOutputStream());
        if (stream == nullptr)
            return false;

        juce::WavAudioFormat format;
        auto* raw = stream.get();
        std::unique_ptr<juce::AudioFormatWriter> writer (
            format.createWriterFor (raw, sampleRateUsed, (unsigned int) channels,
                                    24, {}, 0));
        if (writer == nullptr)
        {
            // JUCE deletes the stream when it refuses to create a writer.
            stream.release();
            return false;
        }

        stream.release(); // the writer owns it now
        return writer->writeFromAudioSampleBuffer (ordered, 0, total);
    }

    double getSampleRate() const noexcept { return sampleRateUsed; }

private:
    mutable juce::SpinLock lock;
    juce::AudioBuffer<float> storage;
    int writePos = 0;
    int channels = 2;
    double sampleRateUsed = 48000.0;
    std::atomic<bool> anyAudio { false };
};
