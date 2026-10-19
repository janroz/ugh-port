#include "game/game.hpp"

#include "data/layout.hpp"
#include "game/scancode.hpp"
#include "game/text.hpp"
#include "game/vars.hpp"

#include <algorithm>
#include <fstream>
#include <string>

namespace ugh {
namespace {

namespace ds = layout::ds;

constexpr int kMenuIdleFrames = 0x834;
constexpr int kSettleFrames = 0x28;
constexpr int kMenuWaterline = 0xAF;
constexpr int kNoWaterline = 0xC8;

constexpr std::uint8_t code(Scancode scancode)
{
    return static_cast<std::uint8_t>(scancode);
}

} // namespace

Game::Game(GameAssets assets, Vga& vga, Keyboard& keyboard, FrameClock& clock)
    : video_(vga, clock, std::move(assets.sprites), assets.gamePalette), keyboard_(&keyboard), clock_(&clock),
      data_(std::move(assets.data)), introPictures_(std::move(assets.introPictures)),
      levelMaps_(std::move(assets.levelMaps)), waterSurface_(std::move(assets.waterSurface)),
      randomState_(assets.randomState),
      sound_(std::move(assets.soundData), std::move(assets.loadModule)), joystickPresent_{false, false}
{
}

void Game::loadHighScores(const std::filesystem::path& directory)
{
    std::string name;
    for (std::uint16_t offset = ds::kHighScoreFileName; data_.byte(offset) != 0; ++offset) {
        name.push_back(static_cast<char>(data_.byte(offset)));
    }
    highScoreFile_ = directory / name;

    std::ifstream file(*highScoreFile_, std::ios::binary);
    if (!file) {
        return;
    }
    std::array<char, ds::kHighScoreTableBytes> buffer{};
    file.read(buffer.data(), buffer.size());
    const auto bytesRead = static_cast<std::size_t>(file.gcount());

    auto table = data_.window(ds::kHighScoreTable, ds::kHighScoreTableBytes);
    if (table) {
        std::ranges::transform(std::span(buffer).first(bytesRead), table->begin(),
                               [](char c) { return static_cast<std::uint8_t>(c); });
    }
    // The whole table is decoded even after a short read, as in the original.
    toggleHighScoreCipher();
}

void Game::toggleHighScoreCipher()
{
    if (auto table = data_.window(ds::kHighScoreTable, ds::kHighScoreTableBytes)) {
        for (std::uint8_t& byte : *table) {
            byte ^= 0xFFU;
        }
    }
}

void Game::saveHighScores()
{
    // The original never closes the file (DOS does at exit) and ignores every error.
    if (!highScoreFile_) {
        return;
    }
    const std::filesystem::path& path = *highScoreFile_;
    toggleHighScoreCipher();
    if (auto table = data_.window(ds::kHighScoreTable, ds::kHighScoreTableBytes)) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        std::string bytes(table->begin(), table->end());
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    toggleHighScoreCipher();
}

void Game::playMusic(std::uint16_t segment)
{
    static_cast<void>(sound_.playMusic(segment, 0, 0, 0xA5));
}

int Game::playSound(std::uint16_t segment, int volume, int loops)
{
    return sound_.playSound(segment, 0, volume, loops);
}

Task<> Game::stopMusic(int fadeTicks)
{
    // The original busy-waits here while its timer interrupt runs the fade.
    if (sound_.beginStopMusic(fadeTicks)) {
        while (sound_.fading()) {
            co_await clock_->nextFrame();
        }
        sound_.finishStopMusic();
    }
}

Task<> Game::run()
{
    sound_.initialize(); // 113b:4f43
    co_await video_.setMode320();
    if (co_await intro() == Outcome::Quit) {
        co_await stopMusic(0x11);
        sound_.shutdown();
        co_return;
    }
    co_await video_.setMode384();
    co_await mainMenu();
    co_await stopMusic(0x11); // 113b:4f4e
    sound_.shutdown();
}

Task<> Game::waitFrames(int count)
{
    for (int i = 0; i < count; ++i) {
        co_await clock_->nextFrame();
    }
}

Task<Game::HoldResult> Game::holdForKey(int frames)
{
    static_cast<void>(keyboard_->poll());
    for (int remaining = frames; remaining > 0; --remaining) {
        const Keyboard::Poll key = keyboard_->poll();
        if (key.changed) {
            co_return HoldResult{.code = key.code, .keyPressed = true};
        }
        co_await clock_->nextFrame();
    }
    co_return HoldResult{.code = code(Scancode::Space), .keyPressed = false};
}

Task<> Game::waitForKey()
{
    static_cast<void>(keyboard_->poll());
    while (true) {
        co_await clock_->nextFrame();
        if (keyboard_->poll().changed) {
            co_return;
        }
    }
}

Task<Game::Outcome> Game::introPicture(std::size_t index, int holdFrames)
{
    video_.showPicture(introPictures_.at(index));
    video_.flip();
    co_await video_.fadeIn();
    const HoldResult held = co_await holdForKey(holdFrames);
    co_await video_.fadeOut();

    switch (held.code & 0x7FU) {
    case code(Scancode::Escape):
        co_return Outcome::Menu;
    case code(Scancode::Q):
        co_return Outcome::Quit;
    default:
        co_return Outcome::Continue;
    }
}

Task<Game::Outcome> Game::intro()
{
    for (std::size_t i = 0; i < layout::kIntroPictures.size(); ++i) {
        const Outcome outcome = co_await introPicture(i, layout::kIntroPictures.at(i).holdFrames);
        if (outcome != Outcome::Continue) {
            co_return outcome;
        }
    }
    co_return Outcome::Continue;
}

Task<> Game::mainMenu()
{
    while (true) {
        co_await stopMusic(0x11);
        playMusic(0x4637); // title tune
        if (co_await attractSequence() == Outcome::Quit) {
            co_return;
        }
        video_.useGamePalette();

        bool backToAttract = false;
        while (!backToAttract) {
            video_.setWaterline(kMenuWaterline);
            co_await waitFrames(kSettleFrames);
            video_.clearVram();
            drawTitleBackground();
            video_.saveBackground();
            video_.flip();
            video_.restoreScreen();
            drawMenu();
            co_await video_.fadeIn();

            bool redraw = false;
            int idle = kMenuIdleFrames;
            while (!redraw && !backToAttract) {
                co_await clock_->nextFrame();
                if (--idle == 0) {
                    co_await video_.fadeOut();
                    backToAttract = true;
                    break;
                }
                const Keyboard::Poll key = keyboard_->poll();
                if (!key.changed) {
                    continue;
                }
                idle = kMenuIdleFrames;
                switch (key.code) {
                case code(Scancode::C):
                    co_await video_.fadeOut();
                    backToAttract = true;
                    break;
                case code(Scancode::Q):
                    co_await video_.fadeOut();
                    co_return;
                case code(Scancode::F5):
                    co_await video_.fadeOut();
                    co_await controlOptions();
                    redraw = true;
                    break;
                case code(Scancode::F4): {
                    const int players = data_.get(var::kPlayerCount) ^ 3;
                    data_.set(var::kPlayerCount, players);
                    data_.set(var::kPlayerCountTimes2, players * 2);
                    drawMenu();
                    break;
                }
                case code(Scancode::F3): {
                    const int difficulty = data_.get(var::kDifficulty) + 1;
                    data_.set(var::kDifficulty, difficulty < 3 ? difficulty : 0);
                    drawMenu();
                    break;
                }
                case code(Scancode::F2):
                    co_await passwordEntry();
                    drawMenu();
                    break;
                case code(Scancode::F1):
                    co_await video_.fadeOut();
                    co_await startGame();
                    backToAttract = true;
                    break;
                default:
                    break;
                }
            }
        }
    }
}

Task<> Game::startGame()
{
    co_await play();
    video_.clearVram();
    const int players = data_.get(var::kPlayerCount);
    if (data_.get(var::kLevel) < data_.get(var::kLevelCount, players - 1)) {
        video_.setWaterline(kNoWaterline);
        drawText(video_, data_, ds::kGameFailedText, 0x48);
        video_.flip();
        co_await stopMusic(0x11);
        static_cast<void>(playSound(0x426B, 0xE1, 1)); // "failed" tune
        co_await video_.fadeIn();
        static_cast<void>(co_await holdForKey(0x2BC));
        co_await video_.fadeOut();
    } else {
        co_await closingSequence();
    }
    co_await enterHighScore();
}

bool Game::scoreQualifies(int entry) const
{
    const auto scores = static_cast<std::uint16_t>(ds::kHighScoreScores + (entry * data_.byte(ds::kHighScoreStride)));
    for (std::uint16_t i = 0; i < 6; ++i) {
        const std::uint8_t own = data_.byte(static_cast<std::uint16_t>(var::kDigitsNew + i));
        const std::uint8_t listed = data_.byte(static_cast<std::uint16_t>(scores + i));
        if (own != listed) {
            return own > listed;
        }
    }
    return true;
}

void Game::drawNameLine()
{
    video_.restoreRect(0, 0xA0, 0x140, 0x10);
    drawTextLine(video_, data_, ds::kNameEntryLine, 0xA0);
    video_.flip();
}

Task<> Game::enterHighScore()
{
    const auto score = (static_cast<std::uint32_t>(static_cast<std::uint16_t>(data_.get(var::kScoreHigh))) << 16U) |
                       static_cast<std::uint16_t>(data_.get(var::kScoreLow));
    storeSixDigits(data_, var::kDigitsNew, score);
    bool leading = true;
    for (std::uint16_t i = 0; i < 6; ++i) {
        const auto at = static_cast<std::uint16_t>(var::kDigitsNew + i);
        const auto character = static_cast<std::uint8_t>(data_.byte(at) + '0');
        if (character != '0') {
            leading = false;
        }
        data_.setByte(at, leading ? ' ' : character);
    }

    int entry = 0;
    while (entry < ds::kHighScoreEntries && !scoreQualifies(entry)) {
        ++entry;
    }
    if (entry < ds::kHighScoreEntries) {
        const int stride = data_.byte(ds::kHighScoreStride);
        // Lower entries move down one place; only 20 of the 21 bytes are copied.
        // The loop tests at its end, so entry 3 is copied over entry 4 even when the new score takes place 4.
        for (int from = ds::kHighScoreEntries - 2; from >= std::min(entry, ds::kHighScoreEntries - 2); --from) {
            const auto source = static_cast<std::uint16_t>(ds::kHighScoreTable + (from * stride));
            for (std::uint16_t i = 0; i < 20; ++i) {
                data_.setByte(static_cast<std::uint16_t>(source + 0x15 + i),
                              data_.byte(static_cast<std::uint16_t>(source + i)));
            }
        }
        const auto scores = static_cast<std::uint16_t>(ds::kHighScoreScores + (entry * stride));
        for (std::uint16_t i = 0; i < 6; ++i) {
            data_.setByte(static_cast<std::uint16_t>(scores + i),
                          data_.byte(static_cast<std::uint16_t>(var::kDigitsNew + i)));
        }

        co_await stopMusic(0x11);
        playMusic(0x4279); // high-score tune
        keyboard_->setHandlerInstalled(false);
        auto window = data_.window(ds::kNameEntryLine, ds::kNameLength);
        if (!window) {
            keyboard_->setHandlerInstalled(true);
            co_return;
        }
        const std::span<std::uint8_t> name = *window;
        std::ranges::fill(name, ' ');

        video_.setWaterline(0x9F);
        co_await waitFrames(kSettleFrames);
        video_.clearVram();
        drawTitleBackground();
        drawText(video_, data_, ds::kNameEntryText, 0x50);
        video_.saveBackground();
        video_.flip();
        video_.restoreScreen();
        co_await video_.fadeIn();

        std::size_t cursor = 0;
        std::uint8_t blink = 0;
        bool entering = true;
        while (entering) {
            co_await clock_->nextFrame();
            --blink;
            if (cursor < ds::kNameLength) {
                name[cursor] = (blink & 0x10U) != 0 ? '?' : ' ';
            }
            drawNameLine();

            const std::optional<char> typed = keyboard_->readBios();
            if (!typed) {
                continue;
            }
            char character = *typed;
            if (character >= 'a' && character <= 'z') {
                character = static_cast<char>(character - 0x20);
            }
            const bool printable = (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') ||
                                   character == ' ';
            if (character == '\b') {
                if (cursor == 0) {
                    continue;
                }
                if (cursor < ds::kNameLength) {
                    name[cursor] = ' ';
                }
                --cursor;
                name[cursor] = ' ';
                blink = 0;
            } else if (printable) {
                if (cursor < ds::kNameLength) {
                    name[cursor] = static_cast<std::uint8_t>(character);
                    ++cursor;
                    blink = 0;
                }
            } else if (character == '\r') {
                if (cursor < ds::kNameLength) {
                    name[cursor] = ' ';
                    drawNameLine();
                    if (cursor == 0) {
                        std::ranges::copy(std::string_view("INCOGNITUS"), name.begin());
                    }
                }
                entering = false;
            }
        }

        const auto names = static_cast<std::uint16_t>(ds::kHighScoreTable + (entry * stride));
        for (std::size_t i = 0; i < ds::kNameLength; ++i) {
            data_.setByte(static_cast<std::uint16_t>(names + i), name[i]);
        }
        keyboard_->setHandlerInstalled(true);
        co_await video_.fadeOut();
        saveHighScores();
        static_cast<void>(co_await highScores());
    }
    data_.set(var::kScoreLow, 0);
    data_.set(var::kScoreHigh, 0);
}

Task<Game::Outcome> Game::attractSequence()
{
    Outcome outcome = co_await firstDemo();
    if (outcome == Outcome::Continue) {
        outcome = co_await credits();
    }
    if (outcome == Outcome::Continue) {
        outcome = co_await secondDemo();
    }
    if (outcome == Outcome::Continue) {
        outcome = co_await highScores();
    }
    co_return outcome == Outcome::Quit ? Outcome::Quit : Outcome::Menu;
}

Task<Game::Outcome> Game::credits()
{
    video_.setWaterline(kNoWaterline);
    video_.useGamePalette();

    std::uint16_t page = ds::kCreditsText;
    while (true) {
        co_await waitFrames(kSettleFrames);
        video_.clearVram();
        drawTitleBackground();
        video_.saveBackground();
        video_.flip();
        video_.restoreScreen();
        if (data_.byte(page) == 0xFF) {
            co_return Outcome::Continue;
        }

        unsigned lines = 0;
        for (std::uint16_t scan = page;; ++scan) {
            const std::uint8_t character = data_.byte(scan);
            if (character == 0xFD || character == 0x0D) {
                ++lines;
            } else if (character == 0) {
                ++lines;
                break;
            }
        }
        const int y = static_cast<int>(static_cast<std::uint16_t>(0x90 - (lines * 16)) >> 1U) + 0x38;
        page = drawText(video_, data_, page, y).offset;

        video_.flip();
        co_await video_.fadeIn();
        const HoldResult held = co_await holdForKey(0x15E);
        co_await video_.fadeOut();
        if (held.keyPressed) {
            switch (held.code & 0x7FU) {
            case code(Scancode::Q):
                co_return Outcome::Quit;
            case code(Scancode::Escape):
                co_return Outcome::Menu;
            default:
                co_return Outcome::Continue;
            }
        }
    }
}

Task<Game::Outcome> Game::highScores()
{
    video_.setWaterline(kNoWaterline);
    co_await waitFrames(kSettleFrames);
    video_.clearVram();
    drawTitleBackground();
    drawText(video_, data_, ds::kHighScoreText, 0x50);
    video_.flip();
    co_await video_.fadeIn();
    const HoldResult held = co_await holdForKey(0x578);
    co_await video_.fadeOut();
    co_return (held.code & 0x7FU) == code(Scancode::Q) ? Outcome::Quit : Outcome::Menu;
}

Task<> Game::controlOptions()
{
    video_.setWaterline(kNoWaterline);
    co_await waitFrames(kSettleFrames);
    drawControls();
    co_await video_.fadeIn();

    int idle = kMenuIdleFrames;
    while (true) {
        co_await clock_->nextFrame();
        if (--idle == 0) {
            co_await video_.fadeOut();
            co_return;
        }
        const Keyboard::Poll key = keyboard_->poll();
        if (!key.changed) {
            continue;
        }
        idle = kMenuIdleFrames;
        if (key.code == code(Scancode::Escape)) {
            co_await video_.fadeOut();
            co_return;
        }
        if (key.code == code(Scancode::F1) || key.code == code(Scancode::F2)) {
            cycleControl(key.code == code(Scancode::F1) ? 0 : 1);
            drawControls();
        }
    }
}

Task<> Game::passwordEntry()
{
    constexpr std::uint16_t kLineY = 0xB0;
    constexpr std::size_t kLength = ds::kPasswordLength;

    keyboard_->setHandlerInstalled(false);
    video_.restoreScreen();
    drawMenu();

    auto window = data_.window(ds::kPasswordLine, kLength);
    if (!window) {
        keyboard_->setHandlerInstalled(true);
        co_return;
    }
    const std::span<std::uint8_t> line = *window;
    std::ranges::fill(line, ' ');

    std::size_t cursor = 0;
    std::uint8_t blink = 0;
    while (true) {
        co_await clock_->nextFrame();
        --blink;
        if (cursor < kLength) {
            line[cursor] = (blink & 0x10U) != 0 ? '?' : ' ';
        }
        video_.restoreRect(0, kLineY, 320, 16);
        drawTextLine(video_, data_, ds::kPasswordLine, kLineY);
        video_.flip();

        const std::optional<char> typed = keyboard_->readBios();
        if (!typed) {
            continue;
        }
        char character = *typed;
        if (character == '\b') {
            if (cursor == 0) {
                continue;
            }
            if (cursor < kLength) {
                line[cursor] = ' ';
            }
            --cursor;
            line[cursor] = ' ';
            blink = 0;
        } else if (character == '\r') {
            if (cursor < kLength) {
                line[cursor] = ' ';
                if (cursor == 0) {
                    std::ranges::copy(std::string_view("NONE"), line.begin());
                }
            }
            keyboard_->setHandlerInstalled(true);
            co_return;
        } else {
            if (character >= 'a' && character <= 'z') {
                character = static_cast<char>(character - ('a' - 'A'));
            }
            const bool accepted = (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9');
            if (accepted && cursor < kLength) {
                line[cursor++] = static_cast<std::uint8_t>(character);
                blink = 0;
            }
        }
    }
}

void Game::drawTitleBackground()
{
    video_.drawSprite(0xD7, 0x10, 0);
    video_.drawSprite(0xD8, 0xE3, 0);
    video_.drawSprite(0xD6, 0x61, 0);
}

void Game::drawMenu()
{
    video_.restoreScreen();
    data_.copyString(ds::kMenuDifficultySlot,
                     data_.word(static_cast<std::uint16_t>(ds::kDifficultyNames + (2 * data_.get(var::kDifficulty)))));
    data_.copyString(
        ds::kMenuPlayerModeSlot,
        data_.word(static_cast<std::uint16_t>(ds::kPlayerModeNames + (2 * (data_.get(var::kPlayerCount) - 1)))));
    drawText(video_, data_, ds::kMenuText, 0x48);
    video_.flip();
}

void Game::drawControls()
{
    video_.restoreScreen();
    data_.copyString(ds::kControlsPlayer1Slot,
                     data_.word(static_cast<std::uint16_t>(ds::kControlNames + (2 * (data_.get(var::kControl, 0) + 1)))));
    data_.copyString(ds::kControlsPlayer2Slot,
                     data_.word(static_cast<std::uint16_t>(ds::kControlNames + (2 * (data_.get(var::kControl, 1) + 1)))));
    drawText(video_, data_, ds::kControlsText, 0x50);
    video_.flip();
}

void Game::cycleControl(std::size_t player)
{
    // 113b:0620: next joystick that is present, wrapping to the keyboard (-1).
    int control = data_.get(var::kControl, static_cast<int>(player));
    while (true) {
        ++control;
        if (control >= 2) {
            control = -1;
            break;
        }
        if (joystickPresent_.at(static_cast<std::size_t>(control))) {
            break;
        }
    }
    data_.set(var::kControl, static_cast<int>(player), control);
}

} // namespace ugh
