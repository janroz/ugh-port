#pragma once

#include "core/error.hpp"
#include "data/graphics.hpp"
#include "platform/sdl_handles.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ugh {

/// Window showing an indexed 320x200 frame, scaled with integer factors and the original 4:3 aspect.
class Screen {
public:
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 200;

    [[nodiscard]] static Result<Screen> create(const char* title);

    /// Converts `pixels` (kWidth * kHeight indices) through `palette` and shows the frame.
    [[nodiscard]] Result<void> present(std::span<const std::uint8_t> pixels, const Palette& palette);

private:
    Screen(sdl::WindowPtr window, sdl::RendererPtr renderer, sdl::TexturePtr texture)
        : window_(std::move(window)), renderer_(std::move(renderer)), texture_(std::move(texture)),
          argb_(static_cast<std::size_t>(kWidth) * kHeight)
    {
    }

    sdl::WindowPtr window_;
    sdl::RendererPtr renderer_;
    sdl::TexturePtr texture_;
    std::vector<std::uint32_t> argb_;
};

} // namespace ugh
