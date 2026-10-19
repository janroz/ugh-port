#pragma once

#include <cstdint>
#include <memory>
#include <span>

struct _opl3_chip; // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp): Nuked-OPL3's own struct tag

namespace ugh {

/// The AdLib's OPL chip (emulated by Nuked-OPL3), producing 16-bit stereo at the chip's native rate.
class OplChip {
public:
    static constexpr int kSampleRate = 49716;

    OplChip();
    ~OplChip();
    OplChip(const OplChip&) = delete;
    OplChip& operator=(const OplChip&) = delete;
    OplChip(OplChip&&) noexcept;
    OplChip& operator=(OplChip&&) noexcept;

    /// OUT 388h, register; OUT 389h, value.
    void write(std::uint8_t reg, std::uint8_t value);

    /// Fills `stereo` (interleaved left/right) with the next samples.
    void generate(std::span<std::int16_t> stereo);

private:
    std::unique_ptr<_opl3_chip> chip_; // complete type only in the .cpp, hence the out-of-line special members
};

} // namespace ugh
