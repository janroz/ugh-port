// Copter control and physics (113b:1095), collision probes (113b:1457), the keyboard interrupt handler
// (113b:4567) and the platform list (113b:3a67).
//
// All arithmetic is 16-bit like the original: positions are pixels * 32, velocities are position units * 64.

#include "game/game.hpp"
#include "game/vars.hpp"

#include <array>

namespace ugh {
namespace {

constexpr int kRowBytes = 96;
constexpr int kMaxVelocity = 0x1800;
constexpr int kMinX = -0x200;
constexpr int kMaxX = 0x2600;
constexpr int kMinY = -0x260;
constexpr int kMaxY = 0x17E0;
constexpr int kPixel = 0x20;
constexpr std::uint16_t kPixelMask = 0xFFE0;

constexpr int kSidewaysThrust = 0x3F;
constexpr int kGravity = 0x1B;
constexpr int kVerticalThrust = 0x46;
constexpr int kPedalSpeed = 0x5A;
constexpr int kSinkDeceleration = 0xC1;
constexpr int kBuoyancy = 0x15;

/// Bytes of the copter outline tested against the background (113b:1457), relative to its top-left probe:
/// rows 0, 6, 12, 19 and columns +0, +12, +20 pixels (all in the same plane).
constexpr std::array<std::uint16_t, 10> kProbeOffsets{
    0x000, 0x003, 0x005, 0x720, 0x723, 0x725, 0x240, 0x245, 0x480, 0x485,
};

std::int16_t word(int value)
{
    return static_cast<std::int16_t>(value);
}

std::int16_t clampVelocity(int value)
{
    const std::int16_t velocity = word(value);
    if (velocity < -kMaxVelocity) {
        return -kMaxVelocity;
    }
    if (velocity > kMaxVelocity) {
        return kMaxVelocity;
    }
    return velocity;
}

/// NEG, SAR 1: reverse and halve.
std::int16_t bounced(int velocity)
{
    return static_cast<std::int16_t>(word(-velocity) >> 1);
}

/// |velocity * 2| as the original computes it (SHL, NEG when negative).
std::int16_t impactOf(int velocity)
{
    const std::int16_t doubled = word(velocity * 2);
    return doubled < 0 ? word(-doubled) : doubled;
}

std::int16_t alignedToPixel(int position)
{
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(position) & kPixelMask);
}

} // namespace

bool Game::collides(std::uint16_t offset, int plane)
{
    std::uint8_t bits = 0;
    for (const std::uint16_t probe : kProbeOffsets) {
        bits |= video_.backgroundByte(static_cast<std::uint16_t>(offset + probe), plane);
    }
    return (bits & 0x80U) != 0;
}

void Game::updatePlayer(int player)
{
    const auto get = [&](WordArray array) { return data_.get(array, player); };
    const auto set = [&](WordArray array, int value) { data_.set(array, player, value); };
    const auto burnFuel = [&](int amount) { data_.set(var::kFuel, data_.get(var::kFuel) - amount); };

    burnFuel(1);
    set(var::kImpact, 0);
    set(var::kRotorSpeed, 0);

    // > 0: the copter's bottom is below the waterline, 0: exactly at it, < 0: above.
    const std::int16_t depth = word(get(var::kPlayerScreenY) - data_.get(var::kWaterline) + 0x12);
    int surface = -1;
    if (depth > 0) {
        surface = 1;
    } else if (depth == 0) {
        surface = 0;
    }

    if (get(var::kLandedPlatform) < 0) {
        const std::uint8_t rain = data_.get(var::kRain);
        if (surface < 0 && rain != 0) {
            // Wind: rain 1 blows left, 2 right; it also pushes the copter down.
            const auto wind = word((((rain - 1) * 2) - 1) * 32);
            set(var::kVelocityX, get(var::kVelocityX) + wind);
            auto push = static_cast<std::int16_t>(wind >> 1);
            if (push < 0) {
                push = word(-push);
            }
            set(var::kVelocityY, get(var::kVelocityY) + push);
        }

        int velocity = get(var::kVelocityX);
        if (get(var::kLeft) != 0) {
            velocity -= kSidewaysThrust;
            set(var::kRotorSpeed, get(var::kRotorSpeed) + kSidewaysThrust);
        } else if (get(var::kRight) != 0) {
            velocity += kSidewaysThrust;
            set(var::kRotorSpeed, get(var::kRotorSpeed) + kSidewaysThrust);
        }
        const std::int16_t velocityX = clampVelocity(velocity);
        set(var::kVelocityX, velocityX);
        std::int16_t target = word((velocityX >> 6) + get(var::kPlayerX));
        if (target < kMinX) {
            target = kMinX;
            set(var::kVelocityX, 0);
        } else if (target > kMaxX) {
            target = kMaxX;
            set(var::kVelocityX, 0);
        }

        // Sweep pixel by pixel against the background. Moving left only the first pixel is tested (the
        // original loops back past the probe advance); moving right every pixel is.
        const int screenX = get(var::kPlayerScreenX);
        auto offset = static_cast<std::uint16_t>(word(get(var::kPlayerScreenY) * kRowBytes) + (word(screenX + 5) >> 2));
        int plane = (screenX + 0x25) & 3;
        std::int16_t position = target;
        if ((target >> 5) != screenX) {
            position = word(get(var::kPlayerX));
            if (target > position) {
                while (true) {
                    if (++plane >= 4) {
                        plane &= 3;
                        ++offset;
                    }
                    if (collides(offset, plane)) {
                        position = alignedToPixel(position);
                        set(var::kVelocityX, bounced(get(var::kVelocityX)));
                        set(var::kImpact, impactOf(get(var::kVelocityX)));
                        break;
                    }
                    position = word(position + kPixel);
                    if (position >= target) {
                        position = target;
                        break;
                    }
                }
            } else {
                if (--plane < 0) {
                    plane &= 3;
                    --offset;
                }
                while (true) {
                    if (collides(offset, plane)) {
                        position = word(alignedToPixel(position) + kPixel);
                        set(var::kVelocityX, bounced(get(var::kVelocityX)));
                        set(var::kImpact, impactOf(get(var::kVelocityX)));
                        break;
                    }
                    position = word(position - kPixel);
                    if (position <= target) {
                        position = target;
                        break;
                    }
                }
            }
        }
        set(var::kPlayerX, position);
        set(var::kPlayerScreenX, position >> 5);
    }

    int velocity = get(var::kVelocityY);
    bool checkUp = false;
    if (surface < 0) {
        checkUp = true;
        if (get(var::kLandedPlatform) < 0) {
            velocity += kGravity;
            burnFuel(2);
            if (get(var::kDown) != 0) {
                velocity += kVerticalThrust;
                set(var::kRotorSpeed, get(var::kRotorSpeed) + kPedalSpeed);
                checkUp = false;
            }
        }
    } else if (surface == 0) {
        checkUp = true;
    } else {
        burnFuel(2);
        if (velocity > 0) {
            velocity -= kSinkDeceleration;
        } else {
            velocity -= kBuoyancy;
            set(var::kLandedPlatform, -1);
        }
    }
    if (checkUp && get(var::kUp) != 0) {
        burnFuel(3);
        velocity -= kVerticalThrust;
        set(var::kRotorSpeed, get(var::kRotorSpeed) + kPedalSpeed);
        set(var::kLandedPlatform, -1);
    }

    const std::int16_t velocityY = clampVelocity(velocity);
    set(var::kVelocityY, velocityY);
    std::int16_t target = word((velocityY >> 6) + get(var::kPlayerY));
    if (target < kMinY) {
        target = kMinY;
        set(var::kVelocityY, 0);
    } else if (target > kMaxY) {
        target = kMaxY;
        set(var::kVelocityY, 0);
    }
    if (surface > 0) {
        // Under water the copter cannot go deeper than its surface position allows; it floats up.
        const std::int16_t below = word((target >> 5) - data_.get(var::kWaterline) + 0x12);
        if (below <= 0) {
            set(var::kVelocityY, 0);
            target = word((data_.get(var::kWaterline) * 32) - 0x240);
        }
    }

    const int screenX = get(var::kPlayerScreenX);
    const int screenY = get(var::kPlayerScreenY);
    auto offset = static_cast<std::uint16_t>(word(screenY * kRowBytes) + (word(screenX + 5) >> 2));
    const int plane = (screenX + 0x25) & 3;
    std::int16_t position = target;
    bool hit = false;
    if ((target >> 5) != screenY) {
        position = word(get(var::kPlayerY));
        if (target > position) {
            while (true) {
                offset = static_cast<std::uint16_t>(offset + kRowBytes);
                if (collides(offset, plane)) {
                    position = alignedToPixel(position);
                    hit = true;
                    break;
                }
                position = word(position + kPixel);
                if (position >= target) {
                    position = target;
                    break;
                }
            }
        } else {
            offset = static_cast<std::uint16_t>(offset - kRowBytes);
            while (true) {
                if (collides(offset, plane)) {
                    position = word(alignedToPixel(position) + kPixel);
                    hit = true;
                    break;
                }
                position = word(position - kPixel);
                if (position <= target) {
                    position = target;
                    break;
                }
            }
        }
    }

    const int crashThreshold = data_.get(var::kCrashThreshold, data_.get(var::kDifficulty));
    if (hit) {
        set(var::kVelocityY, bounced(get(var::kVelocityY)));
        const std::int16_t impact = impactOf(get(var::kVelocityY));
        if (impact > get(var::kImpact)) {
            set(var::kImpact, impact);
        }
        // A gentle touch-down on a platform lands the copter.
        if (get(var::kVelocityY) < 0 && get(var::kImpact) < crashThreshold) {
            const int bottom = (position >> 5) + 0x14;
            const int centre = get(var::kPlayerScreenX) + 0x10;
            for (int platform = 0; data_.get(var::kPlatformLeft, platform) >= 0; ++platform) {
                if (data_.get(var::kPlatformY, platform) == bottom && centre >= data_.get(var::kPlatformLeft, platform) &&
                    centre <= data_.get(var::kPlatformRight, platform)) {
                    set(var::kLandedPlatform, platform);
                    set(var::kVelocityX, 0);
                    set(var::kVelocityY, 0);
                    break;
                }
            }
        }
    }

    set(var::kPlayerY, position);
    set(var::kPlayerScreenY, position >> 5);
    if (get(var::kImpact) >= crashThreshold && data_.get(var::kFadeStep) >= 0) {
        data_.set(var::kFadeStep, -2);
        data_.set(var::kFadeLevel, data_.get(var::kFadeLevel) - 2);
    }
}

void Game::serviceKeyboard()
{
    constexpr std::uint16_t kEntryBytes = 6;
    constexpr std::uint8_t kEnd = 0xFF;
    const auto entry = [](std::uint16_t index, int field) {
        return static_cast<std::uint16_t>(var::kKeyTable + (index * kEntryBytes) + field);
    };
    const auto resetSequences = [&] {
        data_.set(var::kKeySequencePosition, 0);
        for (std::uint16_t i = 0; data_.byte(entry(i, 0)) != kEnd; ++i) {
            data_.setByte(entry(i, 5), 0);
        }
    };

    while (const std::optional<std::uint8_t> received = keyboard_->nextInterruptByte()) {
        const int position = data_.get(var::kKeySequencePosition);
        int stillMatching = 0;
        bool completed = false;
        for (std::uint16_t i = 0; data_.byte(entry(i, 0)) != kEnd; ++i) {
            if (data_.byte(entry(i, 5)) == kEnd) {
                continue;
            }
            if (*received != data_.byte(entry(i, position))) {
                data_.setByte(entry(i, 5), kEnd);
                continue;
            }
            if (data_.byte(entry(i, position + 1)) != kEnd) {
                ++stillMatching;
                continue;
            }
            // Complete sequence: set the control byte unless that player uses a joystick.
            const std::uint8_t target = data_.byte(entry(i, 3));
            if (data_.get(var::kControl, static_cast<int>((target & 2U) >> 1U)) < 0) {
                data_.setByte(static_cast<std::uint16_t>(var::kUp.offset + target), data_.byte(entry(i, 4)));
            }
            completed = true;
            break;
        }
        if (completed || stillMatching == 0) {
            resetSequences();
        } else {
            data_.set(var::kKeySequencePosition, position + 1);
        }
    }
}

void Game::loadPlatforms()
{
    auto source = static_cast<std::uint16_t>(data_.get(var::kPlatformList));
    const auto next = [&] {
        const auto value = static_cast<std::int16_t>(data_.word(source));
        source = static_cast<std::uint16_t>(source + 2);
        return value;
    };
    for (int platform = 0;; ++platform) {
        const std::int16_t left = next();
        data_.set(var::kPlatformLeft, platform, left);
        if (left < 0) {
            return;
        }
        data_.set(var::kPlatformRight, platform, next());
        const std::int16_t y = next();
        data_.set(var::kPlatformY, platform, word(y - (y >> 2)));
        data_.set(var::kPlatform2949, platform, next());
        data_.set(var::kPlatform295d, platform, next());
        data_.set(var::kPlatformStopX, platform, next());
        data_.set(var::kPlatform2985, platform, next());
        static_cast<void>(next());
        data_.set(var::kPlatform2999, platform, -1);
    }
}

} // namespace ugh
