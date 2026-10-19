#pragma once

#include "core/error.hpp"
#include "data/exe_image.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ugh {

struct Rgb {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};

/// 256-colour VGA palette with 6-bit components, as programmed into the DAC.
class Palette {
public:
    static constexpr std::size_t kColors = 256;
    static constexpr std::size_t kBytes = kColors * 3;

    Palette() = default;
    [[nodiscard]] static Result<Palette> fromVga(std::span<const std::uint8_t> bytes);

    /// Palette scaled like the original fade (113b:4e36): component * level / 256, level 0..256.
    [[nodiscard]] Palette faded(unsigned level) const;

    [[nodiscard]] Rgb rgb(std::uint8_t index) const;

    /// Raw 6-bit components, 3 per colour.
    [[nodiscard]] std::span<const std::uint8_t> components() const { return vga_; }

    /// Overwrites colours starting at `firstColor` with `components` (3 per colour, clipped to the palette).
    void setColors(std::size_t firstColor, std::span<const std::uint8_t> components);

private:
    std::array<std::uint8_t, kBytes> vga_{};
};

/// Indexed 320x200 full-screen picture.
struct Picture {
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 200;

    std::vector<std::uint8_t> pixels; // row-major, kWidth * kHeight
    Palette palette;
};

/// ICE!-packed picture: 64000 pixel bytes in screen order followed by a 768-byte palette.
[[nodiscard]] Result<Picture> loadPicture(const ExeImage& exe, std::uint16_t segment);

/// Indexed sprite; colour 0 is transparent.
struct Sprite {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels; // row-major, width * height

    [[nodiscard]] std::uint8_t at(int x, int y) const
    {
        return pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x)];
    }
};

/// All sprites of the game, indexed by the sprite number used by the original code.
[[nodiscard]] Result<std::vector<Sprite>> loadSprites(const ExeImage& exe);

[[nodiscard]] Result<Palette> loadGamePalette(const ExeImage& exe);

} // namespace ugh
