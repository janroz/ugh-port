// Runs the game headless for a scripted number of 70 Hz frames and saves screenshots.
// Usage: ugh-snap <UGH.EXE> <output-dir> <event>...
//   <frame>=shot        save <output-dir>/frame_<frame>.png after that frame
//   <frame>:<make-hex>  press the key with that set-1 make code (released one frame later);
//                       e0xx sends an extended key (arrows: e048 up, e050 down, e04b left, e04d right)
//   <frame>+<make-hex>  press and hold;  <frame>-<make-hex>  release
//   <frame>=text:<WORD>  type WORD and Enter through the BIOS (password entry)
//   <frame>=hover       from then on hold up whenever player 1 sinks (keeps it in the air)
//   trace=<from>-<to>   print player 1 state for those frames

#include "data/data_segment.hpp"
#include "data/exe_image.hpp"
#include "data/layout.hpp"
#include "game/assets.hpp"
#include "game/game.hpp"
#include "game/vars.hpp"
#include "game/keyboard.hpp"
#include "platform/sdl_handles.hpp"
#include "runtime/task.hpp"
#include "runtime/timing.hpp"
#include "vga/vga.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Event {
    enum class Kind : std::uint8_t { Screenshot, Tap, Press, Release, Text, Hover };
    int frame = 0;
    Kind kind = Kind::Screenshot;
    std::uint8_t makeCode = 0;
    bool extended = false;
    std::string text;
};

std::optional<int> parseInt(std::string_view text, int base = 10)
{
    int value = 0;
    const auto [end, error] = std::from_chars(std::to_address(text.begin()), std::to_address(text.end()), value, base);
    if (error != std::errc{} || end != std::to_address(text.end())) {
        return std::nullopt;
    }
    return value;
}

ugh::Result<Event> parseEvent(std::string_view text)
{
    const auto separator = text.find_first_of("=:+-");
    if (separator == std::string_view::npos) {
        return ugh::fail(std::format("bad event '{}'", text));
    }
    Event event;
    const std::string_view frameText = text.substr(0, separator);
    const std::string_view rest = text.substr(separator + 1);
    if (std::from_chars(std::to_address(frameText.begin()), std::to_address(frameText.end()), event.frame).ec != std::errc{}) {
        return ugh::fail(std::format("bad frame in '{}'", text));
    }
    if (text[separator] == '=') {
        if (rest.starts_with("text:")) {
            event.kind = Event::Kind::Text;
            event.text = std::string(rest.substr(5)) + '\r';
            return event;
        }
        if (rest == "hover") {
            event.kind = Event::Kind::Hover;
            return event;
        }
        if (rest != "shot") {
            return ugh::fail(std::format("bad action in '{}'", text));
        }
        return event;
    }
    switch (text[separator]) {
    case ':':
        event.kind = Event::Kind::Tap;
        break;
    case '+':
        event.kind = Event::Kind::Press;
        break;
    default:
        event.kind = Event::Kind::Release;
        break;
    }
    unsigned code = 0;
    if (std::from_chars(std::to_address(rest.begin()), std::to_address(rest.end()), code, 16).ec != std::errc{}) {
        return ugh::fail(std::format("bad make code in '{}'", text));
    }
    event.extended = (code & 0xFF00U) == 0xE000U;
    if ((code & 0xFF00U) != 0 && !event.extended) {
        return ugh::fail(std::format("bad make code in '{}'", text));
    }
    event.makeCode = static_cast<std::uint8_t>(code & 0x7FU);
    return event;
}

ugh::Result<void> saveFrame(const ugh::Vga& vga, const std::filesystem::path& path)
{
    std::vector<std::uint8_t> frame(static_cast<std::size_t>(ugh::Vga::kScreenWidth) * ugh::Vga::kScreenHeight);
    vga.scanout(frame);
    const ugh::sdl::SurfacePtr surface(
        SDL_CreateSurface(ugh::Vga::kScreenWidth, ugh::Vga::kScreenHeight, SDL_PIXELFORMAT_RGBA32));
    if (!surface) {
        return ugh::fail(std::format("SDL_CreateSurface: {}", SDL_GetError()));
    }
    for (int y = 0; y < ugh::Vga::kScreenHeight; ++y) {
        for (int x = 0; x < ugh::Vga::kScreenWidth; ++x) {
            const ugh::Rgb c = vga.dac().rgb(
                frame[(static_cast<std::size_t>(y) * ugh::Vga::kScreenWidth) + static_cast<std::size_t>(x)]);
            SDL_WriteSurfacePixel(surface.get(), x, y, c.r, c.g, c.b, 255);
        }
    }
    if (!SDL_SavePNG(surface.get(), path.string().c_str())) {
        return ugh::fail(std::format("SDL_SavePNG: {}", SDL_GetError()));
    }
    return {};
}

int run(std::span<char*> args)
{
    if (args.size() < 4) {
        std::println(stderr, "usage: ugh-snap <UGH.EXE> <output-dir> <frame>=shot|<frame>:<make-hex>...");
        return 2;
    }
    std::vector<Event> events;
    int traceFrom = -1;
    int traceTo = -1;
    for (const char* arg : args.subspan(3)) {
        const std::string_view text(arg);
        if (text.starts_with("trace=")) {
            const auto range = text.substr(6);
            const auto dash = range.find('-');
            const auto from = parseInt(range.substr(0, dash));
            const auto to = dash == std::string_view::npos ? std::nullopt : parseInt(range.substr(dash + 1));
            if (!from || !to) {
                std::println(stderr, "ugh-snap: bad trace range '{}'", text);
                return 2;
            }
            traceFrom = *from;
            traceTo = *to;
            continue;
        }
        auto event = parseEvent(arg);
        if (!event) {
            std::println(stderr, "ugh-snap: {}", event.error().message);
            return 2;
        }
        events.push_back(*event);
    }
    std::ranges::sort(events, {}, &Event::frame);

    const std::filesystem::path exePath = args[1];
    const std::filesystem::path outDir = args[2];
    auto assets = ugh::ExeImage::load(exePath).and_then(ugh::loadGameAssets);
    if (!assets) {
        std::println(stderr, "ugh-snap: {}", assets.error().message);
        return 1;
    }
    const ugh::sdl::Context sdl(0);
    std::filesystem::create_directories(outDir);

    ugh::Vga vga;
    ugh::Keyboard keyboard;
    ugh::FrameClock clock;
    ugh::Game game(std::move(*assets), vga, keyboard, clock);
    ugh::Task<> program = game.run();
    ugh::SampleClock audioClock(ugh::OplChip::kSampleRate);
    program.start();

    std::vector<Event> releaseNextFrame;
    int hoverFrom = -1;
    bool hoverHeld = false;
    for (const Event& event : events) {
        if (event.kind == Event::Kind::Hover) {
            hoverFrom = event.frame;
        }
    }
    const int lastFrame = std::max(events.empty() ? 0 : events.back().frame, traceTo);
    for (int frame = 0; frame <= lastFrame && !program.done(); ++frame) {
        for (const Event& tapped : releaseNextFrame) {
            keyboard.keyEvent(tapped.makeCode, tapped.extended, false, std::nullopt);
        }
        releaseNextFrame.clear();
        for (const Event& event : events) {
            if (event.frame != frame || event.kind == Event::Kind::Screenshot || event.kind == Event::Kind::Hover) {
                continue;
            }
            if (event.kind == Event::Kind::Text) {
                for (const char character : event.text) {
                    keyboard.keyEvent(0, false, true, character);
                }
                continue;
            }
            keyboard.keyEvent(event.makeCode, event.extended, event.kind != Event::Kind::Release, std::nullopt);
            if (event.kind == Event::Kind::Tap) {
                releaseNextFrame.push_back(event);
            }
        }
        if (hoverFrom >= 0 && frame >= hoverFrom) {
            const bool hold = game.data().get(ugh::var::kVelocityY, 0) > 0;
            if (hold != hoverHeld) {
                keyboard.keyEvent(0x48, true, hold, std::nullopt);
                hoverHeld = hold;
            }
        }
        game.serviceKeyboard();
        clock.tick();
        game.sound().render(audioClock.next(), {});
        if (frame >= traceFrom && frame <= traceTo) {
            namespace var = ugh::var;
            const ugh::DataSegment& data = game.data();
            std::println("{:5} fade={:4} x={:5} y={:5} vx={:5} vy={:5} imp={:4} land={:2} fuel={:5} up={} lives={}",
                         frame, data.get(var::kFadeLevel), data.get(var::kPlayerX, 0), data.get(var::kPlayerY, 0),
                         data.get(var::kVelocityX, 0), data.get(var::kVelocityY, 0), data.get(var::kImpact, 0),
                         data.get(var::kLandedPlatform, 0), data.get(var::kFuel), data.get(var::kUp, 0),
                         data.get(var::kLives));
        }
        for (const Event& event : events) {
            if (event.frame == frame && event.kind == Event::Kind::Screenshot) {
                if (auto saved = saveFrame(vga, outDir / std::format("frame_{:05}.png", frame)); !saved) {
                    std::println(stderr, "ugh-snap: {}", saved.error().message);
                    return 1;
                }
            }
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
        static_cast<void>(std::fputs("ugh-snap: fatal: ", stderr));
        static_cast<void>(std::fputs(e.what(), stderr));
        static_cast<void>(std::fputs("\n", stderr));
        return 1;
    } catch (...) {
        return 1;
    }
}
