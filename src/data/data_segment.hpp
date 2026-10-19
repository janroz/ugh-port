#pragma once

#include "core/error.hpp"
#include "data/exe_image.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ugh {

/// A byte variable in the data segment.
struct ByteVar {
    std::uint16_t offset;
};

/// A signed 16-bit variable in the data segment.
struct WordVar {
    std::uint16_t offset;
};

/// A signed 16-bit array in the data segment, element i at offset + 2 * i (players, entities, ...).
struct WordArray {
    std::uint16_t offset;
};

/// Mutable copy of the original data segment (DGROUP, DS = 6c09), zero-extended to a full 64 KiB segment.
/// Text templates and tables are used in place, exactly as the original addresses them;
/// 16-bit offsets can never leave the segment.
class DataSegment {
public:
    static constexpr std::size_t kBytes = 0x10000;

    [[nodiscard]] static Result<DataSegment> load(const ExeImage& exe);

    /// Any segment of the load module as a 64 KiB data segment (bytes past the module are zero).
    [[nodiscard]] static Result<DataSegment> fromSegment(const ExeImage& exe, std::uint16_t segment);

    [[nodiscard]] std::uint8_t byte(std::uint16_t offset) const { return bytes_[offset]; }
    [[nodiscard]] std::uint16_t word(std::uint16_t offset) const
    {
        return static_cast<std::uint16_t>(byte(offset) | (byte(static_cast<std::uint16_t>(offset + 1)) << 8U));
    }
    void setByte(std::uint16_t offset, std::uint8_t value) { bytes_[offset] = value; }
    void setWord(std::uint16_t offset, std::uint16_t value)
    {
        setByte(offset, static_cast<std::uint8_t>(value & 0xFFU));
        setByte(static_cast<std::uint16_t>(offset + 1), static_cast<std::uint8_t>(value >> 8U));
    }

    /// Typed access. Stores truncate to the variable's width exactly like the 16-bit original.
    [[nodiscard]] std::uint8_t get(ByteVar var) const { return byte(var.offset); }
    void set(ByteVar var, int value) { setByte(var.offset, static_cast<std::uint8_t>(value)); }
    [[nodiscard]] std::int16_t get(WordVar var) const { return static_cast<std::int16_t>(word(var.offset)); }
    void set(WordVar var, int value) { setWord(var.offset, static_cast<std::uint16_t>(value)); }
    [[nodiscard]] std::int16_t get(WordArray array, int index) const
    {
        return static_cast<std::int16_t>(word(element(array, index)));
    }
    void set(WordArray array, int index, int value)
    {
        setWord(element(array, index), static_cast<std::uint16_t>(value));
    }

    /// Zeroes `count` bytes from `offset` (wrapping inside the segment like REP STOSB).
    void clear(std::uint16_t offset, std::size_t count)
    {
        for (std::size_t i = 0; i < count; ++i) {
            setByte(static_cast<std::uint16_t>(offset + i), 0);
        }
    }

    /// Everything from `offset` to the end of the segment.
    [[nodiscard]] std::span<const std::uint8_t> from(std::uint16_t offset) const
    {
        return std::span<const std::uint8_t>(bytes_).subspan(offset);
    }

    /// Writable window; fails when it would cross the end of the segment.
    [[nodiscard]] Result<std::span<std::uint8_t>> window(std::uint16_t offset, std::size_t count);

    /// Copies the NUL-terminated string at `source` to `destination` without the terminator (113b:0652).
    void copyString(std::uint16_t destination, std::uint16_t source);

private:
    static std::uint16_t element(WordArray array, int index)
    {
        return static_cast<std::uint16_t>(array.offset + (2 * index));
    }

    explicit DataSegment(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}

    std::vector<std::uint8_t> bytes_;
};

} // namespace ugh
