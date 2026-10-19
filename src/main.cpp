#include "data/data_segment.hpp"
#include "data/exe_image.hpp"
#include "data/graphics.hpp"
#include "data/layout.hpp"
#include "game/assets.hpp"
#include "game/game.hpp"
#include "game/keyboard.hpp"
#include "platform/game_exe.hpp"
#include "platform/keyboard.hpp"
#include "platform/screen.hpp"
#include "platform/sdl_handles.hpp"
#include "runtime/task.hpp"
#include "runtime/timing.hpp"
#include "sound/opl_chip.hpp"
#include "vga/vga.hpp"

#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <optional>
#include <print>
#include <span>
#include <vector>

namespace {

/// After a stall (window dragged, debugger) do not try to catch up more than this many frames.
constexpr std::uint64_t kMaxCatchUpFrames = 4;

/// Audio queued ahead of the device: enough to ride out uneven frame pacing, small enough to keep effects
/// in sync with the picture.
constexpr int kAudioLowWaterFrames = ugh::OplChip::kSampleRate / 50;   // 20 ms
constexpr int kAudioCushionFrames = ugh::OplChip::kSampleRate / 15;    // ~67 ms

/// SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK without its C-style cast.
constexpr SDL_AudioDeviceID kDefaultPlaybackDevice = 0xFFFFFFFFU;

/// Opens the default playback device for the OPL's 16-bit stereo output; null when there is no audio.
ugh::sdl::AudioStreamPtr openAudio()
{
    const SDL_AudioSpec spec{.format = SDL_AUDIO_S16, .channels = 2, .freq = ugh::OplChip::kSampleRate};
    ugh::sdl::AudioStreamPtr stream(
        SDL_OpenAudioDeviceStream(kDefaultPlaybackDevice, &spec, nullptr, nullptr));
    if (!stream) {
        std::println(stderr, "ugh: no sound: {}", SDL_GetError());
        return stream;
    }
    SDL_ResumeAudioStreamDevice(stream.get());
    return stream;
}

/// Tops the queue up with silence when it ran (nearly) dry, e.g. after a stall.
void keepAudioCushion(SDL_AudioStream* stream)
{
    constexpr int kBytesPerFrame = 4;
    if (SDL_GetAudioStreamQueued(stream) >= kAudioLowWaterFrames * kBytesPerFrame) {
        return;
    }
    const std::vector<std::int16_t> silence(static_cast<std::size_t>(kAudioCushionFrames) * 2);
    SDL_PutAudioStreamData(stream, silence.data(), static_cast<int>(silence.size() * sizeof(std::int16_t)));
}

int run(std::span<char*> args)
{
    const ugh::sdl::Context sdl(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    if (!sdl.ok()) {
        std::println(stderr, "ugh: SDL_Init: {}", SDL_GetError());
        return 1;
    }

    // The port's own data folder: a copy of UGH.EXE and UGH!.HI live here, never next to the original files.
    std::optional<std::filesystem::path> dataDirectory;
    if (const ugh::sdl::SdlStringPtr prefPath{SDL_GetPrefPath("Play Byte", "UGH")}) {
        dataDirectory = std::filesystem::path(prefPath.get());
    } else {
        std::println(stderr, "ugh: no data folder, high scores will not be saved: {}", SDL_GetError());
    }

    const std::optional<std::filesystem::path> commandLine =
        args.size() > 1 ? std::optional(std::filesystem::path(args[1])) : std::nullopt;
    auto exeFile = ugh::findGameExe(commandLine, dataDirectory);
    if (!exeFile) {
        std::println(stderr, "ugh: {}", exeFile.error().message);
        return 1;
    }
    if (!*exeFile) {
        return 0; // the player cancelled the file dialog
    }
    auto assets = ugh::ExeImage::fromFileBytes(**exeFile).and_then(ugh::loadGameAssets);
    if (!assets) {
        std::println(stderr, "ugh: {}", assets.error().message);
        return 1;
    }

    auto screen = ugh::Screen::create("UGH!");
    if (!screen) {
        std::println(stderr, "ugh: {}", screen.error().message);
        return 1;
    }

    ugh::Vga vga;
    ugh::Keyboard keyboard;
    ugh::FrameClock clock;
    ugh::Game game(std::move(*assets), vga, keyboard, clock);
    if (dataDirectory) {
        game.loadHighScores(*dataDirectory);
    }

    const ugh::sdl::AudioStreamPtr audio = openAudio();
    ugh::SampleClock audioClock(ugh::OplChip::kSampleRate);
    std::vector<std::int16_t> samples;

    ugh::Task<> program = game.run();
    program.start();

    std::vector<std::uint8_t> frame(static_cast<std::size_t>(ugh::Vga::kScreenWidth) * ugh::Vga::kScreenHeight);
    std::uint64_t lastTick = SDL_GetTicksNS();
    std::uint64_t pending = 0;

    while (!program.done()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                return 0;
            }
            if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) && !event.key.repeat) {
                if (const auto key = ugh::toDosKey(event.key.scancode)) {
                    keyboard.keyEvent(key->makeCode, key->extended, event.type == SDL_EVENT_KEY_DOWN, key->ascii);
                }
            }
        }

        const std::uint64_t now = SDL_GetTicksNS();
        pending = std::min(pending + (now - lastTick), ugh::kFrameNs * kMaxCatchUpFrames);
        lastTick = now;
        while (pending >= ugh::kFrameNs && !program.done()) {
            pending -= ugh::kFrameNs;
            game.serviceKeyboard();
            clock.tick();
            const std::size_t frames = audioClock.next();
            samples.resize(audio ? frames * 2 : 0);
            game.sound().render(frames, samples);
            if (audio) {
                keepAudioCushion(audio.get());
                SDL_PutAudioStreamData(audio.get(), samples.data(),
                                       static_cast<int>(samples.size() * sizeof(std::int16_t)));
            }
        }

        vga.scanout(frame);
        if (auto shown = screen->present(frame, vga.dac()); !shown) {
            std::println(stderr, "ugh: {}", shown.error().message);
            return 1;
        }
    }
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        return run(std::span(argv, static_cast<std::size_t>(argc)));
    } catch (const std::exception& e) {
        static_cast<void>(std::fputs("ugh: fatal: ", stderr));
        static_cast<void>(std::fputs(e.what(), stderr));
        static_cast<void>(std::fputs("\n", stderr));
        return 1;
    } catch (...) {
        return 1;
    }
}
