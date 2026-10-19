// Rising water (113b:2d1c), the animated water surface (113b:2db9), rain (113b:3c04..3d4c) and the random
// number generator (113b:4f09), which only the rain uses.

#include "game/game.hpp"
#include "game/vars.hpp"

#include <algorithm>

namespace ugh {
namespace {

constexpr int kRowBytes = 96;
constexpr std::uint8_t kRainColor = 0x5B;
constexpr int kSurfaceFrames = 6;
constexpr int kRainWarmUpFrames = 0x241;

std::int16_t word(int value)
{
    return static_cast<std::int16_t>(value);
}

} // namespace

std::uint16_t Game::random(std::uint16_t range)
{
    // A chain of additions with carry over four words of state; the result scales the state into the range.
    std::array<std::uint16_t, 4>& state = randomState_;
    unsigned sum = unsigned{range} + state[0];
    const unsigned low = sum & 0xFFFFU;
    for (std::size_t i = 1; i < state.size(); ++i) {
        const unsigned previous = i == 1 ? low : state.at(i - 1);
        sum = state.at(i) + previous + (sum >> 16U);
        state.at(i) = static_cast<std::uint16_t>(sum);
    }
    state[0] = static_cast<std::uint16_t>(low + state[3] + (sum >> 16U));
    return static_cast<std::uint16_t>((unsigned{state[0]} * range) >> 16U);
}

void Game::updateWaterLevel()
{
    // 113b:2d1c: on the frame after a row went under water, the other page gets the tinted row too.
    if (data_.get(var::kWaterRowPending) != 0) {
        data_.set(var::kWaterRowPending, 0);
        video_.restoreRect(0, data_.get(var::kWaterlineDrawn), 320, 1);
        return;
    }
    data_.set(var::kWaterlineDrawn, data_.get(var::kWaterline));

    // The water moves every second frame.
    int water = data_.get(var::kWater);
    const int toggle = data_.get(var::kWaterToggle) ^ 1;
    data_.set(var::kWaterToggle, toggle);
    if (toggle == 0) {
        water = std::max<int>(word(water + data_.get(var::kWaterSpeed)), 0);
    }
    data_.set(var::kWater, water);
    setWaterline(water >> 5);
    if (data_.get(var::kWaterline) == data_.get(var::kWaterlineDrawn)) {
        return;
    }
    video_.tintBackgroundRow(data_.get(var::kWaterlineDrawn));
    data_.set(var::kWaterRowPending, data_.get(var::kWaterRowPending) - 1);
    video_.restoreRect(0, data_.get(var::kWaterlineDrawn), 320, 1);
}

void Game::drawWaterSurfaceFrame()
{
    // 113b:2db9: three phases, each shown for seven frames.
    int phase = data_.get(var::kSurfacePhase);
    const auto timer = static_cast<std::int8_t>(data_.get(var::kSurfaceTimer) - 1);
    data_.set(var::kSurfaceTimer, timer);
    if (timer < 0) {
        data_.set(var::kSurfaceTimer, kSurfaceFrames);
        if (--phase < 0) {
            phase = 2;
        }
        data_.set(var::kSurfacePhase, phase);
    }
    const auto rowOffset = static_cast<std::uint16_t>((data_.get(var::kWaterline) & 0xFF) * kRowBytes);
    data_.set(var::kWaterlineOffset, rowOffset);
    video_.drawWaterSurface(rowOffset, waterSurface_.at(static_cast<std::size_t>(phase) % waterSurface_.size()));
}

void Game::spawnRaindrop(int drop)
{
    // 113b:3c35: anywhere along the top edge, or along the side the wind blows from.
    const auto position = random(static_cast<std::uint16_t>(0x140 + data_.get(var::kWaterline)));
    int x = position;
    int y = 0;
    if (position >= 0x140) {
        y = position - 0x140;
        x = data_.get(var::kRain) == 1 ? 0x13F : 0;
    }
    data_.set(var::kRainOffset, drop, ((y & 0xFF) * kRowBytes) + (x >> 2));
    data_.set(var::kRainPlane, drop, x & 3);
}

void Game::loadRain()
{
    // 113b:3c04: drops start spread over the screen (577 frames simulated in advance).
    for (int drop = 0; drop < var::kRainDrops; ++drop) {
        data_.set(var::kRainOffset, drop, 0);
        data_.set(var::kRainDrawnOffset, drop, 0);
    }
    if (data_.get(var::kRain) == 0) {
        return;
    }
    for (int drop = var::kRainDrops - 1; drop >= 0; --drop) {
        spawnRaindrop(drop);
    }
    for (int frame = 0; frame < kRainWarmUpFrames; ++frame) {
        updateRain();
    }
}

void Game::updateRain()
{
    // 113b:3c78: even drops fall 3 rows and drift 3 pixels per frame, odd ones 2 and 2; below the water they
    // start again.
    const int direction = ((data_.get(var::kRain) - 1) * 2) - 1;
    const auto bottom = static_cast<std::uint16_t>(data_.get(var::kWaterlineOffset));
    for (int drop = data_.get(var::kWaterline) + 1; drop >= 0; --drop) {
        const bool even = (drop & 1) == 0;
        int offset = data_.get(var::kRainOffset, drop);
        data_.set(var::kRainDrawnOffset, drop, offset);
        offset += 2 * kRowBytes;
        int plane = data_.get(var::kRainPlane, drop) + (2 * direction);
        if (even) {
            offset += kRowBytes;
            plane += direction;
        }
        if ((plane & 0xFC) != 0) {
            offset += direction;
        }
        plane &= 3;
        if (static_cast<std::uint16_t>(offset) >= bottom) {
            spawnRaindrop(drop);
        } else {
            data_.set(var::kRainOffset, drop, offset);
            data_.set(var::kRainPlane, drop, plane);
        }
    }
}

void Game::eraseRain()
{
    // 113b:3cde
    for (int drop = data_.get(var::kWaterline) + 1; drop >= 0; --drop) {
        video_.restoreByte(static_cast<std::uint16_t>(data_.get(var::kRainDrawnOffset, drop)));
    }
}

void Game::drawRain()
{
    // 113b:3d19
    for (int drop = data_.get(var::kWaterline); drop >= 0; --drop) {
        video_.plotPixel(static_cast<std::uint16_t>(data_.get(var::kRainOffset, drop)), data_.get(var::kRainPlane, drop),
                         kRainColor);
    }
}

} // namespace ugh
