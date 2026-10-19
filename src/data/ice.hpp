#pragma once

#include "core/error.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ugh {

/// Unpacks a Pack-Ice ("ICE!") block, as done by the original routine at 113b:4cad.
/// `packed` starts at the "ICE!" header; it may extend past the block.
[[nodiscard]] Result<std::vector<std::uint8_t>> unpackIce(std::span<const std::uint8_t> packed);

} // namespace ugh
