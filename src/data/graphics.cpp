#include "data/graphics.hpp"

#include "core/byte_reader.hpp"
#include "data/ice.hpp"
#include "data/layout.hpp"

#include <algorithm>
#include <format>

namespace ugh {

Result<Palette> Palette::fromVga(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() != kBytes) {
        return fail(std::format("palette has {} bytes, expected {}", bytes.size(), kBytes));
    }
    if (std::ranges::any_of(bytes, [](std::uint8_t component) { return component > 63; })) {
        return fail("palette component exceeds 6 bits");
    }
    Palette palette;
    std::ranges::copy(bytes, palette.vga_.begin());
    return palette;
}

Palette Palette::faded(unsigned level) const
{
    Palette result;
    std::ranges::transform(vga_, result.vga_.begin(), [level](std::uint8_t component) {
        return static_cast<std::uint8_t>((component * level) >> 8U);
    });
    return result;
}

void Palette::setColors(std::size_t firstColor, std::span<const std::uint8_t> components)
{
    const std::size_t start = std::min(firstColor * 3, kBytes);
    const std::size_t count = std::min(components.size(), kBytes - start);
    std::ranges::copy(components.first(count), std::span(vga_).subspan(start).begin());
}

Rgb Palette::rgb(std::uint8_t index) const
{
    // 6-bit DAC value to 8 bits, replicating the top bits into the bottom ones.
    const auto expand = [](std::uint8_t v) { return static_cast<std::uint8_t>((v << 2U) | (v >> 4U)); };
    const std::size_t base = std::size_t{index} * 3;
    return {.r = expand(vga_.at(base)), .g = expand(vga_.at(base + 1)), .b = expand(vga_.at(base + 2))};
}

Result<Picture> loadPicture(const ExeImage& exe, std::uint16_t segment)
{
    constexpr std::size_t kPixelBytes = std::size_t{Picture::kWidth} * Picture::kHeight;

    auto unpacked = exe.segment(segment).and_then(unpackIce);
    if (!unpacked) {
        return fail(std::format("picture at segment {:#06x}: {}", segment, unpacked.error().message));
    }
    if (unpacked->size() != kPixelBytes + Palette::kBytes) {
        return fail(std::format("picture at segment {:#06x} has unexpected size {}", segment, unpacked->size()));
    }

    const std::span<const std::uint8_t> data(*unpacked);
    auto palette = Palette::fromVga(data.subspan(kPixelBytes));
    if (!palette) {
        return std::unexpected(palette.error());
    }
    // Stored in VGA write order (plane 0..3 for consecutive pixels), which equals linear screen order.
    return Picture{.pixels = {data.begin(), data.begin() + kPixelBytes}, .palette = *palette};
}

Result<std::vector<Sprite>> loadSprites(const ExeImage& exe)
{
    const auto directoryBytes = exe.segment(layout::kSpriteDirectory, layout::kSpriteDirectoryBytes);
    if (!directoryBytes) {
        return std::unexpected(directoryBytes.error());
    }
    const ByteReader directory(*directoryBytes);
    const std::size_t count = directory.size() / 4;

    std::vector<Sprite> sprites;
    sprites.reserve(count);
    for (std::size_t number = 0; number < count; ++number) {
        const auto bank = std::ranges::find_last_if(
            layout::kSpriteBanks, [number](const auto& b) { return b.firstSprite <= number; });
        const auto& bankLayout = bank.front();

        const std::size_t entry = number * 4;
        const auto offset = directory.u16le(entry);
        const auto width = directory.u8(entry + 2);
        const auto height = directory.u8(entry + 3);
        if (!offset || !width || !height) {
            return fail("sprite directory truncated");
        }

        const std::size_t size = std::size_t{*width} * *height;
        const auto columns =
            exe.segment(bankLayout.segment, bankLayout.bytes).and_then([&](auto bankData) {
                return ByteReader(bankData).bytes(*offset, size);
            });
        if (!columns) {
            return fail(std::format("sprite {}: {}", number, columns.error().message));
        }

        // Stored column by column (113b:41fd draws each column top to bottom).
        Sprite sprite{.width = *width, .height = *height, .pixels = std::vector<std::uint8_t>(size)};
        const std::size_t columnCount = *width;
        const std::size_t rowCount = *height;
        for (std::size_t x = 0; x < columnCount; ++x) {
            for (std::size_t y = 0; y < rowCount; ++y) {
                sprite.pixels[(y * columnCount) + x] = (*columns)[(x * rowCount) + y];
            }
        }
        sprites.push_back(std::move(sprite));
    }
    return sprites;
}

Result<Palette> loadGamePalette(const ExeImage& exe)
{
    return exe.segment(layout::kGamePalette, Palette::kBytes).and_then(Palette::fromVga);
}

} // namespace ugh
