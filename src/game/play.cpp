// Playing a game: level sequence, level start and the per-frame loop (113b:0c07 and helpers).

#include "data/layout.hpp"
#include "game/game.hpp"
#include "game/scancode.hpp"
#include "game/text.hpp"
#include "game/vars.hpp"

#include <algorithm>

namespace ugh {
namespace {

namespace ds = layout::ds;

constexpr std::uint8_t kPauseKey = 0x19;           // P
constexpr std::uint8_t kPauseKeyRelease = 0x99;
constexpr int kFuelAtStart = 0x5A3B;
constexpr int kTileWidth = 16;
constexpr int kTileHeight = 12;
constexpr int kLevelTextWaterline = 0xAF;
constexpr int kMaxWater = 0x17E0;

/// 113b:3d4d: Amiga (256-line) y to PC (192-line) y: y - y / 4 (arithmetic shift).
int amigaToPcY(int y)
{
    const auto value = static_cast<std::int16_t>(y);
    return static_cast<std::int16_t>(value - (value >> 2));
}

/// 16-bit arithmetic shift right by 5 (position * 32 to pixels).
int toPixels(int fixedPoint)
{
    return static_cast<std::int16_t>(fixedPoint) >> 5;
}

} // namespace

int Game::passwordLevel() const
{
    // The typed password (20 characters, blank padded) must equal one list entry exactly.
    const int players = data_.get(var::kPlayerCount);
    auto source = static_cast<std::uint16_t>(data_.word(static_cast<std::uint16_t>(ds::kPasswordLists + (2 * (players - 1)))));
    int level = 0;
    while (true) {
        int matched = 0;
        int mismatches = 0;
        while (true) {
            const std::uint8_t character = data_.byte(source++);
            if (character == 0xFF) {
                return 0;
            }
            if (character == 0) {
                break;
            }
            ++matched;
            if (character != data_.byte(static_cast<std::uint16_t>(ds::kPasswordLine + matched - 1))) {
                --mismatches;
            }
        }
        if (mismatches == 0) {
            bool trailingBlank = true;
            for (int i = matched; std::cmp_less(i, ds::kPasswordLength); ++i) {
                if (data_.byte(static_cast<std::uint16_t>(ds::kPasswordLine + i)) != ' ') {
                    trailingBlank = false;
                    break;
                }
            }
            if (trailingBlank) {
                return level;
            }
        }
        ++level;
    }
}

Task<> Game::play()
{
    data_.set(var::kLevel, passwordLevel());
    co_await openingFlight();
    co_await stopMusic(0x11);
    data_.set(var::kLives, 3);       // 113b:3961
    data_.set(var::kMultiplier, 1);
    data_.set(var::kScoreLow, 0);
    data_.set(var::kScoreHigh, 0);
    co_await video_.blackOut();

    while (true) {
        co_await startLevel();
        playMusic(0x4444); // in-game tune

        while (true) {
            const int fade = data_.get(var::kFadeLevel);
            if (fade < 0) {
                break;
            }
            if (fade <= 0x100) {
                co_await video_.fadeStep(static_cast<unsigned>(fade));
                data_.set(var::kFadeLevel, fade + data_.get(var::kFadeStep));
            } else {
                co_await clock_->nextFrame();
            }

            updateStatusBar();
            const int players = data_.get(var::kPlayerCount);
            for (int player = 0; player < players; ++player) {
                erasePlayer(player);
            }
            erasePassengers();
            eraseEnemies();
            eraseItems();
            if (data_.get(var::kRain) != 0) {
                eraseRain();
            }
            updateWaterLevel();

            // 113b:0fe8 also samples the joysticks and the control keys of both players.
            std::uint8_t key = keyboard_->poll().code;
            if (key == kPauseKey) {
                co_await pause();
                key = keyboard_->poll().code;
            }
            if (key == static_cast<std::uint8_t>(Scancode::Escape)) {
                data_.set(var::kLives, 0);
                if (data_.get(var::kFadeStep) >= 0) {
                    data_.set(var::kFadeStep, -2);
                    data_.set(var::kFadeLevel, data_.get(var::kFadeLevel) - 2);
                }
            }

            // Players stay still while the level fades in.
            if (data_.get(var::kFadeLevel) > 0xC0 || data_.get(var::kFadeStep) <= 0) {
                for (int player = 0; player < players; ++player) {
                    updatePlayer(player);
                }
            }
            updatePassengers();
            updateEnemies();
            updateItems();

            drawPassengers();
            drawEnemies(true);
            for (int player = 0; player < players; ++player) {
                drawPlayer(player);
            }
            drawEnemies(false);
            drawItems();
            drawPassengerBubbles();
            if (data_.get(var::kRain) != 0) {
                updateRain();
                drawRain();
            }
            drawWaterSurfaceFrame();
            video_.flip();
        }

        // 113b:0fa7: level finished or attempt lost.
        co_await stopMusic(0x11);
        if ((data_.get(var::kLevelCompleted) & 0x80U) != 0) {
            const int next = data_.get(var::kLevel) + 1;
            data_.set(var::kLevel, next);
            const int players = data_.get(var::kPlayerCount);
            if (next >= data_.get(var::kLevelCount, players - 1)) {
                co_return;
            }
            continue;
        }
        const auto lives = static_cast<std::int8_t>(data_.get(var::kLives) - 1);
        data_.set(var::kLives, lives);
        if (lives <= 0) {
            co_return;
        }
        data_.set(var::kMultiplier, 1);
    }
}

Task<> Game::startLevel()
{
    data_.clear(var::kLevelStateStart, var::kLevelStateBytes);
    data_.set(var::kFuel, kFuelAtStart);
    data_.set(var::kUnknown27c8, -1);
    data_.set(var::kFadeStep, 2);

    // Make every status field differ from its value so the first frame draws all of them.
    const auto offsetShown = [](int value) {
        const auto shown = static_cast<std::uint8_t>(value + 11);
        return shown >= 100 ? shown - 100 : shown;
    };
    data_.set(var::kLivesShown, offsetShown(data_.get(var::kLives)));
    data_.set(var::kMultiplierShown, offsetShown(data_.get(var::kMultiplier)));
    data_.set(var::kDeliveriesShown, 99);
    data_.set(var::kTipShown, 9999);
    std::uint32_t scoreShown = (static_cast<std::uint32_t>(static_cast<std::uint16_t>(data_.get(var::kScoreHigh)))
                                << 16U) +
                               static_cast<std::uint16_t>(data_.get(var::kScoreLow)) + 111111U;
    if (scoreShown > 1000000U) {
        scoreShown -= 1000000U;
    }
    data_.set(var::kScoreShownLow, static_cast<int>(scoreShown & 0xFFFFU));
    data_.set(var::kScoreShownHigh, static_cast<int>(scoreShown >> 16U));

    data_.set(var::kStatusY, 0xC2);
    if (data_.get(var::kPlayerCount) == 2) {
        data_.set(var::kLivesX, 0x21);
        data_.set(var::kScoreX, 0x72);
        data_.set(var::kDeliveriesX, 0xA9);
        data_.set(var::kPlayer1Icon2X, 0xC7);
        data_.set(var::kPlayer1IconX, 0xD7);
        data_.set(var::kTipX, 0xE8);
        data_.set(var::kMultiplierX, 0x131);
        data_.set(var::kPlayer2IconX, 0x102);
        data_.set(var::kPlayer2Icon2X, 0x112);
    } else {
        data_.set(var::kLivesX, 0x21);
        data_.set(var::kScoreX, 0x76);
        data_.set(var::kDeliveriesX, 0xB1);
        data_.set(var::kPlayer1Icon2X, 0xD0);
        data_.set(var::kPlayer1IconX, 0xE0);
        data_.set(var::kTipX, 0xF4);
        data_.set(var::kMultiplierX, 0x122);
    }

    loadLevel();
    co_await levelIntro();
    video_.clearVram();
    co_await video_.blackOut();
    video_.useGamePalette();
    drawLevelMap();
    video_.saveBackground();
    video_.flip();
    video_.restoreScreen();
    drawStatusFrame();
    updateEnemies();
    updatePassengers();
    resetCharacterSprites();
}

void Game::loadLevel()
{
    const int players = data_.get(var::kPlayerCount);
    const auto table = static_cast<std::uint16_t>(data_.get(var::kLevelTables, players - 1));
    const auto record = data_.word(static_cast<std::uint16_t>(table + (2 * data_.get(var::kLevel))));
    for (std::size_t i = 0; i < var::kLevelRecordBytes; ++i) {
        data_.setByte(static_cast<std::uint16_t>(var::kLevelRecord + i),
                      data_.byte(static_cast<std::uint16_t>(record + i)));
    }

    const auto mapOffset = static_cast<std::size_t>(static_cast<std::uint16_t>(data_.get(var::kMapOffset)));
    constexpr auto kMapBytes = static_cast<std::size_t>(var::kMapColumns) * var::kMapRows;
    for (std::size_t i = 0; i < kMapBytes; ++i) {
        const std::size_t source = mapOffset + i;
        data_.setByte(static_cast<std::uint16_t>(var::kLevelMap + i), source < levelMaps_.size() ? levelMaps_[source] : 0);
    }

    struct Start {
        WordVar x;
        WordVar y;
    };
    constexpr std::array<Start, 2> kStarts{{{.x = var::kPlayer1StartX, .y = var::kPlayer1StartY},
                                            {.x = var::kPlayer2StartX, .y = var::kPlayer2StartY}}};
    for (int player = 0; player < 2; ++player) {
        const Start& start = kStarts.at(static_cast<std::size_t>(player));
        const int x = data_.get(start.x) - 0x200;
        data_.set(var::kPlayerX, player, x);
        data_.set(var::kPlayerScreenX, player, toPixels(x));
        data_.set(var::kPlayerDrawnX, player, toPixels(x));
        const int y = amigaToPcY(data_.get(start.y)) - 0x140;
        data_.set(var::kPlayerY, player, y);
        data_.set(var::kPlayerScreenY, player, toPixels(y));
        data_.set(var::kPlayerDrawnY, player, toPixels(y));

        data_.set(var::kLandedPlatform, player, -1);
        data_.set(var::kRotorTimer, player, -1);
        data_.set(var::kPlayerIconShown, player, -1);
        data_.set(var::kPlayerIcon, player, 0);
        data_.set(var::kPlayerIcon2, player, 0);
        data_.set(var::kPlayerTip, player, 0);
        data_.set(var::kVelocityX, player, 0);
        data_.set(var::kVelocityY, player, 0);
    }
    data_.set(var::kPlayerSprite, 0, 0xDA);
    data_.set(var::kPlayerSprite, 1, 0xE0);
    data_.set(var::kTipRefresh, 0xFF);

    int water = amigaToPcY(data_.get(var::kWater));
    if (static_cast<std::uint16_t>(water) > kMaxWater) {
        water = kMaxWater;
    }
    data_.set(var::kWater, water);
    setWaterline(toPixels(water));
    data_.set(var::kWaterlineDrawn, toPixels(water));
    loadPlatforms();
    loadPassengers();
    loadEnemies();
    loadRain();
}

Task<> Game::levelIntro()
{
    video_.clearVram();
    const int waterline = data_.get(var::kWaterline);
    setWaterline(kLevelTextWaterline);

    const int levelNumber = data_.get(var::kLevel) + 1;
    data_.setByte(static_cast<std::uint16_t>(ds::kLevelTitle + 7), static_cast<std::uint8_t>('0' + (levelNumber / 10)));
    data_.setByte(static_cast<std::uint16_t>(ds::kLevelTitle + 8), static_cast<std::uint8_t>('0' + (levelNumber % 10)));
    int y = drawTextLine(video_, data_, ds::kLevelTitle, 0x20).y + 0x20;
    drawText(video_, data_, static_cast<std::uint16_t>(data_.get(var::kLevelName)), y);

    y = drawTextLine(video_, data_, ds::kLevelPasswordTitle, 0xA0).y;
    const int players = data_.get(var::kPlayerCount);
    auto password = data_.word(static_cast<std::uint16_t>(ds::kPasswordLists + (2 * (players - 1))));
    for (int skip = data_.get(var::kLevel); skip > 0; --skip) {
        while (data_.byte(password++) != 0) {
        }
    }
    drawTextLine(video_, data_, password, y);
    video_.flip();

    co_await stopMusic(0x11);
    static_cast<void>(playSound(0x4629, 0xE1, 1)); // level jingle
    co_await video_.fadeIn();
    co_await waitForKey();
    setWaterline(waterline);
    co_await video_.fadeOut();
}

void Game::drawLevelMap()
{
    for (int row = 0; row < var::kMapRows; ++row) {
        for (int column = 0; column < var::kMapColumns; ++column) {
            const std::uint8_t tile =
                data_.byte(static_cast<std::uint16_t>(var::kLevelMap + (row * var::kMapColumns) + column));
            video_.drawSprite(tile, column * kTileWidth, row * kTileHeight);
        }
    }
}

void Game::erasePlayer(int player)
{
    const int x = data_.get(var::kPlayerDrawnX, player);
    const int y = data_.get(var::kPlayerDrawnY, player);
    if (x > -0x20 && x < 0x140 && y > -0x14 && y < 0xC0) {
        video_.restoreSpriteRect(data_.get(var::kPlayerDrawnSprite, player), x, y);
    }
    data_.set(var::kPlayerDrawnSprite, player, data_.get(var::kPlayerSprite, player));
    data_.set(var::kPlayerDrawnX, player, data_.get(var::kPlayerScreenX, player));
    data_.set(var::kPlayerDrawnY, player, data_.get(var::kPlayerScreenY, player));
}

void Game::drawPlayer(int player)
{
    // The rotor turns one frame each time the timer, decremented by the rotor speed (max 90), wraps.
    const int speed = std::min<int>(data_.get(var::kRotorSpeed, player), 0x5A);
    int timer = data_.get(var::kRotorTimer, player) - speed;
    data_.set(var::kRotorTimer, player, timer);
    timer = data_.get(var::kRotorTimer, player);
    if (timer < 0) {
        data_.set(var::kRotorTimer, player, timer + 0xC8);
        int sprite = data_.get(var::kPlayerSprite, player) + 1;
        if (sprite >= data_.get(var::kRotorFirstSprite, player + 1)) {
            sprite = data_.get(var::kRotorFirstSprite, player);
        }
        data_.set(var::kPlayerSprite, player, sprite);
    }

    const int x = data_.get(var::kPlayerScreenX, player);
    const int y = data_.get(var::kPlayerScreenY, player);
    if (y > -0x14 && y < 0xC0 && x > -0x20 && x < 0x140) {
        video_.drawSprite(data_.get(var::kPlayerSprite, player), x, y);
    }
}

Task<> Game::pause()
{
    // The original polls in a tight loop; waiting for frames in between changes nothing visible.
    // Wait for P to be released, then for any key press and its release.
    while (keyboard_->poll().code != kPauseKeyRelease) {
        co_await clock_->nextFrame();
    }
    while ((keyboard_->poll().code & 0x80U) != 0) {
        co_await clock_->nextFrame();
    }
    while ((keyboard_->poll().code & 0x80U) == 0) {
        co_await clock_->nextFrame();
    }
}

void Game::setWaterline(int y)
{
    data_.set(var::kWaterline, y);
    video_.setWaterline(data_.get(var::kWaterline));
}

} // namespace ugh
