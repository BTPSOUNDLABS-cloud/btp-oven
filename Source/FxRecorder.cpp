#include "FxRecorder.h"

namespace btp
{

void FxRecorder::clear()
{
    recording.store (false);

    for (auto& lane : lanes)
    {
        lane.count.store (0);
        lane.lastValue = 1.0e9f;
    }
}

void FxRecorder::startRecording()
{
    for (auto& lane : lanes)
    {
        lane.count.store (0);
        lane.lastValue = 1.0e9f;
    }

    recording.store (true);
}

void FxRecorder::stopRecording()
{
    recording.store (false);
}

void FxRecorder::capture (int lane, float value, double phase01) noexcept
{
    if (! isRecording() || lane < 0 || lane >= numLanes)
        return;

    auto& data = lanes[(size_t) lane];

    if (std::abs (value - data.lastValue) < 0.0004f)
        return;

    const int n = data.count.load (std::memory_order_relaxed);

    if (n >= maxPoints)
        return;

    data.phases[n] = (float) juce::jlimit (0.0, 1.0, phase01);
    data.values[n] = value;
    data.count.store (n + 1, std::memory_order_release);
    data.lastValue = value;
}

float FxRecorder::lookup (int lane, double phase01) const noexcept
{
    if (lane < 0 || lane >= numLanes)
        return std::numeric_limits<float>::quiet_NaN();

    const auto& data = lanes[(size_t) lane];
    const int n = data.count.load (std::memory_order_acquire);

    if (n < 2)
        return std::numeric_limits<float>::quiet_NaN();

    const float p = (float) juce::jlimit (0.0, 1.0, phase01);

    // Binary search for the segment containing p
    int lo = 0, hi = n - 1;

    while (lo + 1 < hi)
    {
        const int mid = (lo + hi) / 2;

        if (data.phases[mid] <= p)
            lo = mid;
        else
            hi = mid;
    }

    const float p0 = data.phases[lo];
    const float p1 = data.phases[hi];
    const float v0 = data.values[lo];
    const float v1 = data.values[hi];

    if (p1 <= p0)
        return v1;

    const float t = juce::jlimit (0.0f, 1.0f, (p - p0) / (p1 - p0));
    return v0 + (v1 - v0) * t;
}

bool FxRecorder::hasData() const noexcept
{
    for (const auto& lane : lanes)
        if (lane.count.load (std::memory_order_acquire) > 1)
            return true;

    return false;
}

int FxRecorder::getPointCount (int lane) const noexcept
{
    if (lane < 0 || lane >= numLanes)
        return 0;

    return lanes[(size_t) lane].count.load (std::memory_order_acquire);
}

int FxRecorder::getTotalPoints() const noexcept
{
    int total = 0;

    for (const auto& lane : lanes)
        total += lane.count.load (std::memory_order_acquire);

    return total;
}

float FxRecorder::getPeak (int lane) const noexcept
{
    if (lane < 0 || lane >= numLanes)
        return 0.0f;

    const auto& data = lanes[(size_t) lane];
    const int n = data.count.load (std::memory_order_acquire);
    float peak = 0.0f;

    for (int i = 0; i < n; ++i)
        peak = juce::jmax (peak, std::abs (data.values[i]));

    return peak;
}

float FxRecorder::getNormalised (int lane, int pointIndex, int numPoints,
                                 float minValue, float maxValue) const noexcept
{
    if (lane < 0 || lane >= numLanes || numPoints <= 0 || maxValue <= minValue)
        return -1.0f;

    const auto& data = lanes[(size_t) lane];

    if (data.count.load (std::memory_order_acquire) < 2)
        return -1.0f;

    const double phase = (double) pointIndex / (double) numPoints;
    const float v = lookup (lane, phase);

    if (std::isnan (v))
        return -1.0f;

    return juce::jlimit (0.0f, 1.0f, (v - minValue) / (maxValue - minValue));
}

} // namespace btp
