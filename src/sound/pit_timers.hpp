#pragma once

#include <cstdint>
#include <vector>

namespace ugh {

/// Port of the timer multiplexer in segment 1a32: several routines share the PC timer (PIT, 1193182 Hz),
/// each with its own period in PIT ticks. The interrupt comes at the shortest period; every routine's
/// counter is reduced by the elapsed time and the routines whose counter ran out are called (highest index
/// first). Index 0 is the BIOS clock (period 0x10000).
class PitTimers {
public:
    static constexpr std::uint32_t kFrequency = 1193182;

    /// Routine identifiers: the code offset of the original routine (0 for the BIOS clock).
    using Routine = std::uint16_t;

    /// 1a32:0008: resets to the BIOS clock only.
    void install();

    /// 1a32:00a0: back to the plain BIOS clock; no routine runs any more.
    void uninstall() { timers_.clear(); }

    /// 1a32:01aa: adds a routine; returns its handle.
    int add(std::uint32_t period, Routine routine);

    /// 1a32:021b
    void remove(int handle);

    /// 1a32:02c1: new period for the next intervals (the running counter is kept).
    void setPeriod(int handle, std::uint32_t period);

    /// PIT ticks until the next interrupt.
    [[nodiscard]] std::uint32_t interval() const { return current_; }

    /// 1a32:00d2: one timer interrupt; returns the routines to run, in call order.
    std::vector<Routine> interrupt();

private:
    struct Timer {
        std::uint32_t period;
        std::int32_t counter;
        Routine routine;
        int handle;
        bool fired;
    };

    void recomputeMinimum();
    [[nodiscard]] std::size_t indexOf(int handle) const;

    std::vector<Timer> timers_;
    std::uint32_t current_ = 0x10000; // DS:7b6e, the PIT's current period
    std::uint32_t minimum_ = 0x10000; // DS:7b76, programmed at the end of the next interrupt
    int nextHandle_ = 1;
};

} // namespace ugh
