// Enemies (113b:2363..2b7e, collision tests 113b:2196 and 113b:22f1, setup 113b:3b1b): the pterodactyl,
// the dinosaur, the blowing creature and fruit trees. Same scheme as the passengers: the state is the code
// offset of the original routine, the type record in the data segment holds the follow-up routines.

#include "game/game.hpp"
#include "game/vars.hpp"

#include <algorithm>

namespace ugh {
namespace {

constexpr std::uint16_t kTreeType = 0x76E4;
constexpr std::uint16_t kPterodactylType = 0x7630;
constexpr std::uint16_t kBlowerType = 0x76A8;
constexpr std::uint16_t kRockType = 0x78DC;
constexpr std::uint16_t kRockFallingRoutine = 0x1CEE;
constexpr std::uint16_t kReturn = 0x1C0E;
constexpr std::uint16_t kReturn2 = 0x2B7E;
constexpr int kRockHitSprite = 0x222;
constexpr int kAnimationRestart = -2;

std::int16_t word(int value)
{
    return static_cast<std::int16_t>(value);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------
// Helpers

std::uint16_t Game::enemyType(int slot) const
{
    return static_cast<std::uint16_t>(data_.get(var::kEnemyType, slot));
}

EnemyJump Game::jumpEnemy(int slot, int field, int rock) const
{
    return {.handler = static_cast<std::uint16_t>(typeWord(enemyType(slot), field)), .rock = rock};
}

std::int16_t Game::enemyFrame(int slot, std::uint16_t list)
{
    auto frame = static_cast<std::int16_t>(
        data_.word(static_cast<std::uint16_t>(list + data_.get(var::kEnemyAnimationStep, slot))));
    if (frame == -1) {
        data_.set(var::kEnemyAnimationStep, slot, 0);
        frame = static_cast<std::int16_t>(data_.word(list));
    }
    return frame;
}

std::uint16_t Game::enemyDirectionalList(int slot, int field) const
{
    // The direction (0 or 2) selects between two consecutive list pointers in the type record.
    return static_cast<std::uint16_t>(typeWord(enemyType(slot), field + data_.get(var::kEnemyDirection, slot)));
}

void Game::addScore(std::uint16_t points)
{
    const std::uint32_t score =
        ((static_cast<std::uint32_t>(static_cast<std::uint16_t>(data_.get(var::kScoreHigh))) << 16U) |
         static_cast<std::uint16_t>(data_.get(var::kScoreLow))) +
        points;
    data_.set(var::kScoreLow, static_cast<int>(score & 0xFFFFU));
    data_.set(var::kScoreHigh, static_cast<int>(score >> 16U));
}

int Game::rockHittingEnemy(int slot) const
{
    // 113b:2196: a rock falling down (not rising) inside the enemy's box.
    const int enemyX = data_.get(var::kEnemyX, slot);
    const int enemyY = data_.get(var::kEnemyY, slot);
    for (int rock = 0; rock < var::kPassengerSlots && data_.get(var::kPassengerType, rock) != -1; ++rock) {
        if (passengerType(rock) != kRockType ||
            static_cast<std::uint16_t>(data_.get(var::kPassengerHandler, rock)) != kRockFallingRoutine ||
            data_.get(var::kPassengerVelocityY, rock) < 0) {
            continue;
        }
        const int y = data_.get(var::kPassengerY, rock);
        if (word(y + 0x100) < enemyY || word(y - 0x280) > enemyY) {
            continue;
        }
        const int x = data_.get(var::kPassengerX, rock);
        if (word(x + 0x180) < enemyX || word(x - 0x340) > enemyX) {
            continue;
        }
        return rock;
    }
    return -1;
}

int Game::playerTouchingEnemy(int slot) const
{
    // 113b:22f1
    const std::uint16_t type = enemyType(slot);
    const int x = data_.get(var::kEnemyX, slot);
    const int y = data_.get(var::kEnemyY, slot);
    const int lastPlayer = data_.get(var::kPlayerCount) == 2 ? 1 : 0;
    for (int player = 0; player <= lastPlayer; ++player) {
        const int playerX = data_.get(var::kPlayerX, player);
        const int playerY = data_.get(var::kPlayerY, player);
        if (word((typeWord(type, 2) * 32) + y) < playerY) {
            continue;
        }
        if (word(((typeWord(type, 2) - (2 * typeWord(type, 6))) * 32) + y - 0x280) > playerY) {
            continue;
        }
        if (word(((typeWord(type, 0) + typeWord(type, 4)) * 32) + x - 0xA0) < playerX) {
            continue;
        }
        if (word(((typeWord(type, 0) - typeWord(type, 4)) * 32) + x - 0x340) <= playerX) {
            return player;
        }
    }
    return -1;
}

EnemyStep Game::enemyHitByRock(int slot)
{
    // The common "hit by a rock" check: the rock bounces back up and the enemy reacts (type + 0x1c).
    const int rock = rockHittingEnemy(slot);
    if (rock < 0) {
        return std::nullopt;
    }
    data_.set(var::kPassengerVelocityY, rock, -data_.get(var::kPassengerVelocityY, rock));
    data_.set(var::kPassengerSprite, rock, kRockHitSprite);
    return jumpEnemy(slot, 0x1C, rock);
}

int Game::playerLandedOnEnemyPlatform(int slot) const
{
    const int platform = data_.get(var::kEnemyPlatform, slot);
    for (int player = 0; player < data_.get(var::kPlayerCount); ++player) {
        if (data_.get(var::kLandedPlatform, player) == platform) {
            return player;
        }
    }
    return -1;
}

void Game::faceDinosaurTowards(int slot, int player)
{
    // 113b:26f7 / 113b:27b2: turn to the player, speed sign following the direction.
    int speed = data_.get(var::kEnemyVelocityX, slot);
    int direction = 0;
    if (data_.get(var::kEnemyX, slot) < data_.get(var::kPlayerX, player)) {
        direction = 2;
        if (speed < 0) {
            speed = -speed;
        }
    } else if (speed >= 0) {
        speed = -speed;
    }
    data_.set(var::kEnemyDirection, slot, direction);
    data_.set(var::kEnemyVelocityX, slot, speed);
}

// ---------------------------------------------------------------------------------------------------------
// Level setup and the per-frame loops

void Game::loadEnemies()
{
    // 113b:3b1b
    auto source = static_cast<std::uint16_t>(data_.get(var::kEnemyList));
    const auto next = [&] {
        const auto value = static_cast<std::int16_t>(data_.word(source));
        source = static_cast<std::uint16_t>(source + 2);
        return value;
    };
    for (int slot = 0;; ++slot) {
        const auto type = static_cast<std::uint16_t>(next());
        data_.set(var::kEnemyType, slot, type);
        if (type == 0xFFFF || slot >= var::kEnemySlots) {
            return;
        }
        if (type == kTreeType) {
            const std::int16_t platform = next();
            data_.set(var::kEnemyPlatform, slot, platform);
            data_.set(var::kEnemyX, slot, (data_.get(var::kPlatformStopX, platform) - 0x10) * 32);
            data_.set(var::kEnemyY, slot, (data_.get(var::kPlatformY, platform) - 0x18) * 32);
            data_.set(var::kEnemySequence, slot, next());
            data_.set(var::kEnemyHandler, slot, 0x2A87);
        } else if (type == kPterodactylType) {
            static_cast<void>(next());
            const std::int16_t delay = next();
            data_.set(var::kEnemyDelay, slot, delay + (delay >> 1)); // 113b:3d5a, Amiga frames to PC frames
            data_.set(var::kEnemyVelocityX, slot, next());
            data_.set(var::kEnemyDirection, slot, 1);
            data_.set(var::kEnemyHandler, slot, 0x2379);
        } else if (type == kBlowerType) {
            data_.set(var::kEnemyX, slot, next() * 32);
            const auto y = word(next() * 32);
            data_.set(var::kEnemyY, slot, word(y - (y >> 2)) - 0x2C0); // 113b:3d4d, Amiga y to PC y
            static_cast<void>(next());
            static_cast<void>(next());
            data_.set(var::kEnemyHandler, slot, 0x295B);
        } else {
            const std::int16_t platform = next();
            data_.set(var::kEnemyPlatform, slot, platform);
            data_.set(var::kEnemyX, slot, (data_.get(var::kPlatformStopX, platform) - 0x10) * 32);
            data_.set(var::kEnemyY, slot, (data_.get(var::kPlatformY, platform) - 0x16) * 32);
            static_cast<void>(next());
            const std::int16_t speed = next();
            data_.set(var::kEnemyVelocityX, slot, (speed >> 2) - speed); // -(3/4) of the Amiga speed
            data_.set(var::kEnemyDirection, slot, 0);
            data_.set(var::kEnemyHandler, slot, 0x25B1);
        }
    }
}

void Game::updateEnemies()
{
    // 113b:2363
    for (int slot = 0; slot < var::kEnemySlots && data_.get(var::kEnemyType, slot) != -1; ++slot) {
        runEnemy(slot, static_cast<std::uint16_t>(data_.get(var::kEnemyHandler, slot)), -1);
    }
}

void Game::eraseEnemies()
{
    // 113b:0d14
    for (int slot = 0; slot < var::kEnemySlots && data_.get(var::kEnemyType, slot) != -1; ++slot) {
        const int drawn = data_.get(var::kEnemyDrawnSprite, slot);
        data_.set(var::kEnemyDrawnSprite, slot, data_.get(var::kEnemySprite, slot));
        if (drawn != -1) {
            video_.restoreSpriteRect(drawn, data_.get(var::kEnemyDrawnX, slot), data_.get(var::kEnemyDrawnY, slot));
        }
    }
}

void Game::drawEnemies(bool behindCopters)
{
    // 113b:0e3e (trees and blowers, behind the copters) / 113b:0ead (the others, in front).
    for (int slot = 0; slot < var::kEnemySlots && data_.get(var::kEnemyType, slot) != -1; ++slot) {
        const std::uint16_t type = enemyType(slot);
        if ((type == kTreeType || type == kBlowerType) != behindCopters) {
            continue;
        }
        const int sprite = data_.get(var::kEnemySprite, slot);
        if (sprite == -1) {
            continue;
        }
        data_.set(var::kEnemyDrawnX, slot, data_.get(var::kEnemyScreenX, slot));
        data_.set(var::kEnemyDrawnY, slot, data_.get(var::kEnemyScreenY, slot));
        const int x = data_.get(var::kEnemyX, slot) >> 5;
        const int y = data_.get(var::kEnemyY, slot) >> 5;
        data_.set(var::kEnemyScreenX, slot, x);
        data_.set(var::kEnemyScreenY, slot, y);
        if (y < 0xC0) {
            video_.drawSprite(sprite, x, y);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------
// State routines

void Game::runEnemy(int slot, std::uint16_t handler, int rock)
{
    constexpr int kMaxJumps = 64;
    EnemyStep next = EnemyJump{.handler = handler, .rock = rock};
    for (int jumps = 0; next; ++jumps) {
        if (jumps == kMaxJumps) {
            reportUnknownRoutine("enemy (endless chain)", next->handler);
            return;
        }
        next = enemyRoutine(slot, next->handler, next->rock);
    }
}

EnemyStep Game::enemyRoutine(int slot, std::uint16_t handler, int rock)
{
    switch (handler) {
    case 0x2379: return pterodactylStart(slot);
    case 0x239F: return pterodactylWaiting(slot);
    case 0x23B0: return pterodactylCry(slot);
    case 0x23D9: return pterodactylCrying(slot);
    case 0x23EA: return pterodactylLaunch(slot);
    case 0x2493: return pterodactylFlying(slot);
    case 0x252B: return pterodactylHit(slot);
    case 0x255E: return pterodactylFalling(slot);
    case 0x25B1: return dinosaurStartWalking(slot);
    case 0x25C9: return dinosaurWalking(slot);
    case 0x2667: return dinosaurStartRoaring(slot);
    case 0x2681: return dinosaurRoaring(slot);
    case 0x272E: return dinosaurStartCharging(slot);
    case 0x2748: return dinosaurCharging(slot);
    case 0x2830: return dinosaurStartStopping(slot);
    case 0x2844: return dinosaurStopping(slot);
    case 0x288F: return dinosaurStartRecovering(slot);
    case 0x28A3: return dinosaurRecovering(slot);
    case 0x28EE: return dinosaurHit(slot);
    case 0x2914: return dinosaurStunned(slot);
    case 0x295B: return blowerStart(slot);
    case 0x2973: return blowerBlowing(slot);
    case 0x2A53: return blowerHit(slot);
    case 0x2A76: return blowerStunned(slot);
    case 0x2A87: return treeStart(slot);
    case 0x2AB5: return treeIdle(slot);
    case 0x2B0C: return treeHit(slot, rock);
    case 0x2B58: return treeShaking(slot);
    case kReturn:
    case kReturn2:
        return std::nullopt;
    default:
        reportUnknownRoutine("enemy", handler);
        return std::nullopt;
    }
}

EnemyStep Game::pterodactylStart(int slot)
{
    // 113b:2379: invisible until the delay runs out.
    data_.set(var::kEnemyHandler, slot, typeWord(enemyType(slot), 0xA));
    data_.set(var::kEnemyAnimationStep, slot, kAnimationRestart);
    data_.set(var::kEnemyAnimationTimer, slot, 1);
    data_.set(var::kEnemySprite, slot, -1);
    data_.set(var::kEnemyTimer, slot, data_.get(var::kEnemyDelay, slot));
    return std::nullopt;
}

EnemyStep Game::pterodactylWaiting(int slot)
{
    // 113b:239f
    const int timer = word(data_.get(var::kEnemyTimer, slot) - 1);
    data_.set(var::kEnemyTimer, slot, timer);
    return timer == 0 ? EnemyStep{jumpEnemy(slot, 0xC, -1)} : std::nullopt;
}

EnemyStep Game::pterodactylCry(int slot)
{
    // 113b:23b0: its cry warns 70 frames ahead.
    data_.set(var::kEnemyHandler, slot, typeWord(enemyType(slot), 0xE));
    data_.set(var::kEnemyTimer, slot, 0x46);
    static_cast<void>(playSound(0x425B, 0xFF, 1)); // cry
    return std::nullopt;
}

EnemyStep Game::pterodactylCrying(int slot)
{
    // 113b:23d9
    const int timer = word(data_.get(var::kEnemyTimer, slot) - 1);
    data_.set(var::kEnemyTimer, slot, timer);
    return timer == 0 ? EnemyStep{jumpEnemy(slot, 0x10, -1)} : std::nullopt;
}

EnemyStep Game::pterodactylLaunch(int slot)
{
    // 113b:23ea: target the players in turn; fly in from the side away from the target, at its height.
    const std::uint16_t type = enemyType(slot);
    int target = data_.get(var::kEnemyDirection, slot) ^ 1;
    if (target >= data_.get(var::kPlayerCount)) {
        target = 0;
    }
    data_.set(var::kEnemyDirection, slot, target);

    int speed = data_.get(var::kEnemyVelocityX, slot);
    int x = 0;
    int list = 0;
    if (data_.get(var::kPlayerX, target) >= 0x1400) {
        if (speed < 0) {
            speed = -speed;
        }
        x = word(0xFC20);
        list = typeWord(type, 0x24);
    } else {
        if (speed >= 0) {
            speed = -speed;
        }
        x = 0x27E0;
        list = typeWord(type, 0x22);
    }
    data_.set(var::kEnemyVelocityX, slot, speed);
    data_.set(var::kEnemyX, slot, x);
    data_.set(var::kEnemySequence, slot, list);

    // At the target's height, but not below the water and not too far above the screen.
    const int bottom = std::min<int>(word(data_.get(var::kPlayerY, target) + 0x340), data_.get(var::kWater));
    data_.set(var::kEnemyY, slot, std::max<int>(word(bottom - 0x340), -0x80));
    data_.set(var::kEnemyHandler, slot, typeWord(type, 0x12));
    data_.set(var::kEnemySound, slot, playSound(0x4260, 0xFF, 0)); // wings, until stopped
    return std::nullopt;
}

EnemyStep Game::pterodactylFlying(int slot)
{
    // 113b:2493: collisions are only tested on animation steps (every 4th frame), and only the targeted
    // player can be hit.
    const std::int16_t x = word(data_.get(var::kEnemyVelocityX, slot) + data_.get(var::kEnemyX, slot));
    if (x <= -0x400 || x >= 0x2800) {
        sound_.stopSound(data_.get(var::kEnemySound, slot)); // 113b:25a0
        return jumpEnemy(slot, 8, -1);
    }
    data_.set(var::kEnemyX, slot, x);
    const int timer = word(data_.get(var::kEnemyAnimationTimer, slot) - 1);
    data_.set(var::kEnemyAnimationTimer, slot, timer);
    if (timer != 0) {
        return std::nullopt;
    }
    data_.set(var::kEnemyAnimationTimer, slot, 4);
    data_.set(var::kEnemyAnimationStep, slot, data_.get(var::kEnemyAnimationStep, slot) + 2);
    data_.set(var::kEnemySprite, slot,
              enemyFrame(slot, static_cast<std::uint16_t>(data_.get(var::kEnemySequence, slot))));

    if (rockHittingEnemy(slot) >= 0) {
        sound_.stopSound(data_.get(var::kEnemySound, slot)); // 113b:25a0
        return enemyHitByRock(slot);
    }
    const int player = playerTouchingEnemy(slot);
    if (player < 0 || player != data_.get(var::kEnemyDirection, slot) || data_.get(var::kFadeStep) < 0) {
        return std::nullopt;
    }
    data_.set(var::kFadeStep, -2);
    data_.set(var::kFadeLevel, data_.get(var::kFadeLevel) - 2);
    return std::nullopt;
}

EnemyStep Game::pterodactylHit(int slot)
{
    // 113b:252b: points, then it tumbles down.
    const std::uint16_t type = enemyType(slot);
    data_.set(var::kEnemyHandler, slot, typeWord(type, 0x1E));
    addScore(static_cast<std::uint16_t>(typeWord(type, 0x3A)));
    data_.set(var::kEnemyTimer, slot, 0);
    const auto frames = static_cast<std::uint16_t>(typeWord(type, data_.get(var::kEnemyVelocityX, slot) < 0 ? 0x36 : 0x38));
    data_.set(var::kEnemySprite, slot, data_.word(frames));
    return std::nullopt;
}

EnemyStep Game::pterodactylFalling(int slot)
{
    // 113b:255e
    const std::int16_t x = word(data_.get(var::kEnemyVelocityX, slot) + data_.get(var::kEnemyX, slot));
    if (x <= -0x400 || x >= 0x2800) {
        return jumpEnemy(slot, 8, -1);
    }
    data_.set(var::kEnemyX, slot, x);
    int speed = data_.get(var::kEnemyTimer, slot);
    if (speed < 0x28) {
        ++speed;
        data_.set(var::kEnemyTimer, slot, speed);
    }
    const std::int16_t y = word(speed + data_.get(var::kEnemyY, slot));
    if (y >= 0x1800) {
        return jumpEnemy(slot, 8, -1);
    }
    data_.set(var::kEnemyY, slot, y);
    return std::nullopt;
}

EnemyStep Game::dinosaurStartWalking(int slot)
{
    // 113b:25b1
    data_.set(var::kEnemyHandler, slot, typeWord(enemyType(slot), 0xA));
    data_.set(var::kEnemyAnimationStep, slot, kAnimationRestart);
    data_.set(var::kEnemyAnimationTimer, slot, 1);
    return std::nullopt;
}

EnemyStep Game::dinosaurWalking(int slot)
{
    // 113b:25c9: patrol the platform; a copter landing on it makes the dinosaur roar.
    const int timer = word(data_.get(var::kEnemyAnimationTimer, slot) - 1);
    data_.set(var::kEnemyAnimationTimer, slot, timer);
    if (timer == 0) {
        const int platform = data_.get(var::kEnemyPlatform, slot);
        const std::int16_t x = word(data_.get(var::kEnemyVelocityX, slot) + data_.get(var::kEnemyX, slot));
        data_.set(var::kEnemyX, slot, x);
        const int left = x >> 5;
        if (left < data_.get(var::kPlatformLeft, platform) ||
            left + 0x20 >= data_.get(var::kPlatformRight, platform)) {
            data_.set(var::kEnemyVelocityX, slot, -data_.get(var::kEnemyVelocityX, slot));
            data_.set(var::kEnemyDirection, slot, data_.get(var::kEnemyDirection, slot) ^ 2);
        }
        data_.set(var::kEnemyAnimationTimer, slot, 4);
        data_.set(var::kEnemyAnimationStep, slot, data_.get(var::kEnemyAnimationStep, slot) + 2);
        data_.set(var::kEnemySprite, slot, enemyFrame(slot, enemyDirectionalList(slot, 0x22)));
    }
    if (playerLandedOnEnemyPlatform(slot) >= 0) {
        return jumpEnemy(slot, 0xC, -1);
    }
    return enemyHitByRock(slot);
}

EnemyStep Game::dinosaurStartRoaring(int slot)
{
    // 113b:2667
    data_.set(var::kEnemyHandler, slot, typeWord(enemyType(slot), 0xE));
    data_.set(var::kEnemyAnimationStep, slot, kAnimationRestart);
    data_.set(var::kEnemyAnimationTimer, slot, 1);
    data_.set(var::kEnemyTimer, slot, 0x8C);
    return std::nullopt;
}

EnemyStep Game::dinosaurRoaring(int slot)
{
    // 113b:2681: roar at the landed copter for 140 frames, then charge.
    const int timer = word(data_.get(var::kEnemyTimer, slot) - 1);
    data_.set(var::kEnemyTimer, slot, timer);
    if (timer == 0) {
        return jumpEnemy(slot, 0x10, -1);
    }
    const int animation = word(data_.get(var::kEnemyAnimationTimer, slot) - 1);
    data_.set(var::kEnemyAnimationTimer, slot, animation);
    if (animation == 0) {
        data_.set(var::kEnemyAnimationTimer, slot, 5);
        data_.set(var::kEnemyAnimationStep, slot, data_.get(var::kEnemyAnimationStep, slot) + 2);
        data_.set(var::kEnemySprite, slot, enemyFrame(slot, enemyDirectionalList(slot, 0x2E)));
    }
    if (auto hit = enemyHitByRock(slot)) {
        return hit;
    }
    const int player = playerLandedOnEnemyPlatform(slot);
    if (player < 0) {
        return jumpEnemy(slot, 8, -1);
    }
    faceDinosaurTowards(slot, player);
    return std::nullopt;
}

EnemyStep Game::dinosaurStartCharging(int slot)
{
    // 113b:272e
    data_.set(var::kEnemyHandler, slot, typeWord(enemyType(slot), 0x12));
    data_.set(var::kEnemyAnimationStep, slot, kAnimationRestart);
    data_.set(var::kEnemyAnimationTimer, slot, 1);
    data_.set(var::kEnemyTimer, slot, 0);
    return std::nullopt;
}

EnemyStep Game::dinosaurCharging(int slot)
{
    // 113b:2748: accelerate towards the copter; ramming it knocks it off the platform.
    const int animation = word(data_.get(var::kEnemyAnimationTimer, slot) - 1);
    data_.set(var::kEnemyAnimationTimer, slot, animation);
    if (animation == 0) {
        data_.set(var::kEnemyAnimationTimer, slot, 4);
        data_.set(var::kEnemyAnimationStep, slot, data_.get(var::kEnemyAnimationStep, slot) + 2);
        data_.set(var::kEnemySprite, slot, enemyFrame(slot, enemyDirectionalList(slot, 0x26)));
    }
    if (auto hit = enemyHitByRock(slot)) {
        return hit;
    }
    const int landed = playerLandedOnEnemyPlatform(slot);
    if (landed < 0) {
        return jumpEnemy(slot, 0x14, -1);
    }
    faceDinosaurTowards(slot, landed);
    const int acceleration = word(data_.get(var::kEnemyDirection, slot) - 1 + data_.get(var::kEnemyTimer, slot));
    data_.set(var::kEnemyTimer, slot, acceleration);
    data_.set(var::kEnemyX, slot,
              acceleration + data_.get(var::kEnemyVelocityX, slot) + data_.get(var::kEnemyX, slot));

    const int player = playerTouchingEnemy(slot);
    if (player < 0) {
        return std::nullopt;
    }
    data_.set(var::kPlayerY, player, data_.get(var::kPlayerY, player) - 0x20);
    const int push = word(data_.get(var::kEnemyVelocityX, slot) + data_.get(var::kEnemyTimer, slot));
    data_.set(var::kVelocityX, player, push * 32);
    data_.set(var::kVelocityY, player, push * 16);
    data_.set(var::kLandedPlatform, player, -1);
    return jumpEnemy(slot, 0x18, -1);
}

EnemyStep Game::dinosaurStartStopping(int slot)
{
    // 113b:2830
    data_.set(var::kEnemyHandler, slot, typeWord(enemyType(slot), 0x16));
    data_.set(var::kEnemyAnimationStep, slot, kAnimationRestart);
    data_.set(var::kEnemyAnimationTimer, slot, 1);
    return std::nullopt;
}

EnemyStep Game::dinosaurPlayOnce(int slot, int listField)
{
    // 113b:2844 / 113b:28a3: one pass through a directional animation, then back to walking.
    const int animation = word(data_.get(var::kEnemyAnimationTimer, slot) - 1);
    data_.set(var::kEnemyAnimationTimer, slot, animation);
    if (animation == 0) {
        data_.set(var::kEnemyAnimationTimer, slot, 4);
        const int step = data_.get(var::kEnemyAnimationStep, slot) + 2;
        data_.set(var::kEnemyAnimationStep, slot, step);
        const auto frame = static_cast<std::int16_t>(
            data_.word(static_cast<std::uint16_t>(enemyDirectionalList(slot, listField) + step)));
        if (frame == -1) {
            return jumpEnemy(slot, 8, -1);
        }
        data_.set(var::kEnemySprite, slot, frame);
    }
    return enemyHitByRock(slot);
}

EnemyStep Game::dinosaurStopping(int slot)
{
    return dinosaurPlayOnce(slot, 0x2A);
}

EnemyStep Game::dinosaurStartRecovering(int slot)
{
    // 113b:288f
    data_.set(var::kEnemyHandler, slot, typeWord(enemyType(slot), 0x1A));
    data_.set(var::kEnemyAnimationStep, slot, kAnimationRestart);
    data_.set(var::kEnemyAnimationTimer, slot, 1);
    return std::nullopt;
}

EnemyStep Game::dinosaurRecovering(int slot)
{
    return dinosaurPlayOnce(slot, 0x32);
}

EnemyStep Game::dinosaurHit(int slot)
{
    // 113b:28ee: points, then 350 frames stunned.
    const std::uint16_t type = enemyType(slot);
    data_.set(var::kEnemyHandler, slot, typeWord(type, 0x1E));
    addScore(static_cast<std::uint16_t>(typeWord(type, 0x3A)));
    data_.set(var::kEnemyAnimationStep, slot, kAnimationRestart);
    data_.set(var::kEnemyAnimationTimer, slot, 1);
    data_.set(var::kEnemyTimer, slot, 0x15E);
    return std::nullopt;
}

EnemyStep Game::dinosaurStunned(int slot)
{
    // 113b:2914
    const int timer = word(data_.get(var::kEnemyTimer, slot) - 1);
    data_.set(var::kEnemyTimer, slot, timer);
    if (timer == 0) {
        return jumpEnemy(slot, 8, -1);
    }
    const int animation = word(data_.get(var::kEnemyAnimationTimer, slot) - 1);
    data_.set(var::kEnemyAnimationTimer, slot, animation);
    if (animation != 0) {
        return std::nullopt;
    }
    data_.set(var::kEnemyAnimationTimer, slot, 4);
    data_.set(var::kEnemyAnimationStep, slot, data_.get(var::kEnemyAnimationStep, slot) + 2);
    data_.set(var::kEnemySprite, slot, enemyFrame(slot, enemyDirectionalList(slot, 0x36)));
    return std::nullopt;
}

EnemyStep Game::blowerStart(int slot)
{
    // 113b:295b
    data_.set(var::kEnemyHandler, slot, typeWord(enemyType(slot), 0x12));
    data_.set(var::kEnemyAnimationStep, slot, kAnimationRestart);
    data_.set(var::kEnemyAnimationTimer, slot, 1);
    return std::nullopt;
}

EnemyStep Game::blowerBlowing(int slot)
{
    // 113b:2973: copters in front of it are pushed away (or sucked in) depending on the animation step.
    const std::uint16_t type = enemyType(slot);
    const int animation = word(data_.get(var::kEnemyAnimationTimer, slot) - 1);
    data_.set(var::kEnemyAnimationTimer, slot, animation);
    if (animation == 0) {
        data_.set(var::kEnemyAnimationTimer, slot, 0xF);
        const int step = data_.get(var::kEnemyAnimationStep, slot) + 2;
        data_.set(var::kEnemyAnimationStep, slot, step);
        if (step == 6) {
            static_cast<void>(playSound(0x4265, 0xFF, 1)); // blowing
        }
        data_.set(var::kEnemySprite, slot, enemyFrame(slot, static_cast<std::uint16_t>(typeWord(type, 0x26))));
    }

    const int x = data_.get(var::kEnemyX, slot);
    const int y = data_.get(var::kEnemyY, slot);
    const int lastPlayer = data_.get(var::kPlayerCount) == 2 ? 1 : 0;
    for (int player = 0; player <= lastPlayer; ++player) {
        const int playerX = data_.get(var::kPlayerX, player);
        const int playerY = data_.get(var::kPlayerY, player);
        if (word(((typeWord(type, 2) - 9) * 32) + y) < playerY ||
            word(((typeWord(type, 2) - 0x1E) * 32) + y) > playerY ||
            word(((typeWord(type, 0) - 0x10) * 32) + x) < playerX ||
            word(((typeWord(type, 0) - 0x68) * 32) + x - 0x340) > playerX) {
            continue;
        }
        const int push = data_.get(var::kEnemyAnimationStep, slot) < 5 ? 0x29 : -0x29;
        data_.set(var::kVelocityX, player, data_.get(var::kVelocityX, player) + push);
    }

    const int rock = rockHittingEnemy(slot);
    if (rock < 0) {
        return std::nullopt;
    }
    data_.set(var::kPassengerVelocityY, rock, -data_.get(var::kPassengerVelocityY, rock));
    return jumpEnemy(slot, 0x1C, rock);
}

EnemyStep Game::blowerHit(int slot)
{
    // 113b:2a53
    const std::uint16_t type = enemyType(slot);
    data_.set(var::kEnemyHandler, slot, typeWord(type, 0x1E));
    addScore(static_cast<std::uint16_t>(typeWord(type, 0x3A)));
    data_.set(var::kEnemyTimer, slot, 0x15E);
    data_.set(var::kEnemySprite, slot, data_.word(static_cast<std::uint16_t>(typeWord(type, 0x36))));
    return std::nullopt;
}

EnemyStep Game::blowerStunned(int slot)
{
    // 113b:2a76
    const int timer = word(data_.get(var::kEnemyTimer, slot) - 1);
    data_.set(var::kEnemyTimer, slot, timer);
    return timer == 0 ? EnemyStep{jumpEnemy(slot, 0x10, -1)} : std::nullopt;
}

EnemyStep Game::treeStart(int slot)
{
    // 113b:2a87. The original stores position * 32 * 32 as the screen position; the first erase after it is
    // skipped (nothing drawn yet), the next one restores a rectangle at that bogus place.
    data_.set(var::kEnemyHandler, slot, typeWord(enemyType(slot), 0xA));
    data_.set(var::kEnemyAnimationStep, slot, kAnimationRestart);
    data_.set(var::kEnemyAnimationTimer, slot, 1);
    data_.set(var::kEnemyScreenX, slot, data_.get(var::kEnemyX, slot) * 32);
    data_.set(var::kEnemyScreenY, slot, data_.get(var::kEnemyY, slot) * 32);
    return std::nullopt;
}

EnemyStep Game::treeIdle(int slot)
{
    // 113b:2ab5
    const int animation = word(data_.get(var::kEnemyAnimationTimer, slot) - 1);
    data_.set(var::kEnemyAnimationTimer, slot, animation);
    if (animation != 0) {
        return std::nullopt;
    }
    data_.set(var::kEnemyAnimationTimer, slot, 6);
    data_.set(var::kEnemyAnimationStep, slot, data_.get(var::kEnemyAnimationStep, slot) + 2);
    data_.set(var::kEnemySprite, slot,
              enemyFrame(slot, static_cast<std::uint16_t>(typeWord(enemyType(slot), 0x22))));

    const int rock = rockHittingEnemy(slot);
    if (rock < 0) {
        return std::nullopt;
    }
    data_.set(var::kPassengerVelocityY, rock, word(-data_.get(var::kPassengerVelocityY, rock)) >> 1);
    data_.set(var::kPassengerSprite, rock, kRockHitSprite);
    return jumpEnemy(slot, 0x1C, rock);
}

EnemyStep Game::treeHit(int slot, int rock)
{
    // 113b:2b0c: shake and drop the next item of the tree's list where the rock hit.
    const std::uint16_t type = enemyType(slot);
    data_.set(var::kEnemyHandler, slot, typeWord(type, 0x1E));
    data_.set(var::kEnemyTimer, slot, 0xD2);
    data_.set(var::kEnemySprite, slot, 0xE8);
    const auto list = static_cast<std::uint16_t>(data_.get(var::kEnemySequence, slot));
    const auto item = data_.word(list);
    spawnItem(item, data_.get(var::kPassengerX, rock), data_.get(var::kPassengerY, rock),
              data_.get(var::kPassengerCountdown, rock), word(-data_.get(var::kPassengerVelocityY, rock)) >> 2);
    data_.set(var::kEnemySequence, slot, list + 2);
    static_cast<void>(playSound(0x4274, 0xFF, 1)); // item drop
    return std::nullopt;
}

EnemyStep Game::treeShaking(int slot)
{
    // 113b:2b58: after 210 frames the tree is ready again while it still has items.
    const int timer = word(data_.get(var::kEnemyTimer, slot) - 1);
    data_.set(var::kEnemyTimer, slot, timer);
    if (timer != 0) {
        return std::nullopt;
    }
    const auto list = static_cast<std::uint16_t>(data_.get(var::kEnemySequence, slot));
    const int field = data_.word(list) == 0xFFFF ? 0x20 : 0xA;
    data_.set(var::kEnemyHandler, slot, typeWord(enemyType(slot), field));
    return std::nullopt;
}

} // namespace ugh
