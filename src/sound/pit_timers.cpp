#include "sound/pit_timers.hpp"

#include <algorithm>
#include <ranges>

namespace ugh {

void PitTimers::install()
{
    timers_.clear();
    timers_.push_back({.period = 0x10000, .counter = 0x10000, .routine = 0, .handle = 0, .fired = false});
    current_ = 0x10000;
    minimum_ = 0x10000;
    nextHandle_ = 1;
}

int PitTimers::add(std::uint32_t period, Routine routine)
{
    const int handle = nextHandle_++;
    timers_.push_back({.period = period,
                       .counter = static_cast<std::int32_t>(period),
                       .routine = routine,
                       .handle = handle,
                       .fired = false});
    minimum_ = std::min(minimum_, period);
    return handle;
}

std::size_t PitTimers::indexOf(int handle) const
{
    const auto found = std::ranges::find(timers_, handle, &Timer::handle);
    return static_cast<std::size_t>(found - timers_.begin());
}

void PitTimers::remove(int handle)
{
    // The last timer moves into the freed place. (The original also mis-updates its handle table when that
    // happens; only the last-added timer is ever removed in this game, so that path is never taken.)
    const std::size_t index = indexOf(handle);
    if (index >= timers_.size()) {
        return;
    }
    if (index + 1 != timers_.size()) {
        timers_[index] = timers_.back();
    }
    timers_.pop_back();
    recomputeMinimum();
}

void PitTimers::setPeriod(int handle, std::uint32_t period)
{
    const std::size_t index = indexOf(handle);
    if (index >= timers_.size()) {
        return;
    }
    timers_[index].period = period;
    recomputeMinimum();
}

void PitTimers::recomputeMinimum()
{
    minimum_ = 0x10000;
    for (const Timer& timer : timers_) {
        minimum_ = std::min(minimum_, timer.period);
    }
}

std::vector<PitTimers::Routine> PitTimers::interrupt()
{
    for (Timer& timer : std::views::reverse(timers_)) {
        timer.counter -= static_cast<std::int32_t>(current_);
        if (timer.counter <= 0) {
            timer.fired = true;
            timer.counter += static_cast<std::int32_t>(timer.period);
        }
    }
    current_ = minimum_;
    std::vector<Routine> due;
    for (Timer& timer : std::views::reverse(timers_)) {
        if (timer.fired) {
            timer.fired = false;
            due.push_back(timer.routine);
        }
    }
    return due;
}

} // namespace ugh
