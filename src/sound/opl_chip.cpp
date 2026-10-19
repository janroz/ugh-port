#include "sound/opl_chip.hpp"

#include <opl3.h>

namespace ugh {

OplChip::OplChip() : chip_(std::make_unique<_opl3_chip>())
{
    OPL3_Reset(chip_.get(), kSampleRate);
}

OplChip::~OplChip() = default;
OplChip::OplChip(OplChip&&) noexcept = default;
OplChip& OplChip::operator=(OplChip&&) noexcept = default;

void OplChip::write(std::uint8_t reg, std::uint8_t value)
{
    OPL3_WriteReg(chip_.get(), reg, value);
}

void OplChip::generate(std::span<std::int16_t> stereo)
{
    for (std::size_t i = 0; i + 1 < stereo.size(); i += 2) {
        OPL3_Generate(chip_.get(), stereo.subspan(i, 2).data());
    }
}

} // namespace ugh
