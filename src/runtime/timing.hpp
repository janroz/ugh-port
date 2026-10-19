#pragma once

#include <cstddef>
#include <cstdint>

namespace ugh {

/// Mode 13h refreshes at ~70.086 Hz; the original paces everything by vertical retrace.
inline constexpr std::uint64_t kFrameNs = 14'268'000;

/// Splits a sample rate into whole samples per 70 Hz frame without drifting.
class SampleClock {
public:
    explicit SampleClock(std::uint64_t sampleRate) : sampleRate_(sampleRate) {}

    /// Samples belonging to the next frame.
    [[nodiscard]] std::size_t next()
    {
        constexpr std::uint64_t kNsPerSecond = 1'000'000'000;
        accumulator_ += sampleRate_ * kFrameNs;
        const std::uint64_t samples = accumulator_ / kNsPerSecond;
        accumulator_ %= kNsPerSecond;
        return static_cast<std::size_t>(samples);
    }

private:
    std::uint64_t sampleRate_;
    std::uint64_t accumulator_ = 0;
};

} // namespace ugh
