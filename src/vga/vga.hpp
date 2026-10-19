#pragma once

#include "data/graphics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ugh {

/// Model of the VGA as used by the game's unchained 256-colour modes ("Mode X"):
/// four 64 KiB planes, sequencer map mask, graphics-controller write modes 0/1 with latches,
/// CRTC start address / row pitch / split screen, and the DAC.
class Vga {
public:
    static constexpr int kScreenWidth = 320;
    static constexpr int kScreenHeight = 200;
    static constexpr std::size_t kPlaneBytes = 0x10000;

    Vga() : planes_(kPlaneBytes * 4) {}

    /// Sequencer register 2: planes written by `write`.
    void setMapMask(std::uint8_t mask) { mapMask_ = mask & 0x0FU; }

    /// Graphics controller register 5, bits 0-1 (only modes 0 and 1 are used by the game).
    void setWriteMode(std::uint8_t mode) { writeMode_ = mode & 0x03U; }

    /// CPU write to A000:offset.
    void write(std::uint16_t offset, std::uint8_t value);

    /// Graphics controller register 8: bits written from the CPU value, the others come from the latches.
    void setBitMask(std::uint8_t mask) { bitMask_ = mask; }

    /// Graphics controller register 3 (function select): how the CPU value combines with the latches.
    enum class Function : std::uint8_t { Replace = 0, And = 1, Or = 2, Xor = 3 };
    void setFunction(Function function) { function_ = function; }

    /// Graphics controller register 4: plane returned by `read`.
    void setReadMap(std::uint8_t plane) { readMap_ = plane & 0x03U; }

    /// CPU read from A000:offset: loads the latches and returns the read-map plane.
    std::uint8_t read(std::uint16_t offset);

    /// Every byte of every plane set to `value`.
    void fill(std::uint8_t value);

    /// CRTC: display start (in plane bytes), bytes per row, and first pixel row shown from address 0.
    void setStartAddress(std::uint16_t address) { startAddress_ = address; }
    void setRowBytes(std::uint16_t rowBytes) { rowBytes_ = rowBytes; }
    void setSplitRow(int row) { splitRow_ = row; }

    [[nodiscard]] Palette& dac() { return dac_; }
    [[nodiscard]] const Palette& dac() const { return dac_; }

    /// Produces the displayed 320x200 indexed frame.
    void scanout(std::span<std::uint8_t> frame) const;

private:
    [[nodiscard]] static std::size_t index(int plane, std::uint16_t offset)
    {
        return (static_cast<std::size_t>(plane) * kPlaneBytes) + offset;
    }

    std::vector<std::uint8_t> planes_;
    std::array<std::uint8_t, 4> latches_{};
    std::uint8_t mapMask_ = 0x0F;
    std::uint8_t writeMode_ = 0;
    std::uint8_t readMap_ = 0;
    std::uint8_t bitMask_ = 0xFF;
    Function function_ = Function::Replace;
    std::uint16_t startAddress_ = 0;
    std::uint16_t rowBytes_ = 80;
    int splitRow_ = kScreenHeight;
    Palette dac_;
};

} // namespace ugh
