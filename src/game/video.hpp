#pragma once

#include "data/graphics.hpp"
#include "game/assets.hpp"
#include "runtime/task.hpp"
#include "vga/vga.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ugh {

/// The game's graphics layer (segment 113b, 41fd..503b): page management, sprites, background restore and
/// palette fades on top of the emulated VGA. Page offsets mirror the variables at CS:4600..4610.
class Video {
public:
    Video(Vga& vga, FrameClock& clock, std::vector<Sprite> sprites, Palette gamePalette)
        : vga_(&vga), clock_(&clock), sprites_(std::move(sprites)), gamePalette_(gamePalette)
    {
        spriteWidths_.reserve(sprites_.size());
        for (const Sprite& sprite : sprites_) {
            spriteWidths_.push_back(static_cast<std::uint8_t>(sprite.width));
        }
    }

    /// 113b:4fb3: 320-pixel rows, three 16000-byte pages, no split screen (start-up pictures).
    Task<> setMode320();

    /// 113b:503b: 384-pixel rows (room for scrolling), status area shown from address 0 below row 192.
    Task<> setMode384();

    /// 113b:4f96: zeroes all video memory.
    void clearVram();

    /// 113b:4ebb: shows the page just drawn and draws into the other one.
    void flip();

    /// 113b:41fd: sprite `number` at (x, y); colour 0 is transparent and pixels below the waterline get
    /// colour | 0x40. Rows at or below the screen height go to the status area.
    void drawSprite(int number, int x, int y);

    /// 113b:43ea: copies a rectangle from the background page to the draw page (latch copy).
    void restoreRect(int x, int y, int width, int height);

    /// 113b:43a5: restores the whole play area from the background page.
    void restoreScreen() { restoreRect(0, 0, 320, screenHeight_); }

    /// 113b:446f: copies the draw page to the background page.
    void saveBackground();

    /// 113b:4c81 + 113b:4634: takes the picture's palette and copies its pixels into the draw page.
    void showPicture(const Picture& picture);

    /// 113b:466a: game palette colours 0..127, repeated for 128..255.
    void useGamePalette();

    /// 113b:4e19 / 113b:4e28: 65 steps of 4/256, one per frame (colours 0..216 only, like the original).
    Task<> fadeIn();
    Task<> fadeOut();

    /// 113b:4e9b: zeroes the DAC, 32 colours per frame.
    Task<> blackOut();

    /// 113b:4e36: DAC colours 0..216 = current palette * level / 256, after the next retrace.
    Task<> fadeStep(unsigned level);

    /// 113b:43b9: restores the rectangle covered by sprite `number` at (x, y).
    void restoreSpriteRect(int number, int x, int y);

    /// Overwrites the width byte of sprite `number` in the sprite directory, which drawing and restoring use
    /// (the first demo narrows the UGH sign this way at the right screen edge, 113b:099f).
    void setSpriteWidth(int number, std::uint8_t width);

    /// Writes `color` into three consecutive status-area rows at `offset` for the planes in `planeMask`
    /// (fuel bar, 113b:40e2).
    void plotStatusColumn(std::uint16_t offset, std::uint8_t planeMask, std::uint8_t color);

    /// Byte `offset` of the background page in `plane` (collision probes read the background, 113b:1457).
    [[nodiscard]] std::uint8_t backgroundByte(std::uint16_t offset, int plane);

    /// 113b:2d5f: ORs the water bit (0x40) into one background row (320 pixels) that went under water.
    void tintBackgroundRow(int y);

    /// 113b:3cde: copies one byte (4 pixels) at `offset` from the background page to the draw page.
    void restoreByte(std::uint16_t offset);

    /// 113b:3d19: one pixel of the draw page (`offset` in plane bytes).
    void plotPixel(std::uint16_t offset, int plane, std::uint8_t color);

    /// 113b:2db9: replays one phase of the water-surface routine on the draw page at `rowOffset`.
    void drawWaterSurface(std::uint16_t rowOffset, std::span<const SurfaceWrite> writes);

    /// DS:2903.
    void setWaterline(int y) { waterline_ = y; }

private:
    static constexpr std::uint16_t kRowBytes384 = 96;

    Vga* vga_;
    FrameClock* clock_;
    std::vector<Sprite> sprites_;
    std::vector<std::uint8_t> spriteWidths_; // width bytes of the sprite directory (6b63:n*4+2), writable
    Palette gamePalette_;
    Palette current_; // CS:4681

    int screenHeight_ = 200;          // CS:4600
    std::uint16_t pageBytes_ = 0;     // CS:4602
    std::uint16_t statusPage_ = 0;    // CS:4604
    std::uint16_t displayPage_ = 0;   // CS:4608
    std::uint16_t drawPage_ = 0;      // CS:460c
    std::uint16_t backgroundPage_ = 0; // CS:4610
    int waterline_ = 0;
};

} // namespace ugh
