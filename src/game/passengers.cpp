// Passengers, the carried rock and bonus items (113b:1486..2195, 2196..22f0, 2b7f..2d1b).
//
// Every character runs a state routine whose code offset is stored in the data segment (kPassengerHandler);
// the type records (also in the data segment) hold the follow-up routines, animation lists and sizes.
// The original jumps between routines through those records; runPassenger() maps each code offset to its
// port, so behaviour defined by the level data stays exactly as in the game.

#include "game/game.hpp"
#include "game/vars.hpp"

#include <algorithm>
#include <cstdio>

namespace ugh {
namespace {

constexpr std::uint16_t kReturn = 0x1C0E; // a bare RET used for "nothing to do"
constexpr std::uint16_t kReturn2 = 0x2195;
constexpr int kTypeNext = 0x48;            // type the character turns into (swimming / walking again)
constexpr int kTypeAnimationDelay = 0x3E;
constexpr std::uint16_t kFloatingWomanType = 0x77FE;
constexpr int kAnimationRestart = -2;

std::int16_t word(int value)
{
    return static_cast<std::int16_t>(value);
}

std::int16_t clampSpeed(int value)
{
    const std::int16_t speed = word(value);
    if (speed < -0x1800) {
        return -0x1800;
    }
    if (speed > 0x1800) {
        return 0x1800;
    }
    return speed;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------
// Helpers

std::int16_t Game::typeWord(std::uint16_t type, int field) const
{
    return static_cast<std::int16_t>(data_.word(static_cast<std::uint16_t>(type + field)));
}

std::uint16_t Game::passengerType(int slot) const
{
    return static_cast<std::uint16_t>(data_.get(var::kPassengerType, slot));
}

PassengerJump Game::jumpPassenger(int slot, int field, int player) const
{
    return {.handler = static_cast<std::uint16_t>(typeWord(passengerType(slot), field)), .player = player};
}

void Game::restartPassengerAnimation(int slot)
{
    data_.set(var::kPassengerAnimationStep, slot, kAnimationRestart);
    data_.set(var::kPassengerAnimationTimer, slot, 1);
}

bool Game::passengerAnimationTick(int slot)
{
    const int timer = data_.get(var::kPassengerAnimationTimer, slot) - 1;
    data_.set(var::kPassengerAnimationTimer, slot, timer);
    if (word(timer) != 0) {
        return false;
    }
    data_.set(var::kPassengerAnimationTimer, slot, typeWord(passengerType(slot), kTypeAnimationDelay));
    data_.set(var::kPassengerAnimationStep, slot, data_.get(var::kPassengerAnimationStep, slot) + 2);
    return true;
}

std::int16_t Game::passengerFrame(int slot, int listField)
{
    // A list ends with -1; reaching it restarts the list. (The original retries forever on an empty list.)
    const auto list = static_cast<std::uint16_t>(typeWord(passengerType(slot), listField));
    auto frame = static_cast<std::int16_t>(
        data_.word(static_cast<std::uint16_t>(list + data_.get(var::kPassengerAnimationStep, slot))));
    if (frame == -1) {
        data_.set(var::kPassengerAnimationStep, slot, 0);
        frame = static_cast<std::int16_t>(data_.word(list));
    }
    return frame;
}

int Game::platformWord(WordArray array, int platformOffset) const
{
    return data_.get(array, platformOffset >> 1);
}

int Game::playerTouchingPassenger(int slot) const
{
    // 113b:2276: box test against each player's position (all in position units).
    const std::uint16_t type = passengerType(slot);
    const int x = data_.get(var::kPassengerX, slot);
    const int y = data_.get(var::kPassengerY, slot);
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

PassengerStep Game::passengerFallsInWater(int slot)
{
    // Feet under the waterline: the passenger turns into its swimming type.
    const std::uint16_t type = passengerType(slot);
    const int waterline = data_.get(var::kWaterline);
    if (word((typeWord(type, 2) >> 1) + data_.get(var::kPassengerScreenY, slot) - waterline) < 0) {
        return std::nullopt;
    }
    data_.set(var::kPassengerY, slot, (waterline + typeWord(type, 2)) * 32);
    data_.set(var::kPlatform2999, data_.get(var::kPassengerPlatformOffset, slot) >> 1, -1);
    data_.set(var::kPassengerBubble, slot, -1);
    return changePassengerType(slot);
}

PassengerStep Game::passengerKnockedOff(int slot)
{
    // A flying copter touching a passenger knocks it off the platform.
    const int player = playerTouchingPassenger(slot);
    if (player < 0 || data_.get(var::kLandedPlatform, player) != -1) {
        return std::nullopt;
    }
    return changePassengerType(slot);
}

PassengerStep Game::changePassengerType(int slot)
{
    const auto next = static_cast<std::uint16_t>(typeWord(passengerType(slot), kTypeNext));
    data_.set(var::kPassengerType, slot, next);
    return PassengerJump{.handler = static_cast<std::uint16_t>(typeWord(next, 8)), .player = -1};
}

int Game::emptyCopterOnPlatform(int platform) const
{
    for (int player = 0; player < data_.get(var::kPlayerCount); ++player) {
        if (data_.get(var::kLandedPlatform, player) == platform && data_.get(var::kPlayerIcon, player) == 0) {
            return player;
        }
    }
    return -1;
}

void Game::followWaterline(int slot)
{
    const int waterline = data_.get(var::kWaterline);
    if (waterline != data_.get(var::kPassengerScreenY, slot)) {
        data_.set(var::kPassengerY, slot, (waterline - typeWord(passengerType(slot), 2)) * 32);
    }
}

int Game::copterOnWater() const
{
    // A copter sitting on the water (bottom exactly at the waterline) without a passenger.
    const int waterline = data_.get(var::kWaterline);
    const int lastPlayer = data_.get(var::kPlayerCount) == 2 ? 1 : 0;
    for (int player = 0; player <= lastPlayer; ++player) {
        if (data_.get(var::kPlayerIcon, player) != 0) {
            continue;
        }
        if (word(data_.get(var::kPlayerScreenY, player) - waterline + 0x12) == 0) {
            return player;
        }
    }
    return -1;
}

PassengerStep Game::walkTowards(int slot, int player, bool moved)
{
    // 113b:196f / 113b:2124: walk (or swim) to the copter door; reaching it boards.
    const std::uint16_t type = passengerType(slot);
    const int position = word(data_.get(var::kPassengerScreenX, slot) + typeWord(type, 0) - 0x10);
    const int door = data_.get(var::kPlayerScreenX, player);
    if (position > door) {
        data_.set(var::kPassengerSprite, slot, passengerFrame(slot, 0x32));
        if (moved) {
            data_.set(var::kPassengerX, slot, data_.get(var::kPassengerX, slot) - 0x20);
        }
    } else if (position < door) {
        data_.set(var::kPassengerSprite, slot, passengerFrame(slot, 0x34));
        if (moved) {
            data_.set(var::kPassengerX, slot, data_.get(var::kPassengerX, slot) + 0x20);
        }
    } else {
        return jumpPassenger(slot, 0x20, player);
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------------------------------------
// Level setup and the per-frame loops

void Game::loadPassengers()
{
    // 113b:3aa9
    auto source = static_cast<std::uint16_t>(data_.get(var::kPassengerList));
    for (int slot = 0;; ++slot) {
        const auto type = data_.word(source);
        data_.set(var::kPassengerType, slot, type);
        if (type == 0xFFFF || slot >= var::kPassengerSlots) {
            return;
        }
        data_.set(var::kPassengerStartPlatform, slot, data_.word(static_cast<std::uint16_t>(source + 2)));
        data_.set(var::kPassengerRoute, slot, data_.word(static_cast<std::uint16_t>(source + 4)));
        source = static_cast<std::uint16_t>(source + 6);
        data_.set(var::kPassengerSprite, slot, -1);
        data_.set(var::kPassengerDrawnSprite, slot, -1);
        data_.set(var::kPassengerBubble, slot, -1);
        data_.set(var::kPassengerDrawnBubble, slot, -1);
        if (type == 0x78DC) {
            // The rock starts lying at its platform's stop.
            data_.set(var::kPassengerHandler, slot, typeWord(type, 0x10));
            const int platform = data_.get(var::kPassengerStartPlatform, slot);
            data_.set(var::kPassengerX, slot, (data_.get(var::kPlatformStopX, platform) - 8) * 32);
            data_.set(var::kPassengerY, slot, (data_.get(var::kPlatformY, platform) - 0xB) * 32);
        } else {
            data_.set(var::kPassengerHandler, slot, typeWord(type, 8));
        }
    }
}

void Game::resetCharacterSprites()
{
    // 113b:0b4f
    for (int slot = 0; slot < 5; ++slot) {
        data_.set(var::kEnemySprite, slot, -1);
        data_.set(var::kEnemyDrawnSprite, slot, -1);
    }
    for (int slot = 0; slot < var::kPassengerSlots; ++slot) {
        data_.set(var::kPassengerSprite, slot, -1);
        data_.set(var::kPassengerDrawnSprite, slot, -1);
    }
    for (int slot = 0; slot < var::kItemSlots; ++slot) {
        data_.set(var::kItemSprite, slot, -1);
        data_.set(var::kItemDrawnSprite, slot, -1);
    }
}

void Game::updatePassengers()
{
    // 113b:1486
    for (int slot = 0; slot < var::kPassengerSlots && data_.get(var::kPassengerType, slot) != -1; ++slot) {
        runPassenger(slot, static_cast<std::uint16_t>(data_.get(var::kPassengerHandler, slot)), -1);
    }
}

void Game::erasePassengers()
{
    // 113b:0cb6
    for (int slot = 0; slot < var::kPassengerSlots && data_.get(var::kPassengerType, slot) != -1; ++slot) {
        const int drawn = data_.get(var::kPassengerDrawnSprite, slot);
        data_.set(var::kPassengerDrawnSprite, slot, data_.get(var::kPassengerSprite, slot));
        const int x = data_.get(var::kPassengerDrawnX, slot);
        const int y = data_.get(var::kPassengerDrawnY, slot);
        if (drawn == -1 || y >= 0xC0) {
            continue;
        }
        video_.restoreSpriteRect(drawn, x, y);
        const int bubble = data_.get(var::kPassengerDrawnBubble, slot);
        data_.set(var::kPassengerDrawnBubble, slot, data_.get(var::kPassengerBubble, slot));
        if (bubble != -1) {
            video_.restoreSpriteRect(bubble, x + 0xB, y - 0xD);
        }
    }
}

void Game::drawPassengers()
{
    // 113b:0de7
    for (int slot = 0; slot < var::kPassengerSlots && data_.get(var::kPassengerType, slot) != -1; ++slot) {
        const int sprite = data_.get(var::kPassengerSprite, slot);
        if (sprite == -1) {
            continue;
        }
        data_.set(var::kPassengerDrawnX, slot, data_.get(var::kPassengerScreenX, slot));
        data_.set(var::kPassengerDrawnY, slot, data_.get(var::kPassengerScreenY, slot));
        const int x = data_.get(var::kPassengerX, slot) >> 5;
        const int y = data_.get(var::kPassengerY, slot) >> 5;
        data_.set(var::kPassengerScreenX, slot, x);
        data_.set(var::kPassengerScreenY, slot, y);
        if (y < 0xC0 && y > -0xB) {
            video_.drawSprite(sprite, x, y);
        }
    }
}

void Game::drawPassengerBubbles()
{
    // 113b:0f54
    for (int slot = 0; slot < var::kPassengerSlots && data_.get(var::kPassengerType, slot) != -1; ++slot) {
        const int bubble = data_.get(var::kPassengerBubble, slot);
        if (data_.get(var::kPassengerSprite, slot) == -1 || bubble == -1) {
            continue;
        }
        video_.drawSprite(bubble, data_.get(var::kPassengerScreenX, slot) + 0xB,
                          data_.get(var::kPassengerScreenY, slot) - 0xD);
    }
}

// ---------------------------------------------------------------------------------------------------------
// State routines

void Game::runPassenger(int slot, std::uint16_t handler, int player)
{
    // The original chains routines with JMP; each routine returns the next one instead. Real chains are a few
    // jumps long, the limit only guards against corrupt data.
    constexpr int kMaxJumps = 64;
    PassengerStep next = PassengerJump{.handler = handler, .player = player};
    for (int jumps = 0; next; ++jumps) {
        if (jumps == kMaxJumps) {
            reportUnknownRoutine("passenger (endless chain)", next->handler);
            return;
        }
        next = passengerRoutine(slot, next->handler, next->player);
    }
}

PassengerStep Game::passengerRoutine(int slot, std::uint16_t handler, int player)
{
    switch (handler) {
    case 0x149C: return passengerStartTrip(slot);
    case 0x1509: return passengerAwaitAppearance(slot);
    case 0x153B: return passengerAppear(slot);
    case 0x1582: return passengerAppearing(slot);
    case 0x15B4: return passengerStartWaiting(slot);
    case 0x15D7: return passengerWaiting(slot);
    case 0x16F6: return passengerStartCalling(slot);
    case 0x172A: return passengerCalling(slot);
    case 0x17E6: return passengerStartHailing(slot);
    case 0x180A: return passengerHailing(slot);
    case 0x18C8: return passengerStartWalkToCopter(slot);
    case 0x18E6: return passengerWalkToCopter(slot);
    case 0x19E0: return swimmerBoard(slot, player);
    case 0x19FB: return passengerBoard(slot, player);
    case 0x1A42: return passengerRiding(slot);
    case 0x1A7E: return passengerArrive(slot, player);
    case 0x1B29: return passengerWalkAway(slot);
    case 0x1BBE: return passengerStartVanishing(slot);
    case 0x1BD6: return passengerVanishing(slot);
    case 0x1C0F: return rockStartLying(slot);
    case 0x1C27: return rockLying(slot);
    case 0x1C48: return rockPickUp(slot, player);
    case 0x1C6B: return rockCarried(slot);
    case 0x1C81: return rockDrop(slot, player);
    case 0x1CEE: return rockFalling(slot);
    case 0x1DA8: return swimmerStart(slot);
    case 0x1DD5: return swimmerSurfacing(slot);
    case 0x1E9C: return swimmerStartDrowning(slot);
    case 0x1EC0: return swimmerDrowning(slot);
    case 0x1F24: return swimmerStartFloating(slot);
    case 0x1F43: return swimmerFloating(slot);
    case 0x1FE2: return swimmerCalling(slot);
    case 0x2068: return swimmerHailing(slot);
    case 0x20C1: return swimmerSwimToCopter(slot);
    case kReturn:
    case kReturn2:
        return std::nullopt;
    default:
        reportUnknownRoutine("passenger", handler);
        return std::nullopt;
    }
}

void Game::reportUnknownRoutine(const char* kind, std::uint16_t handler)
{
    if (!unknownRoutineReported_) {
        unknownRoutineReported_ = true;
        static_cast<void>(std::fprintf(stderr, "ugh: %s routine 113b:%04x is not ported\n", kind, handler)); // NOLINT(cppcoreguidelines-pro-type-vararg): diagnostics only
    }
}

PassengerStep Game::passengerStartTrip(int slot)
{
    // 113b:149c: next leg of the route, or the end of it.
    const auto route = static_cast<std::uint16_t>(data_.get(var::kPassengerRoute, slot));
    const auto destination = static_cast<std::int16_t>(data_.word(static_cast<std::uint16_t>(route + 4)));
    if (destination == -1) {
        const auto remaining = static_cast<std::int8_t>(data_.get(var::kDeliveries) - 1);
        if (remaining >= 0) {
            data_.set(var::kDeliveries, remaining);
            if (remaining == 0) {
                // Last passenger delivered: the level is complete.
                data_.set(var::kFadeStep, -2);
                data_.set(var::kFadeLevel, data_.get(var::kFadeLevel) - 2);
                data_.set(var::kLevelCompleted, 0xFE);
            }
        }
        data_.set(var::kPassengerSprite, slot, -1);
        const std::int16_t next = typeWord(passengerType(slot), 0x2C);
        data_.set(var::kPassengerHandler, slot, next);
        return PassengerJump{.handler = static_cast<std::uint16_t>(next), .player = -1};
    }
    data_.set(var::kPassengerDestination, slot, destination);
    data_.set(var::kPassengerDestinationOffset, slot, destination * 2);
    const auto platform = static_cast<std::int16_t>(data_.word(route));
    data_.set(var::kPassengerPlatform, slot, platform);
    data_.set(var::kPassengerPlatformOffset, slot, platform * 2);
    // 113b:3d5a: Amiga 50 Hz wait times scaled by 1.5.
    const auto wait = static_cast<std::int16_t>(data_.word(static_cast<std::uint16_t>(route + 2)));
    data_.set(var::kPassengerCountdown, slot, wait + (wait >> 1));
    data_.set(var::kPassengerHandler, slot, typeWord(passengerType(slot), 0xA));
    return std::nullopt;
}

PassengerStep Game::passengerAwaitAppearance(int slot)
{
    // 113b:1509: wait, then appear once the platform is free.
    data_.set(var::kPassengerSprite, slot, -1);
    int countdown = data_.get(var::kPassengerCountdown, slot);
    if (countdown != 0) {
        countdown = word(countdown - 1);
        data_.set(var::kPassengerCountdown, slot, countdown);
        if (countdown != 0) {
            return std::nullopt;
        }
    }
    if (platformWord(var::kPlatform2999, data_.get(var::kPassengerPlatformOffset, slot)) != -1) {
        return std::nullopt;
    }
    return jumpPassenger(slot, 0xC, -1);
}

PassengerStep Game::passengerAppear(int slot)
{
    // 113b:153b
    const std::uint16_t type = passengerType(slot);
    data_.set(var::kPassengerHandler, slot, typeWord(type, 0xE));
    const int offset = data_.get(var::kPassengerPlatformOffset, slot);
    data_.set(var::kPlatform2999, offset >> 1, slot * 2);
    const int y = word(platformWord(var::kPlatformY, offset) - typeWord(type, 2));
    data_.set(var::kPassengerScreenY, slot, y);
    data_.set(var::kPassengerY, slot, y * 32);
    const int x = word(platformWord(var::kPlatform2949, offset) - typeWord(type, 0));
    data_.set(var::kPassengerScreenX, slot, x);
    data_.set(var::kPassengerX, slot, x * 32);
    restartPassengerAnimation(slot);
    return std::nullopt;
}

PassengerStep Game::passengerAppearing(int slot)
{
    // 113b:1582: one pass through the appear animation.
    if (!passengerAnimationTick(slot)) {
        return std::nullopt;
    }
    const auto list = static_cast<std::uint16_t>(typeWord(passengerType(slot), 0x36));
    const auto frame = static_cast<std::int16_t>(
        data_.word(static_cast<std::uint16_t>(list + data_.get(var::kPassengerAnimationStep, slot))));
    if (frame == -1) {
        return jumpPassenger(slot, 0x10, -1);
    }
    data_.set(var::kPassengerSprite, slot, frame);
    return std::nullopt;
}

PassengerStep Game::passengerStartWaiting(int slot)
{
    // 113b:15b4
    data_.set(var::kPassengerHandler, slot, typeWord(passengerType(slot), 0x12));
    restartPassengerAnimation(slot);
    data_.set(var::kPassengerTimer, slot, -1);
    data_.set(var::kPassengerBubble, slot, -1);
    return std::nullopt;
}

PassengerStep Game::passengerWaiting(int slot)
{
    // 113b:15d7: walk to the waiting spot and stand there until an empty copter lands.
    if (auto jump = passengerFallsInWater(slot)) {
        return jump;
    }
    if (passengerAnimationTick(slot)) {
        const std::uint16_t type = passengerType(slot);
        const int position = word(data_.get(var::kPassengerScreenX, slot) + typeWord(type, 0));
        const int spot = platformWord(var::kPlatform295d, data_.get(var::kPassengerPlatformOffset, slot));
        if (position == spot) {
            if (data_.get(var::kPassengerTimer, slot) != 1) {
                data_.set(var::kPassengerAnimationStep, slot, 0);
            }
            data_.set(var::kPassengerSprite, slot, passengerFrame(slot, 0x2E));
            data_.set(var::kPassengerTimer, slot, 1);
        } else {
            data_.set(var::kPassengerTimer, slot, 0);
            if (position < spot) {
                data_.set(var::kPassengerSprite, slot, passengerFrame(slot, 0x34));
                data_.set(var::kPassengerX, slot, data_.get(var::kPassengerX, slot) + 0x20);
            } else {
                data_.set(var::kPassengerSprite, slot, passengerFrame(slot, 0x32));
                data_.set(var::kPassengerX, slot, data_.get(var::kPassengerX, slot) - 0x20);
            }
        }
    }
    if (auto jump = passengerKnockedOff(slot)) {
        return jump;
    }
    if (emptyCopterOnPlatform(data_.get(var::kPassengerPlatform, slot)) >= 0) {
        return jumpPassenger(slot, 0x14, -1);
    }
    return std::nullopt;
}

PassengerStep Game::passengerStartCalling(int slot)
{
    // 113b:16f6: show the destination in a speech bubble for 140 frames.
    data_.set(var::kPassengerHandler, slot, typeWord(passengerType(slot), 0x16));
    restartPassengerAnimation(slot);
    auto bubble = static_cast<std::uint16_t>(data_.get(var::kPassengerDestination, slot) + 0x10C);
    if (bubble >= 0x112) {
        bubble = 0x111;
    }
    data_.set(var::kPassengerBubble, slot, bubble);
    data_.set(var::kPassengerTimer, slot, 0x8C);
    return std::nullopt;
}

PassengerStep Game::passengerCalling(int slot)
{
    // 113b:172a
    if (auto jump = passengerFallsInWater(slot)) {
        return jump;
    }
    if (auto jump = passengerKnockedOff(slot)) {
        return jump;
    }
    const int platform = data_.get(var::kPassengerPlatform, slot);
    int landed = -1;
    for (int player = 0; player < data_.get(var::kPlayerCount); ++player) {
        if (data_.get(var::kLandedPlatform, player) == platform) {
            landed = player;
            break;
        }
    }
    if (landed < 0 || data_.get(var::kPlayerIcon, landed) != 0) {
        return jumpPassenger(slot, 0x18, -1);
    }
    if (passengerAnimationTick(slot)) {
        data_.set(var::kPassengerSprite, slot, passengerFrame(slot, 0x30));
    }
    const int timer = word(data_.get(var::kPassengerTimer, slot) - 1);
    data_.set(var::kPassengerTimer, slot, timer);
    if (timer == 0) {
        return jumpPassenger(slot, 0x1C, -1);
    }
    return std::nullopt;
}

PassengerStep Game::passengerStartHailing(int slot)
{
    // 113b:17e6: "taxi!" bubble for 140 frames.
    data_.set(var::kPassengerHandler, slot, typeWord(passengerType(slot), 0x1A));
    restartPassengerAnimation(slot);
    data_.set(var::kPassengerBubble, slot, 0x112);
    data_.set(var::kPassengerTimer, slot, 0x8C);
    return std::nullopt;
}

PassengerStep Game::passengerHailing(int slot)
{
    // 113b:180a
    if (auto jump = passengerFallsInWater(slot)) {
        return jump;
    }
    if (auto jump = passengerKnockedOff(slot)) {
        return jump;
    }
    if (passengerAnimationTick(slot)) {
        data_.set(var::kPassengerSprite, slot, passengerFrame(slot, 0x30));
    }
    const int timer = word(data_.get(var::kPassengerTimer, slot) - 1);
    data_.set(var::kPassengerTimer, slot, timer);
    if (timer != 0) {
        return std::nullopt;
    }
    if (emptyCopterOnPlatform(data_.get(var::kPassengerPlatform, slot)) >= 0) {
        return jumpPassenger(slot, 0x14, -1);
    }
    return jumpPassenger(slot, 0x10, -1);
}

PassengerStep Game::passengerStartWalkToCopter(int slot)
{
    // 113b:18c8
    data_.set(var::kPassengerHandler, slot, typeWord(passengerType(slot), 0x1E));
    restartPassengerAnimation(slot);
    data_.set(var::kPassengerBubble, slot, -1);
    return std::nullopt;
}

PassengerStep Game::passengerWalkToCopter(int slot)
{
    // 113b:18e6
    if (auto jump = passengerFallsInWater(slot)) {
        return jump;
    }
    if (auto jump = passengerKnockedOff(slot)) {
        return jump;
    }
    const int platform = data_.get(var::kPassengerPlatform, slot);
    int landed = -1;
    for (int player = 0; player < data_.get(var::kPlayerCount); ++player) {
        if (data_.get(var::kLandedPlatform, player) == platform) {
            landed = player;
            break;
        }
    }
    if (landed < 0) {
        return jumpPassenger(slot, 0x18, -1);
    }
    return walkTowards(slot, landed, passengerAnimationTick(slot));
}

PassengerStep Game::swimmerBoard(int slot, int player)
{
    // 113b:19e0: a swimmer climbs in and becomes a normal passenger again.
    const std::uint16_t type = passengerType(slot);
    data_.set(var::kPlayerTip, player, typeWord(type, 0x40));
    data_.set(var::kPlayerTipMinimum, player, typeWord(type, 0x42));
    data_.set(var::kPassengerType, slot, typeWord(type, kTypeNext));
    boardCopter(slot, player);
    return std::nullopt;
}

PassengerStep Game::passengerBoard(int slot, int player)
{
    // 113b:19fb
    const std::uint16_t type = passengerType(slot);
    data_.set(var::kPlayerTip, player, typeWord(type, 0x40));
    data_.set(var::kPlayerTipMinimum, player, typeWord(type, 0x42));
    boardCopter(slot, player);
    return std::nullopt;
}

void Game::boardCopter(int slot, int player)
{
    // 113b:1a0d
    const std::uint16_t type = passengerType(slot);
    data_.set(var::kPassengerHandler, slot, typeWord(type, 0x22));
    data_.set(var::kPlayerIcon, player, typeWord(type, 0x46));
    data_.set(var::kPlayerIcon2, player,
              platformWord(var::kPlatform2985, data_.get(var::kPassengerDestinationOffset, slot)));
    data_.set(var::kPassengerTimer, slot, player * 2);
    data_.set(var::kPassengerSprite, slot, -1);
    data_.set(var::kPassengerQuickBonus, slot, 0xC8);
    data_.set(var::kPlatform2999, data_.get(var::kPassengerPlatformOffset, slot) >> 1, -1);
}

PassengerStep Game::passengerRiding(int slot)
{
    // 113b:1a42: the tip sinks towards its minimum while flying.
    const int player = data_.get(var::kPassengerTimer, slot) >> 1;
    const auto tip = static_cast<std::uint16_t>(data_.get(var::kPlayerTip, player));
    if (tip > static_cast<std::uint16_t>(data_.get(var::kPlayerTipMinimum, player))) {
        data_.set(var::kPlayerTip, player, tip - 1);
    }
    const int quickBonus = data_.get(var::kPassengerQuickBonus, slot);
    if (quickBonus > 0) {
        data_.set(var::kPassengerQuickBonus, slot, quickBonus - 1);
    }
    if (data_.get(var::kPassengerDestination, slot) == data_.get(var::kLandedPlatform, player)) {
        return jumpPassenger(slot, 0x24, player);
    }
    return std::nullopt;
}

PassengerStep Game::passengerArrive(int slot, int player)
{
    // 113b:1a7e: get out, pay tip * multiplier, maybe drop a bonus for a quick delivery.
    const std::uint16_t type = passengerType(slot);
    data_.set(var::kPassengerHandler, slot, typeWord(type, 0x26));
    data_.set(var::kPlayerIcon, player, 0);
    data_.set(var::kPlayerIcon2, player, 0);
    const int carrier = data_.get(var::kPassengerTimer, slot) >> 1;
    data_.set(var::kPassengerX, slot, ((0x10 - typeWord(type, 0)) * 32) + data_.get(var::kPlayerX, carrier));

    const std::uint32_t pay = static_cast<std::uint32_t>(static_cast<std::uint16_t>(data_.get(var::kPlayerTip, carrier))) *
                              static_cast<std::uint16_t>(data_.get(var::kMultiplierWord));
    const std::uint32_t score =
        ((static_cast<std::uint32_t>(static_cast<std::uint16_t>(data_.get(var::kScoreHigh))) << 16U) |
         static_cast<std::uint16_t>(data_.get(var::kScoreLow))) +
        pay;
    data_.set(var::kScoreLow, static_cast<int>(score & 0xFFFFU));
    data_.set(var::kScoreHigh, static_cast<int>(score >> 16U));

    if (data_.get(var::kPassengerQuickBonus, slot) != 0) {
        const auto multiplier = static_cast<std::uint16_t>(data_.get(var::kMultiplierWord));
        const auto maximum = static_cast<std::uint16_t>(data_.get(var::kMaxMultiplier, data_.get(var::kDifficulty)));
        if (multiplier < maximum) {
            spawnItem(var::kBonusItemType, data_.get(var::kPlayerX, carrier) + 0x200,
                      data_.get(var::kPlayerY, carrier) + 0x140, 0, 0);
            static_cast<void>(playSound(0x4274, 0xFF, 1)); // bonus
        }
    }
    const int y = word(platformWord(var::kPlatformY, data_.get(var::kPassengerDestinationOffset, slot)) -
                       typeWord(type, 2));
    data_.set(var::kPassengerY, slot, y * 32);
    restartPassengerAnimation(slot);
    data_.set(var::kTipRefresh, 1);
    return std::nullopt;
}

PassengerStep Game::passengerWalkAway(int slot)
{
    // 113b:1b29: walk from the copter to the platform's exit.
    const int offset = data_.get(var::kPassengerDestinationOffset, slot);
    if (!passengerAnimationTick(slot)) {
        return std::nullopt;
    }
    const std::uint16_t type = passengerType(slot);
    const int position = word(data_.get(var::kPassengerScreenX, slot) + typeWord(type, 0));
    const int exit = platformWord(var::kPlatform2949, offset);
    if (position == exit) {
        return jumpPassenger(slot, 0x28, -1);
    }
    if (position < exit) {
        data_.set(var::kPassengerSprite, slot, passengerFrame(slot, 0x34));
        data_.set(var::kPassengerX, slot, data_.get(var::kPassengerX, slot) + 0x20);
    } else {
        data_.set(var::kPassengerSprite, slot, passengerFrame(slot, 0x32));
        data_.set(var::kPassengerX, slot, data_.get(var::kPassengerX, slot) - 0x20);
    }
    if (word((data_.get(var::kPassengerX, slot) >> 5) + typeWord(type, 0)) == exit) {
        return jumpPassenger(slot, 0x28, -1);
    }
    return std::nullopt;
}

PassengerStep Game::passengerStartVanishing(int slot)
{
    // 113b:1bbe
    data_.set(var::kPassengerHandler, slot, typeWord(passengerType(slot), 0x2A));
    restartPassengerAnimation(slot);
    return std::nullopt;
}

PassengerStep Game::passengerVanishing(int slot)
{
    // 113b:1bd6: disappear, then start the next leg of the route.
    if (!passengerAnimationTick(slot)) {
        return std::nullopt;
    }
    const auto list = static_cast<std::uint16_t>(typeWord(passengerType(slot), 0x38));
    const auto entry = static_cast<std::uint16_t>(list + data_.get(var::kPassengerAnimationStep, slot));
    data_.set(var::kPassengerSprite, slot, data_.word(entry));
    if (data_.word(static_cast<std::uint16_t>(entry + 2)) != 0xFFFF) {
        return std::nullopt;
    }
    data_.set(var::kPassengerRoute, slot, data_.get(var::kPassengerRoute, slot) + 4);
    return jumpPassenger(slot, 8, -1);
}

PassengerStep Game::rockStartLying(int slot)
{
    // 113b:1c0f
    data_.set(var::kPassengerHandler, slot, typeWord(passengerType(slot), 0x12));
    restartPassengerAnimation(slot);
    return std::nullopt;
}

PassengerStep Game::rockLying(int slot)
{
    // 113b:1c27: an empty copter touching the rock picks it up.
    const int player = playerTouchingPassenger(slot);
    if (player >= 0 && data_.get(var::kPlayerIcon, player) == 0) {
        return jumpPassenger(slot, 0x20, player);
    }
    data_.set(var::kPassengerSprite, slot, 0x220);
    return std::nullopt;
}

PassengerStep Game::rockPickUp(int slot, int player)
{
    // 113b:1c48
    const std::uint16_t type = passengerType(slot);
    data_.set(var::kPassengerHandler, slot, typeWord(type, 0x22));
    data_.set(var::kPlayerIcon, player, typeWord(type, 0x46));
    data_.set(var::kPlayerIcon2, player, 7);
    data_.set(var::kPassengerTimer, slot, player * 2);
    data_.set(var::kPassengerSprite, slot, -1);
    return std::nullopt;
}

PassengerStep Game::rockCarried(int slot)
{
    // 113b:1c6b: fire drops it.
    const int player = data_.get(var::kPassengerTimer, slot) >> 1;
    if ((data_.byte(static_cast<std::uint16_t>(var::kFire.offset + (player * 2))) & 0x80U) != 0) {
        return jumpPassenger(slot, 0x24, player);
    }
    return std::nullopt;
}

PassengerStep Game::rockDrop(int slot, int player)
{
    // 113b:1c81: released with the copter's speed.
    const std::uint16_t type = passengerType(slot);
    data_.set(var::kPlayerIcon, player, 0);
    data_.set(var::kPlayerIcon2, player, 0);
    data_.set(var::kPassengerCountdown, slot, data_.get(var::kVelocityX, player) >> 5);
    data_.set(var::kPassengerVelocityY, slot, data_.get(var::kVelocityY, player) >> 5);
    const int x = data_.get(var::kPlayerX, player) + 0x200;
    const int y = data_.get(var::kPlayerY, player) + 0x140;
    data_.set(var::kPassengerX, slot, x - (typeWord(type, 0) * 32));
    data_.set(var::kPassengerY, slot, y - (typeWord(type, 2) * 16));
    data_.set(var::kPassengerSprite, slot, 0x221);
    data_.set(var::kPassengerHandler, slot, typeWord(type, 0x26));
    static_cast<void>(playSound(0x4632, 0xFF, 1)); // rock drop
    return std::nullopt;
}

PassengerStep Game::rockFalling(int slot)
{
    // 113b:1cee: fall until a platform catches it or it leaves the screen.
    const std::uint16_t type = passengerType(slot);
    const auto vanish = [&] {
        data_.set(var::kPassengerSprite, slot, -1);
        data_.set(var::kPassengerHandler, slot, typeWord(type, 0x2C));
    };
    const std::int16_t x = word(data_.get(var::kPassengerCountdown, slot) + data_.get(var::kPassengerX, slot));
    if (x <= -0x200 || x >= 0x2800) {
        vanish();
        return std::nullopt;
    }
    data_.set(var::kPassengerX, slot, x);
    const std::int16_t speed = word(data_.get(var::kPassengerVelocityY, slot) + 2);
    data_.set(var::kPassengerVelocityY, slot, speed);
    if (speed < 0) {
        data_.set(var::kPassengerY, slot, data_.get(var::kPassengerY, slot) + speed);
        return std::nullopt;
    }
    const std::int16_t before = data_.get(var::kPassengerY, slot);
    const std::int16_t after = word(speed + before);
    if (after >= 0x1800) {
        vanish();
        return std::nullopt;
    }
    data_.set(var::kPassengerY, slot, after);
    const int bottomAfter = (after >> 5) + typeWord(type, 2);
    const int bottomBefore = (before >> 5) + typeWord(type, 2);
    for (int platform = 0; data_.get(var::kPlatformLeft, platform) >= 0; ++platform) {
        const int platformY = data_.get(var::kPlatformY, platform);
        if (bottomBefore > platformY || bottomAfter < platformY) {
            continue;
        }
        const int centre = (x >> 5) + typeWord(type, 0);
        if (centre < data_.get(var::kPlatformLeft, platform) || centre > data_.get(var::kPlatformRight, platform)) {
            continue;
        }
        data_.set(var::kPassengerY, slot, (platformY - typeWord(type, 2)) * 32);
        return jumpPassenger(slot, 0x10, -1);
    }
    return std::nullopt;
}

PassengerStep Game::swimmerStart(int slot)
{
    // 113b:1da8: fell into the water.
    const std::uint16_t type = passengerType(slot);
    data_.set(var::kPassengerHandler, slot, typeWord(type, 0xA));
    data_.set(var::kPlatform2999, data_.get(var::kPassengerPlatformOffset, slot) >> 1, -1);
    data_.set(var::kPassengerBubble, slot, -1);
    restartPassengerAnimation(slot);
    data_.set(var::kPassengerVelocityY, slot, 0);
    return std::nullopt;
}

PassengerStep Game::swimmerSurfacing(int slot)
{
    // 113b:1dd5: sink, then float up to the surface.
    const std::uint16_t type = passengerType(slot);
    static_cast<void>(passengerAnimationTick(slot));
    data_.set(var::kPassengerSprite, slot, passengerFrame(slot, 0x2E));

    const int waterline = data_.get(var::kWaterline);
    const std::int16_t depth = word(data_.get(var::kPassengerScreenY, slot) - typeWord(type, 2) - waterline);
    int speed = data_.get(var::kPassengerVelocityY, slot);
    if (depth < 0) {
        speed += 0x27;
    } else if (speed <= 0) {
        speed -= 0x5C;
    } else {
        speed -= 0x175;
    }
    const std::int16_t velocity = clampSpeed(speed);
    data_.set(var::kPassengerVelocityY, slot, velocity);
    const std::int16_t y = word((velocity >> 6) + data_.get(var::kPassengerY, slot));
    data_.set(var::kPassengerY, slot, y);
    if (velocity >= 0 || word((y >> 5) - typeWord(type, 2)) > waterline) {
        return std::nullopt;
    }
    data_.set(var::kPassengerY, slot, (waterline - typeWord(type, 2)) * 32);
    return jumpPassenger(slot, 0x10, -1);
}

PassengerStep Game::swimmerStartDrowning(int slot)
{
    // 113b:1e9c
    data_.set(var::kPassengerHandler, slot, typeWord(passengerType(slot), 0xE));
    data_.set(var::kPassengerBubble, slot, -1);
    restartPassengerAnimation(slot);
    data_.set(var::kPassengerVelocityY, slot, 0);
    return std::nullopt;
}

PassengerStep Game::swimmerDrowning(int slot)
{
    // 113b:1ec0: sink out of sight.
    const std::uint16_t type = passengerType(slot);
    static_cast<void>(passengerAnimationTick(slot));
    data_.set(var::kPassengerSprite, slot, passengerFrame(slot, 0x2E));
    const std::int16_t speed = word(data_.get(var::kPassengerVelocityY, slot) + 0x27);
    data_.set(var::kPassengerVelocityY, slot, speed);
    const auto y = static_cast<std::uint16_t>((speed >> 6) + data_.get(var::kPassengerY, slot));
    if (y >= 0x1800) {
        data_.set(var::kPassengerHandler, slot, typeWord(type, 0x2C));
        data_.set(var::kPassengerSprite, slot, -1);
        return std::nullopt;
    }
    data_.set(var::kPassengerY, slot, y);
    return std::nullopt;
}

PassengerStep Game::swimmerStartFloating(int slot)
{
    // 113b:1f24: float for a while before drowning.
    const std::uint16_t type = passengerType(slot);
    data_.set(var::kPassengerHandler, slot, typeWord(type, 0x12));
    restartPassengerAnimation(slot);
    data_.set(var::kPassengerCountdown, slot, typeWord(type, 0x44));
    return std::nullopt;
}

PassengerStep Game::swimmerFloating(int slot)
{
    // 113b:1f43: an empty copter sitting on the water rescues the swimmer.
    static_cast<void>(passengerAnimationTick(slot));
    data_.set(var::kPassengerSprite, slot, passengerFrame(slot, 0x30));
    followWaterline(slot);
    if (passengerType(slot) != kFloatingWomanType) {
        const int lastPlayer = data_.get(var::kPlayerCount) == 2 ? 1 : 0;
        for (int player = 0; player <= lastPlayer; ++player) {
            if (data_.get(var::kVelocityY, player) != 0 || data_.get(var::kPlayerIcon, player) != 0) {
                continue;
            }
            if (word(data_.get(var::kPlayerScreenY, player) - data_.get(var::kWaterline) + 0x12) == 0) {
                return jumpPassenger(slot, 0x14, -1);
            }
        }
    }
    const int countdown = word(data_.get(var::kPassengerCountdown, slot) - 1);
    data_.set(var::kPassengerCountdown, slot, countdown);
    if (countdown == 0) {
        return jumpPassenger(slot, 0xC, -1);
    }
    return std::nullopt;
}

PassengerStep Game::swimmerCalling(int slot)
{
    // 113b:1fe2: like passengerCalling, but any copter on the water keeps the swimmer calling.
    followWaterline(slot);
    const int waterline = data_.get(var::kWaterline);
    const bool copterOnWater =
        word(data_.get(var::kPlayerScreenY, 0) - waterline + 0x12) == 0 ||
        (data_.get(var::kPlayerCount) == 2 && word(data_.get(var::kPlayerScreenY, 1) - waterline + 0x12) == 0);
    if (!copterOnWater) {
        return jumpPassenger(slot, 0x18, -1);
    }
    if (passengerAnimationTick(slot)) {
        data_.set(var::kPassengerSprite, slot, passengerFrame(slot, 0x30));
    }
    const int timer = word(data_.get(var::kPassengerTimer, slot) - 1);
    data_.set(var::kPassengerTimer, slot, timer);
    if (timer == 0) {
        return jumpPassenger(slot, 0x1C, -1);
    }
    return std::nullopt;
}

PassengerStep Game::swimmerHailing(int slot)
{
    // 113b:2068: hail for a while, then go under.
    followWaterline(slot);
    if (passengerAnimationTick(slot)) {
        data_.set(var::kPassengerSprite, slot, passengerFrame(slot, 0x30));
    }
    const int timer = word(data_.get(var::kPassengerTimer, slot) - 1);
    data_.set(var::kPassengerTimer, slot, timer);
    if (timer == 0) {
        return jumpPassenger(slot, 0xC, -1);
    }
    return std::nullopt;
}

PassengerStep Game::swimmerSwimToCopter(int slot)
{
    // 113b:20c1
    followWaterline(slot);
    const int player = copterOnWater();
    if (player < 0) {
        return jumpPassenger(slot, 0x18, -1);
    }
    return walkTowards(slot, player, passengerAnimationTick(slot));
}

// ---------------------------------------------------------------------------------------------------------
// Bonus items

void Game::spawnItem(std::uint16_t type, int x, int y, int velocityX, int velocityY)
{
    // 113b:2b96. With all twelve slots taken the original returns through a misaligned stack (a crash); the
    // port just drops the item.
    for (int slot = var::kItemSlots - 1; slot >= 0; --slot) {
        if (data_.get(var::kItemSprite, slot) >= 0) {
            continue;
        }
        data_.set(var::kItemType, slot, type);
        data_.set(var::kItemX, slot, x - (typeWord(type, 0) * 32));
        data_.set(var::kItemY, slot, y - (typeWord(type, 2) * 16));
        data_.set(var::kItemVelocityX, slot, velocityX);
        data_.set(var::kItemVelocityY, slot, -(velocityY + typeWord(type, 0xC)));
        data_.set(var::kItemSprite, slot, typeWord(type, 0x10));
        data_.set(var::kItemHandler, slot, typeWord(type, 6));
        return;
    }
}

void Game::updateItems()
{
    // 113b:2b7f
    for (int slot = var::kItemSlots - 1; slot >= 0; --slot) {
        if (data_.get(var::kItemSprite, slot) == -1) {
            continue;
        }
        const auto handler = static_cast<std::uint16_t>(data_.get(var::kItemHandler, slot));
        switch (handler) {
        case 0x2BE4: itemFalling(slot); break;
        case 0x2C97: itemLand(slot); break;
        case 0x2CA9: itemLying(slot); break;
        default: reportUnknownRoutine("item", handler); break;
        }
    }
}

void Game::itemFalling(int slot)
{
    // 113b:2be4: like the rock, but platforms catch only items falling onto them from above.
    const auto type = static_cast<std::uint16_t>(data_.get(var::kItemType, slot));
    const std::int16_t x = word(data_.get(var::kItemVelocityX, slot) + data_.get(var::kItemX, slot));
    if (x <= -0x200 || x >= 0x2800) {
        data_.set(var::kItemSprite, slot, -1);
        return;
    }
    data_.set(var::kItemX, slot, x);
    const std::int16_t speed = word(data_.get(var::kItemVelocityY, slot) + 3);
    data_.set(var::kItemVelocityY, slot, speed);
    if (speed < 0) {
        data_.set(var::kItemY, slot, data_.get(var::kItemY, slot) + speed);
        return;
    }
    const std::int16_t before = data_.get(var::kItemY, slot);
    const std::int16_t after = word(speed + before);
    if (after >= 0x1800) {
        data_.set(var::kItemSprite, slot, -1);
        return;
    }
    data_.set(var::kItemY, slot, after);
    const int bottomAfter = (after >> 5) + typeWord(type, 2);
    const int bottomBefore = (before >> 5) + typeWord(type, 2);
    for (int platform = 0; data_.get(var::kPlatformLeft, platform) >= 0; ++platform) {
        const int platformY = data_.get(var::kPlatformY, platform);
        if (bottomBefore >= platformY || bottomAfter < platformY) {
            continue;
        }
        const int centre = (x >> 5) + typeWord(type, 0);
        if (centre < data_.get(var::kPlatformLeft, platform) || centre - 1 > data_.get(var::kPlatformRight, platform)) {
            continue;
        }
        data_.set(var::kItemY, slot, (platformY - typeWord(type, 2)) * 32);
        itemLand(slot);
        return;
    }
}

void Game::itemLand(int slot)
{
    // 113b:2c97: lies for 560 frames.
    const auto type = static_cast<std::uint16_t>(data_.get(var::kItemType, slot));
    data_.set(var::kItemHandler, slot, typeWord(type, 0xA));
    data_.set(var::kItemVelocityX, slot, 0x230);
}

void Game::itemLying(int slot)
{
    // 113b:2ca9: collected by touching it: fuel, a life or a higher multiplier.
    const int lifetime = word(data_.get(var::kItemVelocityX, slot) - 1);
    data_.set(var::kItemVelocityX, slot, lifetime);
    if (lifetime == 0) {
        data_.set(var::kItemSprite, slot, -1);
        return;
    }
    if (playerTouchingItem(slot) < 0) {
        return;
    }
    const auto type = static_cast<std::uint16_t>(data_.get(var::kItemType, slot));
    switch (typeWord(type, 0x12)) {
    case 0: {
        const auto fuel = static_cast<std::uint16_t>(typeWord(type, 0xE) + data_.get(var::kFuel));
        data_.set(var::kFuel, std::min<std::uint16_t>(fuel, 0x5A3B));
        break;
    }
    case 1: {
        const auto lives = static_cast<std::uint8_t>(typeWord(type, 0xE) + data_.get(var::kLives));
        data_.set(var::kLives, std::min<std::uint8_t>(lives, 99));
        break;
    }
    default: {
        const auto multiplier = static_cast<std::uint16_t>(data_.get(var::kMultiplierWord));
        if (multiplier < static_cast<std::uint16_t>(data_.get(var::kMaxMultiplier, data_.get(var::kDifficulty)))) {
            data_.set(var::kMultiplier, static_cast<std::uint8_t>(multiplier + 1));
        }
        break;
    }
    }
    data_.set(var::kItemSprite, slot, -1);
}

int Game::playerTouchingItem(int slot) const
{
    // 113b:2207
    const auto type = static_cast<std::uint16_t>(data_.get(var::kItemType, slot));
    const int x = data_.get(var::kItemX, slot);
    const int y = data_.get(var::kItemY, slot);
    const int lastPlayer = data_.get(var::kPlayerCount) == 2 ? 1 : 0;
    for (int player = 0; player <= lastPlayer; ++player) {
        const int playerX = data_.get(var::kPlayerX, player);
        const int playerY = data_.get(var::kPlayerY, player);
        if (word((typeWord(type, 2) * 32) + y) < playerY) {
            continue;
        }
        if (word(((typeWord(type, 2) - 6) * 32) + y - 0x280) > playerY) {
            continue;
        }
        if (word(((typeWord(type, 0) + 3) * 32) + x - 0xA0) < playerX) {
            continue;
        }
        if (word(((typeWord(type, 0) - 3) * 32) + x - 0x340) <= playerX) {
            return player;
        }
    }
    return -1;
}

void Game::eraseItems()
{
    // 113b:0d44
    for (int slot = var::kItemSlots - 1; slot >= 0; --slot) {
        const int drawn = data_.get(var::kItemDrawnSprite, slot);
        data_.set(var::kItemDrawnSprite, slot, data_.get(var::kItemSprite, slot));
        if (drawn != -1) {
            video_.restoreSpriteRect(drawn, data_.get(var::kItemDrawnX, slot), data_.get(var::kItemDrawnY, slot));
        }
    }
}

void Game::drawItems()
{
    // 113b:0f0e
    for (int slot = var::kItemSlots - 1; slot >= 0; --slot) {
        const int sprite = data_.get(var::kItemSprite, slot);
        if (sprite == -1) {
            continue;
        }
        data_.set(var::kItemDrawnX, slot, data_.get(var::kItemScreenX, slot));
        data_.set(var::kItemDrawnY, slot, data_.get(var::kItemScreenY, slot));
        const int x = data_.get(var::kItemX, slot) >> 5;
        const int y = data_.get(var::kItemY, slot) >> 5;
        data_.set(var::kItemScreenX, slot, x);
        data_.set(var::kItemScreenY, slot, y);
        if (y < 0xC0) {
            video_.drawSprite(sprite, x, y);
        }
    }
}

} // namespace ugh
