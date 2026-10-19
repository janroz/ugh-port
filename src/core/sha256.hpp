#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace ugh {

using Sha256Digest = std::array<std::uint8_t, 32>;

/// SHA-256 (FIPS 180-4) of `data`.
[[nodiscard]] Sha256Digest sha256(std::span<const std::uint8_t> data);

/// Lower-case hexadecimal form of a digest.
[[nodiscard]] std::string toHex(const Sha256Digest& digest);

} // namespace ugh
