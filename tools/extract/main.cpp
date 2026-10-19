// Extracts pictures, sprites and the game palette from UGH.EXE into PNG files.
// Usage: ugh-extract <UGH.EXE> <output-dir>

#include "data/exe_image.hpp"
#include "data/graphics.hpp"
#include "data/layout.hpp"
#include "platform/sdl_handles.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <print>
#include <span>

namespace {

/// RGBA surface; palette index 0 becomes transparent when `transparentZero` is set.
ugh::Result<ugh::sdl::SurfacePtr> toSurface(int width, int height, std::span<const std::uint8_t> pixels,
                                            const ugh::Palette& palette, bool transparentZero)
{
    ugh::sdl::SurfacePtr surface(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32));
    if (!surface) {
        return ugh::fail(std::format("SDL_CreateSurface: {}", SDL_GetError()));
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint8_t index =
                pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x)];
            const ugh::Rgb c = palette.rgb(index);
            const std::uint8_t alpha = transparentZero && index == 0 ? 0 : 255;
            SDL_WriteSurfacePixel(surface.get(), x, y, c.r, c.g, c.b, alpha);
        }
    }
    return surface;
}

ugh::Result<void> savePng(const ugh::sdl::SurfacePtr& surface, const std::filesystem::path& path)
{
    if (!SDL_SavePNG(surface.get(), path.string().c_str())) {
        return ugh::fail(std::format("SDL_SavePNG {}: {}", path.string(), SDL_GetError()));
    }
    return {};
}

ugh::Result<void> extract(const ugh::ExeImage& exe, const std::filesystem::path& out)
{
    std::filesystem::create_directories(out / "pictures");
    std::filesystem::create_directories(out / "sprites");

    for (std::size_t i = 0; i < ugh::layout::kIntroPictures.size(); ++i) {
        const auto segment = ugh::layout::kIntroPictures.at(i).segment;
        auto saved = ugh::loadPicture(exe, segment).and_then([&](const ugh::Picture& picture) {
            return toSurface(ugh::Picture::kWidth, ugh::Picture::kHeight, picture.pixels, picture.palette, false)
                .and_then([&](const auto& surface) {
                    return savePng(surface, out / "pictures" / std::format("intro_{}_{:04x}.png", i, segment));
                });
        });
        if (!saved) {
            return saved;
        }
    }

    auto palette = ugh::loadGamePalette(exe);
    auto sprites = ugh::loadSprites(exe);
    if (!palette || !sprites) {
        return ugh::fail(palette ? sprites.error().message : palette.error().message);
    }

    // Palette swatch: 16x16 grid of 8x8 cells.
    constexpr std::size_t kSwatchSize = 128;
    std::vector<std::uint8_t> swatch(kSwatchSize * kSwatchSize);
    for (std::size_t i = 0; i < swatch.size(); ++i) {
        const std::size_t x = i % kSwatchSize;
        const std::size_t y = i / kSwatchSize;
        swatch[i] = static_cast<std::uint8_t>(((y / 8) * 16) + (x / 8));
    }
    if (auto saved = toSurface(128, 128, swatch, *palette, false).and_then([&](const auto& surface) {
            return savePng(surface, out / "palette.png");
        });
        !saved) {
        return saved;
    }

    int written = 0;
    for (std::size_t number = 0; number < sprites->size(); ++number) {
        const ugh::Sprite& sprite = sprites->at(number);
        if (sprite.width == 0 || sprite.height == 0) {
            continue;
        }
        auto saved = toSurface(sprite.width, sprite.height, sprite.pixels, *palette, true)
                         .and_then([&](const auto& surface) {
                             return savePng(surface, out / "sprites" / std::format("sprite_{:03x}.png", number));
                         });
        if (!saved) {
            return saved;
        }
        ++written;
    }
    std::println("{} pictures, {} sprites, palette -> {}", ugh::layout::kIntroPictures.size(), written,
                 out.string());
    return {};
}

int run(std::span<char*> args)
{
    if (args.size() != 3) {
        std::println(stderr, "usage: ugh-extract <UGH.EXE> <output-dir>");
        return 2;
    }
    const ugh::sdl::Context sdl(0);
    if (!sdl.ok()) {
        std::println(stderr, "SDL_Init: {}", SDL_GetError());
        return 1;
    }
    auto result = ugh::ExeImage::load(args[1]).and_then([&](const ugh::ExeImage& exe) {
        return extract(exe, args[2]);
    });
    if (!result) {
        std::println(stderr, "ugh-extract: {}", result.error().message);
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        return run(std::span(argv, static_cast<std::size_t>(argc)));
    } catch (const std::exception& e) {
        static_cast<void>(std::fputs("ugh-extract: fatal: ", stderr));
        static_cast<void>(std::fputs(e.what(), stderr));
        static_cast<void>(std::fputs("\n", stderr));
        return 1;
    } catch (...) {
        return 1;
    }
}
