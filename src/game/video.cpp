#include "game/video.hpp"

#include <algorithm>
#include <utility>

namespace ugh {
namespace {

constexpr unsigned kFadeStep = 4;
constexpr unsigned kFullLevel = 256;
constexpr std::size_t kFadedColors = 0xD9;

std::uint16_t offset16(int value)
{
    return static_cast<std::uint16_t>(value);
}

} // namespace

Task<> Video::setMode320()
{
    screenHeight_ = 200;
    pageBytes_ = 0x3E80;
    co_await blackOut();
    vga_->setRowBytes(80);
    vga_->setSplitRow(Vga::kScreenHeight); // line compare 0x18f: below the visible area
    co_await clock_->nextFrame();
    statusPage_ = 0;
    displayPage_ = 0;
    drawPage_ = 0x3E80;
    backgroundPage_ = 0x7D00;
    clearVram();
    flip();
}

Task<> Video::setMode384()
{
    screenHeight_ = 192;
    pageBytes_ = 0x4800;
    co_await blackOut();
    vga_->setRowBytes(kRowBytes384);
    vga_->setSplitRow(192); // line compare 0x17f on 400 double-scanned lines
    co_await clock_->nextFrame();
    statusPage_ = 0;
    displayPage_ = 0x0A88;
    drawPage_ = 0x0A88 + 0x4F80;
    backgroundPage_ = 0x0A88 + (2 * 0x4F80);
    clearVram();
    flip();
}

void Video::clearVram()
{
    vga_->setMapMask(0x0F);
    vga_->fill(0);
}

void Video::flip()
{
    std::swap(displayPage_, drawPage_);
    vga_->setStartAddress(displayPage_);
}

void Video::drawSprite(int number, int x, int y)
{
    if (number < 0 || std::cmp_greater_equal(number, sprites_.size())) {
        return;
    }
    const Sprite& sprite = sprites_[static_cast<std::size_t>(number)];
    // A directory width beyond the stored columns would make the original read the next sprite's data; the
    // game never draws such a sprite (113b:099f only widens the sign while it is off screen).
    const int columns = std::min<int>(spriteWidths_[static_cast<std::size_t>(number)], sprite.width);

    int destination = drawPage_;
    if (y >= screenHeight_) {
        destination = statusPage_;
        y -= 192;
    }
    const int waterDepth = waterline_ - y;
    destination += (x >> 2) + (y * kRowBytes384);

    unsigned planeMask = 1U << static_cast<unsigned>(x & 3);
    for (int column = 0; column < columns; ++column) {
        vga_->setMapMask(static_cast<std::uint8_t>(planeMask));
        for (int row = 0; row < sprite.height; ++row) {
            std::uint8_t pixel = sprite.at(column, row);
            if (pixel == 0) {
                continue;
            }
            if (waterDepth - row < 0) {
                pixel |= 0x40U;
            }
            vga_->write(offset16(destination + (row * kRowBytes384)), pixel);
        }
        planeMask = (planeMask << 1U) & 0x0FU;
        if (planeMask == 0) {
            planeMask = 1;
            ++destination;
        }
    }
    vga_->setMapMask(0x0F);
}

void Video::restoreRect(int x, int y, int width, int height)
{
    const int offset = (y * kRowBytes384) + (x >> 2);
    const auto bytesPerRow = static_cast<unsigned>(((x & 3) + width + 3) & 0xFFFF) >> 2U;

    vga_->setWriteMode(1);
    for (int row = 0; row < height; ++row) {
        const int rowOffset = offset + (row * kRowBytes384);
        for (unsigned i = 0; i < bytesPerRow; ++i) {
            const int column = rowOffset + static_cast<int>(i);
            static_cast<void>(vga_->read(offset16(backgroundPage_ + column)));
            vga_->write(offset16(drawPage_ + column), 0);
        }
    }
    vga_->setWriteMode(0);
}

void Video::restoreSpriteRect(int number, int x, int y)
{
    if (number < 0 || std::cmp_greater_equal(number, sprites_.size())) {
        return;
    }
    const Sprite& sprite = sprites_[static_cast<std::size_t>(number)];
    restoreRect(x, y, spriteWidths_[static_cast<std::size_t>(number)], sprite.height);
}

void Video::setSpriteWidth(int number, std::uint8_t width)
{
    if (number >= 0 && std::cmp_less(number, spriteWidths_.size())) {
        spriteWidths_[static_cast<std::size_t>(number)] = width;
    }
}

void Video::plotStatusColumn(std::uint16_t offset, std::uint8_t planeMask, std::uint8_t color)
{
    vga_->setMapMask(planeMask);
    for (int row = 0; row < 3; ++row) {
        vga_->write(offset16(offset + (row * kRowBytes384)), color);
    }
    vga_->setMapMask(0x0F);
}

std::uint8_t Video::backgroundByte(std::uint16_t offset, int plane)
{
    vga_->setReadMap(static_cast<std::uint8_t>(plane));
    const std::uint8_t value = vga_->read(offset16(backgroundPage_ + offset));
    vga_->setReadMap(0);
    return value;
}

void Video::tintBackgroundRow(int y)
{
    // MUL BYTE: only the low byte of y counts.
    const int row = (y & 0xFF) * kRowBytes384;
    vga_->setBitMask(0x40);
    vga_->setFunction(Vga::Function::Or);
    for (int column = 0x4F; column >= 0; --column) {
        const auto offset = offset16(backgroundPage_ + row + column);
        static_cast<void>(vga_->read(offset));
        vga_->write(offset, 0x40);
    }
    vga_->setBitMask(0xFF);
    vga_->setFunction(Vga::Function::Replace);
}

void Video::restoreByte(std::uint16_t offset)
{
    vga_->setBitMask(0x00);
    static_cast<void>(vga_->read(offset16(backgroundPage_ + offset)));
    vga_->write(offset16(drawPage_ + offset), 0);
    vga_->setBitMask(0xFF);
}

void Video::plotPixel(std::uint16_t offset, int plane, std::uint8_t color)
{
    vga_->setMapMask(static_cast<std::uint8_t>(1U << static_cast<unsigned>(plane & 3)));
    vga_->write(offset16(drawPage_ + offset), color);
    vga_->setMapMask(0x0F);
}

void Video::drawWaterSurface(std::uint16_t rowOffset, std::span<const SurfaceWrite> writes)
{
    for (const SurfaceWrite& write : writes) {
        vga_->setMapMask(write.planeMask);
        vga_->write(offset16(drawPage_ + rowOffset + write.offset), write.color);
    }
    vga_->setMapMask(0x0F);
}

void Video::saveBackground()
{
    vga_->setWriteMode(1);
    for (unsigned i = 0; i < pageBytes_; ++i) {
        static_cast<void>(vga_->read(offset16(static_cast<int>(drawPage_ + i))));
        vga_->write(offset16(static_cast<int>(backgroundPage_ + i)), 0);
    }
    vga_->setWriteMode(0);
}

void Video::showPicture(const Picture& picture)
{
    current_ = picture.palette;

    std::size_t source = 0;
    for (unsigned i = 0; i < pageBytes_; ++i) {
        for (unsigned plane = 0; plane < 4 && source < picture.pixels.size(); ++plane) {
            vga_->setMapMask(static_cast<std::uint8_t>(1U << plane));
            vga_->write(offset16(static_cast<int>(drawPage_ + i)), picture.pixels[source++]);
        }
    }
    vga_->setMapMask(0x0F);
}

void Video::useGamePalette()
{
    const auto firstHalf = gamePalette_.components().first(Palette::kBytes / 2);
    current_.setColors(0, firstHalf);
    current_.setColors(Palette::kColors / 2, firstHalf);
}

Task<> Video::fadeIn()
{
    for (unsigned level = 0; level <= kFullLevel; level += kFadeStep) {
        co_await fadeStep(level);
    }
}

Task<> Video::fadeOut()
{
    for (int level = kFullLevel; level >= 0; level -= static_cast<int>(kFadeStep)) {
        co_await fadeStep(static_cast<unsigned>(level));
    }
}

Task<> Video::blackOut()
{
    constexpr std::size_t kColorsPerFrame = 32;
    const std::array<std::uint8_t, kColorsPerFrame * 3> black{};
    for (std::size_t first = 0; first < Palette::kColors; first += kColorsPerFrame) {
        co_await clock_->nextFrame();
        vga_->dac().setColors(first, black);
    }
}

Task<> Video::fadeStep(unsigned level)
{
    const Palette faded = current_.faded(level);
    co_await clock_->nextFrame();
    vga_->dac().setColors(0, faded.components().first(kFadedColors * 3));
}

} // namespace ugh
