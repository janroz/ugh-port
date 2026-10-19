// Scripted games against the original data: menu -> level 1, then copter physics.
// Expected values are regression values of this port; they become reference values once checked against the
// original in DOSBox-X.

#include "check.hpp"
#include "data/exe_image.hpp"
#include "game/assets.hpp"
#include "game/game.hpp"
#include "game/keyboard.hpp"
#include "game/vars.hpp"
#include "runtime/task.hpp"
#include "sound/adlib_driver.hpp"
#include "runtime/timing.hpp"
#include "vga/vga.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ugh::test::Checker;
namespace var = ugh::var;

constexpr std::uint16_t kHighScoreTable = 0x0C86;
constexpr std::uint16_t kHighScoreBytes = 0x6B;
constexpr std::size_t kHighScoreStride = 21;
constexpr std::uint16_t kNameEntryLine = 0x0C6D;
constexpr int kHighScoreFrames = 6000;

struct KeyTap {
    std::uint8_t makeCode;
    bool extended;
};

/// The level title waits for a key, so level 1 runs from this frame on whatever happened before it; times in
/// the level are given relative to it.
constexpr int kTitleDismissed = 1200;

/// Frames at which the scripted player taps keys; ESC skips the intro and the first demo, F1 starts, space
/// skips the opening flight and then dismisses the level title.
std::multimap<int, KeyTap> startLevelOne()
{
    return {{100, {.makeCode = 0x01, .extended = false}},
            {400, {.makeCode = 0x01, .extended = false}},
            {720, {.makeCode = 0x3B, .extended = false}},
            {800, {.makeCode = 0x39, .extended = false}},
            {kTitleDismissed, {.makeCode = 0x39, .extended = false}}};
}

/// Arrow keys of player 1 held by an automatic pilot.
struct Arrows {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
};

using Pilot = std::function<Arrows(int, const ugh::DataSegment&)>;

/// Character typed through the BIOS in a frame, if any (name and password entry).
using Typist = std::function<std::optional<char>(int, const ugh::DataSegment&)>;

/// Runs the game for `frames` frames with the given taps (released one frame later) and calls `observe`
/// after every frame. A pilot, if given, holds player 1's arrow keys from frame to frame; a typist types.
/// With a high-score directory the game loads and saves UGH!.HI there.
void play(const ugh::GameAssets& assets, const std::multimap<int, KeyTap>& taps, int frames,
          const std::function<void(int, const ugh::DataSegment&)>& observe, const Pilot& pilot = {},
          const Typist& typist = {}, const std::filesystem::path& highScoreDirectory = {})
{
    ugh::Vga vga;
    ugh::Keyboard keyboard;
    ugh::FrameClock clock;
    ugh::Game game(assets, vga, keyboard, clock);
    if (!highScoreDirectory.empty()) {
        game.loadHighScores(highScoreDirectory);
    }
    ugh::Task<> program = game.run();
    ugh::SampleClock audioClock(ugh::OplChip::kSampleRate);
    program.start();

    std::vector<KeyTap> release;
    Arrows held;
    const auto hold = [&](bool now, bool& before, std::uint8_t code) {
        if (now != before) {
            keyboard.keyEvent(code, true, now, std::nullopt);
            before = now;
        }
    };
    for (int frame = 0; frame < frames && !program.done(); ++frame) {
        if (pilot) {
            const Arrows wanted = pilot(frame, game.data());
            hold(wanted.up, held.up, 0x48);
            hold(wanted.down, held.down, 0x50);
            hold(wanted.left, held.left, 0x4B);
            hold(wanted.right, held.right, 0x4D);
        }
        for (const KeyTap& tap : release) {
            keyboard.keyEvent(tap.makeCode, tap.extended, false, std::nullopt);
        }
        release.clear();
        if (typist) {
            if (const std::optional<char> typed = typist(frame, game.data())) {
                keyboard.keyEvent(0, false, true, typed);
            }
        }
        const auto [first, last] = taps.equal_range(frame);
        for (auto it = first; it != last; ++it) {
            keyboard.keyEvent(it->second.makeCode, it->second.extended, true, std::nullopt);
            release.push_back(it->second);
        }
        game.serviceKeyboard();
        clock.tick();
        game.sound().render(audioClock.next(), {});
        observe(frame, game.data());
    }
}

void testFreeFallCrashes(Checker& check, const ugh::GameAssets& assets)
{
    int crashFrame = -1;
    int livesAfter = -1;
    // The life is taken after the in-game tune has faded out (113b:0fa7 waits for it).
    play(assets, startLevelOne(), kTitleDismissed + 700, [&](int frame, const ugh::DataSegment& data) {
        if (crashFrame < 0 && data.get(var::kFadeStep) < 0 && frame > kTitleDismissed + 100) {
            crashFrame = frame;
        }
        livesAfter = data.get(var::kLives);
    });
    check(crashFrame == kTitleDismissed + 274, "free fall from the start crashes on the floor");
    check(livesAfter == 2, "the crash costs a life");
}

void testTappedLanding(Checker& check, const ugh::GameAssets& assets)
{
    auto taps = startLevelOne();
    for (int frame = kTitleDismissed + 200; frame <= kTitleDismissed + 330; frame += 3) {
        taps.insert({frame, {.makeCode = 0x48, .extended = true}});
    }
    int landedFrame = -1;
    int platform = -1;
    int impact = -1;
    play(assets, taps, kTitleDismissed + 340, [&](int frame, const ugh::DataSegment& data) {
        // Before the level starts the variable still holds its initial value from the executable (0).
        if (landedFrame < 0 && frame > kTitleDismissed + 100 && data.get(var::kLandedPlatform, 0) >= 0) {
            landedFrame = frame;
            platform = data.get(var::kLandedPlatform, 0);
            impact = data.get(var::kImpact, 0);
        }
    });
    check(landedFrame == kTitleDismissed + 334, "tapping thrust lands the copter");
    check(platform == 2, "on the lower right platform");
    check(impact == 1376, "with a soft impact");
}

/// Flies player 1 to `targetX` (copter centre) above a platform and sets it down gently.
/// The target must keep clear of waiting passengers (a flying copter knocks them into the water) and of
/// platforms above.
Arrows flyTo(const ugh::DataSegment& data, int platform, int targetX)
{
    const int landingY = data.get(var::kPlatformY, platform) - 0x14; // copter top when landed
    const int x = data.get(var::kPlayerScreenX, 0) + 0x10;           // copter centre
    const int y = data.get(var::kPlayerScreenY, 0);
    const int vx = data.get(var::kVelocityX, 0);
    const int vy = data.get(var::kVelocityY, 0);

    Arrows arrows;
    const int dx = targetX - x;
    const bool overPlatform = dx > -6 && dx < 6;
    const int wantedVx = std::clamp(dx * 40, -1200, 1200);
    if (vx < wantedVx - 40) {
        arrows.right = true;
    } else if (vx > wantedVx + 40) {
        arrows.left = true;
    }
    // Cruise above the target until over it, then sink slowly.
    const int cruiseY = std::min(landingY - 24, y);
    const int wantedVy = overPlatform ? 500 : std::clamp((cruiseY - y) * 40, -1500, 600);
    // A landed copter only moves after taking off.
    const int landed = data.get(var::kLandedPlatform, 0);
    arrows.up = vy > wantedVy || (landed >= 0 && landed != platform);
    return arrows;
}

/// Prints player 1 and passenger 0 every 20 frames when UGH_TRACE is set.
void trace(int frame, const ugh::DataSegment& data)
{
    static const bool enabled = std::getenv("UGH_TRACE") != nullptr; // NOLINT(concurrency-mt-unsafe): single-threaded test
    if (!enabled || frame % 20 != 0) {
        return;
    }
    std::println("{:5} copter x={:3} y={:3} vx={:5} vy={:5} landed={:2} icon={} lives={} fade={:3} | passenger "
                 "{:x} at {:x} x={:3} y={:3}",
                 frame, data.get(var::kPlayerScreenX, 0), data.get(var::kPlayerScreenY, 0),
                 data.get(var::kVelocityX, 0), data.get(var::kVelocityY, 0), data.get(var::kLandedPlatform, 0),
                 data.get(var::kPlayerIcon, 0), data.get(var::kLives), data.get(var::kFadeLevel),
                 static_cast<std::uint16_t>(data.get(var::kPassengerType, 0)),
                 static_cast<std::uint16_t>(data.get(var::kPassengerHandler, 0)), data.get(var::kPassengerScreenX, 0),
                 data.get(var::kPassengerScreenY, 0));
}

void testDeliverFirstPassenger(Checker& check, const ugh::GameAssets& assets)
{
    // Level 1: the first passenger waits on platform 0 and wants to go to platform 2.
    int boardedFrame = -1;
    int deliveredFrame = -1;
    int tipAtArrival = -1;
    std::uint32_t score = 0;
    const Pilot pilot = [&](int frame, const ugh::DataSegment& data) {
        if (frame < kTitleDismissed + 180) {
            return Arrows{};
        }
        // Pick up at the left end of platform 0, drop off in the middle of platform 2 (platforms 1 and 0 lie
        // above its left and right parts).
        return boardedFrame < 0 ? flyTo(data, 0, data.get(var::kPlatformLeft, 0) + 18) : flyTo(data, 2, 160);
    };
    play(
        assets, startLevelOne(), kTitleDismissed + 3000,
        [&](int frame, const ugh::DataSegment& data) {
            if (frame < kTitleDismissed + 100) {
                return;
            }
            trace(frame, data);
            if (boardedFrame < 0 && data.get(var::kPlayerIcon, 0) != 0) {
                boardedFrame = frame;
            }
            if (boardedFrame >= 0 && deliveredFrame < 0 && data.get(var::kPlayerIcon, 0) == 0) {
                deliveredFrame = frame;
                tipAtArrival = data.get(var::kPlayerTip, 0);
                score = (static_cast<std::uint32_t>(static_cast<std::uint16_t>(data.get(var::kScoreHigh))) << 16U) |
                        static_cast<std::uint16_t>(data.get(var::kScoreLow));
            }
        },
        pilot);
    check(boardedFrame == kTitleDismissed + 865, "the passenger boards");
    check(deliveredFrame == kTitleDismissed + 1575, "the passenger is delivered");
    // The tip starts at 1100 and sinks by one per frame of the 710-frame ride.
    check(tipAtArrival == 390, "tip at arrival");
    check(score == 390, "score = tip * multiplier 1");
}

int peak(std::span<const std::int16_t> samples)
{
    int loudest = 0;
    for (const std::int16_t sample : samples) {
        loudest = std::max(loudest, std::abs(int{sample}));
    }
    return loudest;
}

/// The high-score table with the scores of entries 2..4 blanked, as UGH!.HI stores it (XOR 0xff).
std::vector<char> blankedHighScoreFile(const ugh::GameAssets& assets)
{
    std::vector<char> file;
    for (std::uint16_t i = 0; i < kHighScoreBytes; ++i) {
        const auto at = static_cast<std::uint16_t>(kHighScoreTable + i);
        const bool blankScore = i >= 2 * kHighScoreStride && i % kHighScoreStride >= 14 &&
                                i % kHighScoreStride < 20;
        file.push_back(static_cast<char>((blankScore ? ' ' : assets.data.byte(at)) ^ 0xFF));
    }
    return file;
}

std::string highScoreName(const std::vector<char>& file, int entry)
{
    std::string name;
    for (int i = 0; i < 12; ++i) {
        name.push_back(static_cast<char>(file.at((static_cast<std::size_t>(entry) * kHighScoreStride) + static_cast<std::size_t>(i)) ^ 0xFF));
    }
    return name;
}

void testHighScoreEntry(Checker& check, const ugh::GameAssets& assets)
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "ugh-play-tests";
    std::filesystem::create_directories(directory);
    const std::filesystem::path path = directory / "UGH!.HI";
    {
        const std::vector<char> file = blankedHighScoreFile(assets);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(file.data(), static_cast<std::streamsize>(file.size()));
    }

    // Free fall until the lives run out; the zero score still equals the blanked entry 2.
    const std::string_view keys = "ab1!\bc\r"; // '!' is ignored, backspace removes '1'
    int entryFrame = -1;
    std::size_t typed = 0;
    const Typist typist = [&](int frame, const ugh::DataSegment& data) -> std::optional<char> {
        if (entryFrame < 0 && data.byte(kNameEntryLine) == '?') {
            entryFrame = frame;
        }
        if (entryFrame < 0 || frame < entryFrame + 10 || frame % 5 != 0 || typed == keys.size()) {
            return std::nullopt;
        }
        return keys.at(typed++);
    };
    auto taps = startLevelOne();
    for (int frame = kTitleDismissed + 600; frame < kTitleDismissed + 5000; frame += 200) {
        taps.insert({frame, {.makeCode = 0x39, .extended = false}}); // dismisses the level title after a crash
    }
    play(assets, taps, kHighScoreFrames, [](int, const ugh::DataSegment&) {}, {}, typist, directory);

    std::ifstream in(path, std::ios::binary);
    const std::vector<char> saved{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    check(entryFrame == kTitleDismissed + 1972, "game over leads to the name entry");
    check(saved.size() == kHighScoreBytes, "UGH!.HI is saved");
    if (saved.size() == kHighScoreBytes) {
        check(highScoreName(saved, 2) == "ABC         ", "the name takes place 2");
        check(highScoreName(saved, 3) == "WILMA       " && highScoreName(saved, 4) == "PEBBLES     ",
              "lower entries move down");
        check(highScoreName(saved, 1) == "BARNEY      ", "higher entries stay");
    }
    std::filesystem::remove_all(directory);
}

void testSound(Checker& check, const ugh::GameAssets& assets)
{
    // Two seconds of each: the title tune, an effect alone, and silence after stopping.
    ugh::AdlibDriver sound(assets.soundData, assets.loadModule);
    sound.initialize();
    constexpr std::size_t kFrames = std::size_t{2} * ugh::OplChip::kSampleRate;
    std::vector<std::int16_t> samples(kFrames * 2);

    check(sound.playMusic(0x4637, 0, 0, 0xA5) == 1, "title tune loads");
    sound.render(kFrames, samples);
    // The game plays its music at volume 0xa5 with channel levels around 200: roughly -22 dB of full scale.
    check(peak(samples) > 200, "title tune is audible");

    check(!sound.beginStopMusic(0), "stopping without a fade needs no wait");
    sound.render(kFrames, samples); // notes may ring on: the library only stops the sequencer here
    const int slot = sound.playSound(0x4629, 0, 0xE1, 1);
    check(slot >= 8 && slot <= 11, "level jingle gets an effect slot");
    sound.render(kFrames, samples);
    check(peak(samples) > 200, "level jingle is audible");

    static_cast<void>(sound.playMusic(0x4444, 0, 0, 0xA5));
    check(sound.beginStopMusic(0x11), "a fade must be waited for");
    int fadeFrames = 0;
    while (sound.fading() && fadeFrames < 1000) {
        sound.render(ugh::OplChip::kSampleRate / 70, {});
        ++fadeFrames;
    }
    sound.finishStopMusic();
    check(fadeFrames > 100 && fadeFrames < 150, "0x11 fade ticks take about 1.7 s");
    sound.render(ugh::OplChip::kSampleRate / 10, {}); // release tails
    sound.render(kFrames, samples);
    check(peak(samples) < 50, "silent after stopping");
}

int run(std::span<char*> args)
{
    if (args.size() != 2) {
        std::println(stderr, "usage: ugh-play-tests <UGH.EXE>");
        return 2;
    }
    auto assets = ugh::ExeImage::load(args[1]).and_then(ugh::loadGameAssets);
    if (!assets) {
        std::println(stderr, "ugh-play-tests: {}", assets.error().message);
        return 1;
    }
    Checker check;
    testFreeFallCrashes(check, *assets);
    testTappedLanding(check, *assets);
    testDeliverFirstPassenger(check, *assets);
    testHighScoreEntry(check, *assets);
    testSound(check, *assets);
    if (check.failures() == 0) {
        std::println("all play tests passed");
    }
    return check.failures() == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        return run(std::span(argv, static_cast<std::size_t>(argc)));
    } catch (const std::exception& e) {
        static_cast<void>(std::fputs("ugh-play-tests: fatal: ", stderr));
        static_cast<void>(std::fputs(e.what(), stderr));
        static_cast<void>(std::fputs("\n", stderr));
        return 1;
    } catch (...) {
        return 1;
    }
}
