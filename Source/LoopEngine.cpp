#include "LoopEngine.h"

namespace btp
{

namespace
{
    constexpr double kTwoPi = juce::MathConstants<double>::twoPi;
}

//==============================================================================
// LoopBuffer
//==============================================================================
void LoopBuffer::prepare (double newSampleRate, double maxSeconds)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    maxLength  = juce::jmax (1024, (int) std::ceil (sampleRate * maxSeconds));

    buffer.setSize (2, maxLength, false, true, false);
    buffer.clear();

    data[0] = buffer.getWritePointer (0);
    data[1] = buffer.getWritePointer (1);

    length.store (0);
    writePos.store (0);
    recording.store (false);
    overdub.store (false);
}

void LoopBuffer::release()
{
    recording.store (false);
    length.store (0);
    writePos.store (0);
    data[0] = data[1] = nullptr;
    buffer.setSize (0, 0);
    maxLength = 0;
}

void LoopBuffer::clear()
{
    recording.store (false);
    length.store (0);
    writePos.store (0);
    overdub.store (false);
    buffer.clear();
}

void LoopBuffer::startRecording (bool overdubOnExisting)
{
    // Stop first so an in-flight writeBlock cannot touch the buffer while we clear it.
    recording.store (false);

    const bool dub = overdubOnExisting && hasContent();

    if (! dub)
    {
        length.store (0);
        buffer.clear();
    }

    writePos.store (0);
    overdub.store (dub);
    recording.store (true);
}

void LoopBuffer::stopRecording()
{
    recording.store (false);

    if (! overdub.load())
    {
        const int written = writePos.load();
        length.store (written > 64 ? written : 0);
    }

    overdub.store (false);
}

void LoopBuffer::writeBlock (const float* left, const float* right, int numSamples) noexcept
{
    if (! recording.load (std::memory_order_acquire) || maxLength <= 0)
        return;

    const bool dub = overdub.load (std::memory_order_relaxed);
    const int existing = length.load (std::memory_order_relaxed);
    const int limit = (dub && existing > 0) ? juce::jmin (existing, maxLength) : maxLength;

    int wp = writePos.load (std::memory_order_relaxed);

    if (wp >= limit)
        wp = 0;

    float* l = data[0];
    float* r = data[1];

    if (l == nullptr || r == nullptr)
        return;

    constexpr float dubGain = 0.85f;

    for (int i = 0; i < numSamples; ++i)
    {
        if (wp >= limit)
        {
            if (dub && existing > 0)
            {
                wp = 0;
            }
            else
            {
                recording.store (false, std::memory_order_release);
                break;
            }
        }

        if (dub && existing > 0)
        {
            l[wp] += left[i] * dubGain;
            r[wp] += right[i] * dubGain;
        }
        else
        {
            l[wp] = left[i];
            r[wp] = right[i];
        }

        ++wp;
    }

    writePos.store (wp, std::memory_order_relaxed);

    if (! dub)
        length.store (wp, std::memory_order_release);
}

float LoopBuffer::getPeak (int pointIndex, int numPoints) const
{
    const int len = getLength();

    if (len <= 1 || numPoints <= 0 || data[0] == nullptr)
        return 0.0f;

    const int start = (int) ((juce::int64) pointIndex * len / numPoints);
    int end = (int) ((juce::int64) (pointIndex + 1) * len / numPoints);

    const int first = juce::jlimit (0, len - 1, start);
    end = juce::jlimit (first + 1, len, end);

    const int stride = juce::jmax (1, (end - first) / 48);
    float peak = 0.0f;

    for (int i = first; i < end; i += stride)
    {
        peak = juce::jmax (peak, std::abs (data[0][i]), std::abs (data[1][i]));
    }

    return peak;
}

bool LoopBuffer::loadFromBuffer (const juce::AudioBuffer<float>& source, double sourceSampleRate)
{
    recording.store (false);
    length.store (0);
    writePos.store (0);
    overdub.store (false);

    const int srcSamples = source.getNumSamples();

    if (srcSamples < 2 || maxLength <= 0 || data[0] == nullptr)
        return false;

    const double ratio = (sourceSampleRate > 0.0 ? sourceSampleRate : sampleRate) / sampleRate;
    const int srcChannels = source.getNumChannels();

    int out = 0;

    for (; out < maxLength; ++out)
    {
        const double srcPos = (double) out * ratio;
        const int i0 = (int) srcPos;

        if (i0 >= srcSamples - 1 || i0 < 0)
            break;

        const float frac = (float) (srcPos - (double) i0);

        for (int ch = 0; ch < 2; ++ch)
        {
            const int srcCh = juce::jmin (ch, srcChannels - 1);
            const float* s = source.getReadPointer (srcCh);
            data[ch][out] = s[i0] + (s[i0 + 1] - s[i0]) * frac;
        }
    }

    if (out < 64)
    {
        buffer.clear();
        return false;
    }

    // Normalise gently if the file is clipping hot
    float peak = 0.0f;
    for (int i = 0; i < out; ++i)
        peak = juce::jmax (peak, std::abs (data[0][i]), std::abs (data[1][i]));

    if (peak > 1.02f)
    {
        const float g = 0.98f / peak;
        for (int i = 0; i < out; ++i)
        {
            data[0][i] *= g;
            data[1][i] *= g;
        }
    }

    buffer.clear (out, maxLength - out);
    writePos.store (out);
    length.store (out);
    return true;
}

bool LoopBuffer::exportToFile (const juce::File& file) const
{
    const int len = getLength();

    if (len <= 0 || data[0] == nullptr)
        return false;

    auto stream = std::make_unique<juce::FileOutputStream> (file);

    if (stream->failedToOpen())
        return false;

    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (sampleRate)
                             .withNumChannels (2)
                             .withBitsPerSample (24);

    std::unique_ptr<juce::OutputStream> outputStream = std::move (stream);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer = wav.createWriterFor (outputStream, options);

    if (writer == nullptr)
        return false;

    juce::AudioBuffer<float> temp (2, len);
    temp.copyFrom (0, 0, data[0], len);
    temp.copyFrom (1, 0, data[1], len);

    return writer->writeFromAudioSampleBuffer (temp, 0, len);
}

//==============================================================================
// LoopVoice
//==============================================================================
void LoopVoice::prepare (double sampleRate)
{
    reset();

    // Pre-roll the LFOs so both voices are not perfectly phase-locked
    wobblePhase = 0.13;
    detunePhase = 0.47;
    dripPhase   = 0.0;

    juce::ignoreUnused (sampleRate);
}

void LoopVoice::reset()
{
    for (auto& g : grains)
        g = Grain();

    pos = 0.0;
    spawnCountdown = 0.0;
    wobblePhase = 0.0;
    detunePhase = 0.0;
    dripPhase = 0.0;
    ghostTimer = 0.0;
    ghostCurrent = 0.0;
    ghostTarget = 0.0;
    stutterActive = false;
    stutterBase = 0.0;
    stutterPhase = 0.0;
    stutterLength = 0.0;
    rearrangePattern.fill (0);
    patternGroup = -1;
    patternSeed = -1;
    lastPhase.store (0.0f);
}

void LoopVoice::spawnGrain (double readPos, double regionStart, double regionLength,
                            const Values& v, double transposeSemitones, float amp,
                            double sampleRate) noexcept
{
    Grain* target = nullptr;

    for (auto& g : grains)
    {
        if (! g.active)
        {
            target = &g;
            break;
        }
    }

    if (target == nullptr)
    {
        // Steal the grain furthest through its life
        float worst = -1.0f;

        for (auto& g : grains)
        {
            const float progress = g.length > 0 ? (float) g.age / (float) g.length : 2.0f;

            if (progress > worst)
            {
                worst = progress;
                target = &g;
            }
        }
    }

    if (target == nullptr)
        return;

    const double randPitch = ((double) rng.nextFloat() * 2.0 - 1.0) * (double) v.grainPitch;
    double semis = transposeSemitones + randPitch;

    if (v.keyLock)
        semis = quantiseToKey (semis, v.keyRoot, v.scaleType);

    const double ratio = std::pow (2.0, semis / 12.0);

    const double sprayOffset = ((double) rng.nextFloat() * 2.0 - 1.0) * (double) v.grainSpray * 0.5 * regionLength;
    const double direction = rng.nextFloat() < v.grainReverse ? -1.0 : 1.0;

    const int grainSamples = juce::jlimit (32,
                                           (int) (sampleRate * 0.75),
                                           (int) (v.grainSizeMs * 0.001 * sampleRate));

    const float pan = v.grainWidth * (rng.nextFloat() * 2.0f - 1.0f);
    const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;

    target->active = true;
    target->pos = wrapInRegion (readPos + sprayOffset, regionStart, regionLength);
    target->step = ratio * direction;
    target->age = 0;
    target->length = grainSamples;
    target->panL = std::cos (angle) * 1.41421356f * 0.7071f;
    target->panR = std::sin (angle) * 1.41421356f * 0.7071f;
    target->amp = amp;
    target->burn = v.grainBurn;
}

double LoopVoice::wrapInRegion (double position, double regionStart, double regionLength) const noexcept
{
    if (regionLength <= 4.0)
        return regionStart;

    double relative = std::fmod (position - regionStart, regionLength);

    if (relative < 0.0)
        relative += regionLength;

    return regionStart + relative;
}

void LoopVoice::updateRearrangePattern (int group, int numSlices, int seed) noexcept
{
    if (group == patternGroup && seed == patternSeed)
        return;

    patternGroup = group;
    patternSeed = seed;

    const int base = group * 64;
    const int n = juce::jlimit (0, 64, numSlices - base);

    if (n <= 0)
    {
        rearrangePattern.fill (0);
        return;
    }

    for (int i = 0; i < 64; ++i)
        rearrangePattern[(size_t) i] = base + (i % n);

    juce::Random shuffleRng ((juce::int64) (seed * 7919 + group * 104729 + 17));

    for (int i = n - 1; i > 0; --i)
    {
        const int j = shuffleRng.nextInt (i + 1);
        std::swap (rearrangePattern[(size_t) i], rearrangePattern[(size_t) j]);
    }
}

void LoopVoice::renderNext (const LoopBuffer& loop,
                            const Values& v,
                            const VoiceOptions& o,
                            double sr,
                            float& outL,
                            float& outR) noexcept
{
    outL = 0.0f;
    outR = 0.0f;

    const int len = loop.getLength();

    if (len <= 1)
    {
        pos = 0.0;
        lastPhase.store (0.0f);
        return;
    }

    //==========================================================================
    // Loop region (start / end / loop)
    double regionStart = (double) len * juce::jlimit (0.0f, 1.0f, o.start);
    double regionEnd   = (double) len * juce::jlimit (0.0f, 1.0f, o.end);

    if (regionEnd - regionStart < 32.0)
    {
        regionStart = 0.0;
        regionEnd = (double) len;
    }

    regionStart = juce::jlimit (0.0, (double) (len - 32), std::floor (regionStart));
    regionEnd   = juce::jlimit (regionStart + 32.0, (double) len, std::ceil (regionEnd));

    const double regionLength = regionEnd - regionStart;

    if (pos < regionStart || pos >= regionEnd)
    {
        if (o.looping)
            pos = wrapInRegion (pos, regionStart, regionLength);
        else
            pos = o.reverse ? regionEnd - 1.0 : regionStart;
    }

    if (! o.looping)
    {
        if ((! o.reverse && pos >= regionEnd) || (o.reverse && pos <= regionStart))
        {
            lastPhase.store ((float) juce::jlimit (0.0, 1.0, (pos - regionStart) / regionLength));
            return;
        }
    }

    //==========================================================================
    // Modulation
    wobblePhase += 0.7 / sr;
    detunePhase += (double) v.detuneRate / sr;
    dripPhase   += (double) v.dripRate / sr;

    if (wobblePhase >= 1.0) wobblePhase -= 1.0;
    if (detunePhase >= 1.0) detunePhase -= 1.0;
    if (dripPhase >= 1.0)   dripPhase -= 1.0;

    const float wobbleSine = std::sin ((float) (wobblePhase * kTwoPi)) * v.wobble * 0.03f
                           + std::sin ((float) (wobblePhase * kTwoPi * 3.7)) * v.wobble * 0.008f;

    const float detuneCents = std::sin ((float) (detunePhase * kTwoPi)) * v.detune;

    // Ghost pitch: random downward dips
    if (ghostTimer > 0.0)
    {
        ghostTimer -= 1.0 / sr;
    }
    else if (v.ghost > 0.0001f && rng.nextFloat() < v.ghost * 0.0008f)
    {
        ghostTimer = 0.06 + (double) rng.nextFloat() * 0.28;
        ghostTarget = -(double) v.ghost * 12.0 * (0.35 + 0.65 * (double) rng.nextFloat());
    }

    const double ghostGoal = ghostTimer > 0.0 ? ghostTarget : 0.0;
    ghostCurrent += (ghostGoal - ghostCurrent) * 0.0025;

    const double dripSemis = -(double) v.drip * dripPhase * 12.0;
    double semitones = (double) v.octave * 12.0
                     + (double) detuneCents / 100.0
                     + ghostCurrent
                     + dripSemis;

    if (v.keyLock)
        semitones = quantiseToKey (semitones, v.keyRoot, v.scaleType);

    double rate = (o.reverse ? -1.0 : 1.0)
                * (o.half ? 0.5 : 1.0)
                * (o.doubleTime ? 2.0 : 1.0)
                * (1.0 + (double) wobbleSine);

    const double stretch = juce::jlimit (0.25, 4.0, (double) v.grainStretch);

    if (v.cookOn)
        rate *= 1.0 / stretch;
    else
        rate *= std::pow (2.0, semitones / 12.0);

    //==========================================================================
    // Slice engine (auto chop + rearrange)
    const int div = juce::jlimit (0, 6, o.chopDiv);
    static constexpr double beatsPerSlice[] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };

    const double beatSamples = sr * 60.0 / (double) juce::jlimit (40.0f, 240.0f, o.bpm);
    const double sliceSamples = juce::jmax (32.0, beatSamples * beatsPerSlice[div]);
    const int numSlices = (int) std::floor (regionLength / sliceSamples);

    //==========================================================================
    // Stutter: loop a tiny slice captured at the moment stutter engages
    double readPos = pos;

    if (o.stutter && v.stutterRate > 0.01f)
    {
        if (! stutterActive)
        {
            stutterActive = true;
            stutterBase = pos;
            stutterPhase = 0.0;
            stutterLength = juce::jmax (16.0, sr / juce::jmax (1.0f, v.stutterRate));
        }

        readPos = wrapInRegion (stutterBase + stutterPhase, regionStart, regionLength);
        stutterPhase += 1.0;

        if (stutterPhase >= stutterLength)
            stutterPhase -= stutterLength;
    }
    else
    {
        stutterActive = false;

        if (o.autoChop && numSlices > 1)
        {
            const double relative = pos - regionStart;
            const int sliceIndex = (int) std::floor (relative / sliceSamples);

            if (sliceIndex >= 0 && sliceIndex < numSlices)
            {
                const double offset = relative - (double) sliceIndex * sliceSamples;
                int targetSlice = sliceIndex;

                if (o.rearrange)
                {
                    updateRearrangePattern (sliceIndex / 64, numSlices, o.shuffleSeed);
                    targetSlice = rearrangePattern[(size_t) (sliceIndex % 64)];
                }
                else if ((sliceIndex & 3) == 3)
                {
                    // Auto-chop: repeat the previous 1/16 - 4 bar slice every fourth slice.
                    targetSlice = juce::jmax (0, sliceIndex - 1);
                }

                readPos = regionStart + (double) targetSlice * sliceSamples + offset;
                readPos = juce::jmin (regionEnd - 1.0, readPos);
            }
        }
    }

    //==========================================================================
    if (v.cookOn)
    {
        const double grainSeconds = juce::jlimit (0.005, 0.75, (double) v.grainSizeMs * 0.001);
        const double overlap = 1.0 + (double) v.grainDensity * 9.0;
        const double grainsPerSecond = overlap / grainSeconds;
        const float amp = (float) (0.85 / std::sqrt (overlap));

        spawnCountdown -= 1.0;

        if (spawnCountdown <= 0.0)
        {
            spawnGrain (readPos, regionStart, regionLength, v, semitones, amp, sr);
            spawnCountdown += juce::jmax (2.0, sr / grainsPerSecond);
        }

        const float twoPiF = juce::MathConstants<float>::twoPi;

        for (auto& g : grains)
        {
            if (! g.active)
                continue;

            float l = loop.readSample (0, g.pos);
            float r = loop.readSample (1, g.pos);

            if (g.burn > 0.001f)
            {
                const float drive = 1.0f + g.burn * 14.0f;
                const float norm = 1.0f / (1.0f + g.burn * 2.5f);
                l = std::tanh (l * drive) * norm;
                r = std::tanh (r * drive) * norm;
            }

            const float progress = g.length > 1 ? (float) g.age / (float) (g.length - 1) : 1.0f;
            const float window = 0.5f - 0.5f * std::cos (twoPiF * progress);
            const float gain = window * g.amp;

            outL += l * gain * g.panL;
            outR += r * gain * g.panR;

            g.pos = wrapInRegion (g.pos + g.step, regionStart, regionLength);
            ++g.age;

            if (g.age >= g.length)
                g.active = false;
        }
    }
    else
    {
        const double edgePos = readPos - regionStart;
        const double fadeLen = juce::jmin (48.0, regionLength * 0.05);
        float edge = 1.0f;

        if (edgePos < fadeLen)
            edge = (float) (edgePos / fadeLen);
        else if (edgePos > regionLength - fadeLen)
            edge = (float) ((regionLength - edgePos) / fadeLen);

        outL = loop.readSample (0, readPos) * edge;
        outR = loop.readSample (1, readPos) * edge;
    }

    pos += rate;

    if (o.looping)
    {
        pos = wrapInRegion (pos, regionStart, regionLength);
    }
    else
    {
        if (pos > regionEnd) pos = regionEnd;
        if (pos < regionStart) pos = regionStart;
    }

    lastPhase.store ((float) juce::jlimit (0.0, 1.0, (pos - regionStart) / regionLength));
}

} // namespace btp
