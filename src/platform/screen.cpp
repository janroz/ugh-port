#include "platform/screen.hpp"

#include <format>

namespace ugh {

Result<Screen> Screen::create(const char* title)
{
    constexpr int kInitialScale = 3;
    // Mode 13h pixels are displayed 5:6 (320x200 stretched to a 4:3 monitor).
    constexpr int kWindowWidth = kWidth * kInitialScale;
    constexpr int kWindowHeight = kWidth * kInitialScale * 3 / 4;

    sdl::WindowPtr window(SDL_CreateWindow(title, kWindowWidth, kWindowHeight, SDL_WINDOW_RESIZABLE));
    if (!window) {
        return fail(std::format("SDL_CreateWindow: {}", SDL_GetError()));
    }
    sdl::RendererPtr renderer(SDL_CreateRenderer(window.get(), nullptr));
    if (!renderer) {
        return fail(std::format("SDL_CreateRenderer: {}", SDL_GetError()));
    }
    SDL_SetRenderVSync(renderer.get(), 1);
    SDL_SetRenderLogicalPresentation(renderer.get(), kWidth, kWidth * 3 / 4, SDL_LOGICAL_PRESENTATION_LETTERBOX);

    sdl::TexturePtr texture(
        SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight));
    if (!texture) {
        return fail(std::format("SDL_CreateTexture: {}", SDL_GetError()));
    }
    SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_NEAREST);

    return Screen(std::move(window), std::move(renderer), std::move(texture));
}

Result<void> Screen::present(std::span<const std::uint8_t> pixels, const Palette& palette)
{
    if (pixels.size() != argb_.size()) {
        return fail("frame has wrong size");
    }
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        const Rgb c = palette.rgb(pixels[i]);
        argb_[i] = 0xFF000000U | (std::uint32_t{c.r} << 16U) | (std::uint32_t{c.g} << 8U) | c.b;
    }

    if (!SDL_UpdateTexture(texture_.get(), nullptr, argb_.data(), kWidth * static_cast<int>(sizeof(std::uint32_t)))) {
        return fail(std::format("SDL_UpdateTexture: {}", SDL_GetError()));
    }
    SDL_SetRenderDrawColor(renderer_.get(), 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer_.get());
    SDL_RenderTexture(renderer_.get(), texture_.get(), nullptr, nullptr);
    SDL_RenderPresent(renderer_.get());
    return {};
}

} // namespace ugh
