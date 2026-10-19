#include "vga/vga.hpp"

#include <algorithm>

namespace ugh {

void Vga::write(std::uint16_t offset, std::uint8_t value)
{
    for (int plane = 0; plane < 4; ++plane) {
        if ((mapMask_ & (1U << static_cast<unsigned>(plane))) == 0) {
            continue;
        }
        const std::uint8_t latch = latches_[static_cast<std::size_t>(plane)];
        if (writeMode_ == 1) {
            planes_[index(plane, offset)] = latch;
            continue;
        }
        std::uint8_t combined = value;
        switch (function_) {
        case Function::Replace:
            break;
        case Function::And:
            combined &= latch;
            break;
        case Function::Or:
            combined |= latch;
            break;
        case Function::Xor:
            combined ^= latch;
            break;
        }
        planes_[index(plane, offset)] =
            static_cast<std::uint8_t>((combined & bitMask_) | (latch & static_cast<std::uint8_t>(~bitMask_)));
    }
}

std::uint8_t Vga::read(std::uint16_t offset)
{
    for (int plane = 0; plane < 4; ++plane) {
        latches_[static_cast<std::size_t>(plane)] = planes_[index(plane, offset)];
    }
    return latches_[readMap_];
}

void Vga::fill(std::uint8_t value)
{
    std::ranges::fill(planes_, value);
}

void Vga::scanout(std::span<std::uint8_t> frame) const
{
    for (int y = 0; y < kScreenHeight; ++y) {
        const int row = y < splitRow_ ? y : y - splitRow_;
        const unsigned base = y < splitRow_ ? startAddress_ : 0U;
        const auto rowAddress = static_cast<std::uint16_t>(base + (static_cast<unsigned>(row) * rowBytes_));
        for (int x = 0; x < kScreenWidth; ++x) {
            const auto address = static_cast<std::uint16_t>(rowAddress + static_cast<unsigned>(x / 4));
            frame[(static_cast<std::size_t>(y) * kScreenWidth) + static_cast<std::size_t>(x)] =
                planes_[index(x % 4, address)];
        }
    }
}

} // namespace ugh
