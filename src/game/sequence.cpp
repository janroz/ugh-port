// Scripted sequences (113b:0720..0b4e): the two attract-mode demos, the opening flight before the first level
// and the closing sequence after the last one. Actors follow word scripts in the data segment over a level map.

#include "game/game.hpp"
#include "game/vars.hpp"

#include <algorithm>


namespace ugh {
namespace {

constexpr std::uint16_t kFirstDemoActors = 0x24DE;
constexpr std::uint16_t kSecondDemoActors = 0x1E40;
constexpr std::uint16_t kClosingActors = 0x1020;
constexpr std::uint16_t kOpeningActors = 0x1792;
constexpr std::uint16_t kTitleSceneMap = 0x7080; // offset in the unpacked level data

constexpr std::uint16_t kNoScript = 0xFFFF;
constexpr std::uint16_t kLoopEnd = 0x8000;
constexpr int kFadeSpeed = 4;
constexpr int kFullBrightness = 0x100;
constexpr int kNoWaterline = 0xC0;

/// Handlers named by the scripts (offsets in segment 113b).
constexpr std::uint16_t kNoHandler = 0x1C0E;           // a bare RET
constexpr std::uint16_t kFadeWhenInvisible = 0x0989;
constexpr std::uint16_t kClipSign = 0x099F;
constexpr int kSignSprite = 0xD6;                       // CS:462c points at its width byte (6b63:035a)

} // namespace

Task<Game::Outcome> Game::firstDemo()
{
    co_await sequenceScene(0xB4, 0, false); // 113b:0720
    const Outcome outcome = co_await playSequence(kFirstDemoActors);
    // 113b:0896: with x of actor 0 cleared, the sign gets its full width back.
    data_.set(var::kPassengerX, 0, 0);
    clipSign(0);
    co_return outcome;
}

Task<Game::Outcome> Game::secondDemo()
{
    co_await sequenceScene(kNoWaterline, kTitleSceneMap, true); // 113b:0712
    co_return co_await playSequence(kSecondDemoActors);
}

Task<> Game::closingSequence()
{
    co_await sequenceScene(0xB4, 0, false);
    co_await stopMusic(0x11);
    playMusic(0x4279);
    static_cast<void>(co_await playSequence(kClosingActors));
}

Task<> Game::openingFlight()
{
    co_await sequenceScene(0xB4, 0, false);
    static_cast<void>(co_await playSequence(kOpeningActors));
}

Task<> Game::sequenceScene(int waterline, std::uint16_t mapOffset, bool titleBackground)
{
    setWaterline(waterline);
    co_await video_.blackOut();
    video_.clearVram();
    video_.useGamePalette();
    constexpr auto kMapBytes = static_cast<std::size_t>(var::kMapColumns) * var::kMapRows;
    for (std::size_t i = 0; i < kMapBytes; ++i) {
        const std::size_t source = mapOffset + i;
        data_.setByte(static_cast<std::uint16_t>(var::kLevelMap + i), source < levelMaps_.size() ? levelMaps_[source] : 0);
    }
    drawLevelMap();
    if (titleBackground) {
        drawTitleBackground();
    }
    video_.saveBackground();
    video_.flip();
    video_.restoreScreen();
    if (data_.get(var::kWaterline) < kNoWaterline) {
        drawWaterSurfaceFrame();
    }
    video_.flip();
}

Task<Game::Outcome> Game::playSequence(std::uint16_t actors)
{
    startActors(actors);
    data_.set(var::kSequenceResult, 0);
    data_.set(var::kFadeLevel, 0);
    data_.set(var::kFadeStep, kFadeSpeed);
    static_cast<void>(keyboard_->poll());

    while (true) {
        const int level = data_.get(var::kFadeLevel);
        if (level < 0) {
            break;
        }
        if (level <= kFullBrightness) {
            co_await video_.fadeStep(static_cast<unsigned>(level));
            data_.set(var::kFadeLevel, level + data_.get(var::kFadeStep));
        } else {
            co_await clock_->nextFrame();
        }
        eraseActors();
        if (data_.get(var::kWaterline) < kNoWaterline) {
            drawWaterSurfaceFrame();
        }
        stepActors();
        drawActors();
        video_.flip();

        bool ending = actorsFinished();
        if (!ending) {
            if (joystickButtonPressed()) {
                data_.set(var::kSequenceResult, 0);
                ending = true;
            } else if (const Keyboard::Poll key = keyboard_->poll(); key.changed) {
                const auto scancode = static_cast<std::uint8_t>(key.code & 0x7FU);
                int result = 0;
                if (scancode == 0x10) { // Q
                    result = -1;
                } else if (scancode == 0x01) { // Esc
                    result = 1;
                }
                data_.set(var::kSequenceResult, result);
                ending = true;
            }
        }
        if (ending && data_.get(var::kFadeStep) >= 0) {
            data_.set(var::kFadeStep, -kFadeSpeed);
            data_.set(var::kFadeLevel, data_.get(var::kFadeLevel) - kFadeSpeed);
        }
    }

    const int result = data_.get(var::kSequenceResult);
    if (result < 0) {
        co_return Outcome::Quit;
    }
    co_return result == 0 ? Outcome::Continue : Outcome::Menu;
}

void Game::startActors(std::uint16_t actors)
{
    int actor = 0;
    for (;; ++actor) {
        const std::uint16_t record = data_.word(static_cast<std::uint16_t>(actors + (2 * actor)));
        if (record == kNoScript) {
            break;
        }
        const auto field = [&](int index) {
            return data_.word(static_cast<std::uint16_t>(record + (2 * index)));
        };
        data_.set(var::kPassengerScreenX, actor, field(0));
        data_.set(var::kPassengerDrawnX, actor, field(0));
        data_.set(var::kPassengerX, actor, field(0) << 5U);
        data_.set(var::kPassengerScreenY, actor, field(1));
        data_.set(var::kPassengerDrawnY, actor, field(1));
        data_.set(var::kPassengerY, actor, field(1) << 5U);
        data_.set(var::kActorDelayReload, actor, field(2));
        data_.set(var::kActorHandler, actor, field(3));
        data_.set(var::kActorScript, actor, record + 8);
        data_.set(var::kActorDelay, actor, 1);
        data_.set(var::kActorLoopCount, actor, 0);
        data_.set(var::kActorFinished, actor, 0);
    }
    data_.set(var::kActorScript, actor, kNoScript);
    resetCharacterSprites();
    stepActors();
}

void Game::stepActors()
{
    for (int actor = 0;; ++actor) {
        auto script = static_cast<std::uint16_t>(data_.get(var::kActorScript, actor));
        if (script == kNoScript) {
            return;
        }
        const auto delay = static_cast<std::uint16_t>(data_.get(var::kActorDelay, actor) - 1);
        data_.set(var::kActorDelay, actor, delay);
        if (delay != 0) {
            continue;
        }
        data_.set(var::kActorDelay, actor, data_.get(var::kActorDelayReload, actor));

        while (true) {
            const std::uint16_t command = data_.word(script);
            script = static_cast<std::uint16_t>(script + 2);
            if (static_cast<std::int16_t>(command) >= 0) {
                data_.set(var::kPassengerSprite, actor, command);
                const auto dx = static_cast<std::int8_t>(data_.byte(script));
                const auto dy = static_cast<std::int8_t>(data_.byte(static_cast<std::uint16_t>(script + 1)));
                script = static_cast<std::uint16_t>(script + 2);
                data_.set(var::kPassengerX, actor, data_.get(var::kPassengerX, actor) + (dx * 4));
                data_.set(var::kPassengerY, actor, data_.get(var::kPassengerY, actor) + (dy * 4));
                runActorHandler(actor, static_cast<std::uint16_t>(data_.get(var::kActorHandler, actor)));
                break;
            }
            if (command != kLoopEnd) {
                data_.set(var::kActorLoopCount, actor, command);
                data_.set(var::kActorLoopStart, actor, script);
                continue;
            }
            const auto count = static_cast<std::uint16_t>(data_.get(var::kActorLoopCount, actor));
            if (count == 0) {
                // The script has ended: it stays on its end mark.
                script = static_cast<std::uint16_t>(script - 2);
                data_.set(var::kActorFinished, actor, -1);
                break;
            }
            const auto counted = static_cast<std::uint16_t>(count + 1);
            data_.set(var::kActorLoopCount, actor, counted);
            if (counted != 0) {
                script = static_cast<std::uint16_t>(data_.get(var::kActorLoopStart, actor));
            }
        }
        data_.set(var::kActorScript, actor, script);
    }
}

void Game::runActorHandler(int actor, std::uint16_t handler)
{
    switch (handler) {
    case kNoHandler:
        return;
    case kFadeWhenInvisible:
        if (data_.get(var::kPassengerSprite, actor) == var::kInvisibleSprite) {
            data_.set(var::kFadeStep, -kFadeSpeed);
            data_.set(var::kFadeLevel, data_.get(var::kFadeLevel) - kFadeSpeed);
        }
        return;
    case kClipSign:
        clipSign(actor);
        return;
    default:
        reportUnknownRoutine("actor", handler);
        return;
    }
}

void Game::clipSign(int actor)
{
    // 113b:099f: the sign is narrowed to end at the right screen edge (at most its full 0x7f columns). Only the
    // low byte is stored: off the right edge the width wraps to a large value, but the sign is not drawn there.
    const int width = 0x140 - (data_.get(var::kPassengerX, actor) >> 5);
    video_.setSpriteWidth(kSignSprite, static_cast<std::uint8_t>(std::min(width, 0x7F) & 0xFF));
}

void Game::eraseActors()
{
    for (int actor = 0; static_cast<std::uint16_t>(data_.get(var::kActorScript, actor)) != kNoScript; ++actor) {
        const int drawn = data_.get(var::kPassengerDrawnSprite, actor);
        data_.set(var::kPassengerDrawnSprite, actor, data_.get(var::kPassengerSprite, actor));
        if (drawn != -1 && drawn != var::kInvisibleSprite) {
            video_.restoreSpriteRect(drawn, data_.get(var::kPassengerDrawnX, actor),
                                     data_.get(var::kPassengerDrawnY, actor));
        }
    }
}

void Game::drawActors()
{
    for (int actor = 0; static_cast<std::uint16_t>(data_.get(var::kActorScript, actor)) != kNoScript; ++actor) {
        const int sprite = data_.get(var::kPassengerSprite, actor);
        if (sprite == var::kInvisibleSprite) {
            continue;
        }
        data_.set(var::kPassengerDrawnX, actor, data_.get(var::kPassengerScreenX, actor));
        data_.set(var::kPassengerDrawnY, actor, data_.get(var::kPassengerScreenY, actor));
        const int x = data_.get(var::kPassengerX, actor) >> 5;
        const int y = data_.get(var::kPassengerY, actor) >> 5;
        data_.set(var::kPassengerScreenX, actor, x);
        data_.set(var::kPassengerScreenY, actor, y);
        if (x < 0x140 && y < 0xC0) {
            video_.drawSprite(sprite, x, y);
        }
    }
}

bool Game::actorsFinished() const
{
    for (int actor = 0; static_cast<std::uint16_t>(data_.get(var::kActorScript, actor)) != kNoScript; ++actor) {
        if (data_.get(var::kActorFinished, actor) == 0) {
            return false;
        }
    }
    return true;
}

bool Game::joystickButtonPressed()
{
    // TODO(joystick): 113b:51ec reads the game port buttons; without joysticks none is pressed.
    return false;
}

} // namespace ugh
