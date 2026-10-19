#pragma once

#include "core/error.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <span>

namespace ugh {

/// Bounds-checked reads of little- and big-endian values from a byte span.
/// The only sanctioned way to interpret raw data taken from the original game.
class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> data) : data_(data) {}

    [[nodiscard]] std::size_t size() const { return data_.size(); }

    [[nodiscard]] Result<std::span<const std::uint8_t>> bytes(std::size_t offset, std::size_t count) const
    {
        if (offset > data_.size() || count > data_.size() - offset) {
            return fail(std::format("read of {} bytes at offset {:#x} exceeds {} bytes", count, offset, data_.size()));
        }
        return data_.subspan(offset, count);
    }

    [[nodiscard]] Result<std::uint8_t> u8(std::size_t offset) const
    {
        return bytes(offset, 1).transform([](auto b) { return b[0]; });
    }

    [[nodiscard]] Result<std::uint16_t> u16le(std::size_t offset) const
    {
        return bytes(offset, 2).transform([](auto b) { return static_cast<std::uint16_t>(b[0] | (b[1] << 8U)); });
    }

    [[nodiscard]] Result<std::uint32_t> u32be(std::size_t offset) const
    {
        return bytes(offset, 4).transform([](auto b) {
            return (std::uint32_t{b[0]} << 24U) | (std::uint32_t{b[1]} << 16U) | (std::uint32_t{b[2]} << 8U) |
                   std::uint32_t{b[3]};
        });
    }

private:
    std::span<const std::uint8_t> data_;
};

} // namespace ugh
