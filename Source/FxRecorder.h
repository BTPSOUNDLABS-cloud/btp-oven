#pragma once

#include <JuceHeader.h>
#include "Params.h"

namespace btp
{

/**
    Records parameter gestures over one loop pass and plays them back as an
    automation layer. The audio thread only reads while playback is active;
    capture happens on the message thread while playback is off.
*/
class FxRecorder
{
public:
    static constexpr int maxPoints = 1024;

    FxRecorder() = default;

    void clear();
    void startRecording();
    void stopRecording();
    bool isRecording() const noexcept { return recording.load (std::memory_order_acquire); }

    void capture (int lane, float value, double phase01) noexcept;

    /** Returns a NaN if the lane holds no automation. */
    float lookup (int lane, double phase01) const noexcept;

    bool  hasData() const noexcept;
    int   getPointCount (int lane) const noexcept;
    int   getTotalPoints() const noexcept;
    float getPeak (int lane) const noexcept;

    /** Normalised sample (0..1) of a lane for the GUI strip. Returns -1 when empty. */
    float getNormalised (int lane, int pointIndex, int numPoints,
                         float minValue, float maxValue) const noexcept;

private:
    struct LaneData
    {
        std::atomic<int> count { 0 };
        float phases[maxPoints] {};
        float values[maxPoints] {};
        float lastValue = 1.0e9f;
    };

    std::array<LaneData, numLanes> lanes;
    std::atomic<bool> recording { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxRecorder)
};

} // namespace btp
