#pragma once

#include <cstdint>

namespace ugh {

/// PC keyboard set-1 make codes, the values the original keyboard handler stores (CS:4509).
enum class Scancode : std::uint8_t {
    None = 0x00,
    Escape = 0x01,
    Q = 0x10,
    Enter = 0x1C,
    C = 0x2E,
    Space = 0x39,
    F1 = 0x3B,
    F2 = 0x3C,
    F3 = 0x3D,
    F4 = 0x3E,
    F5 = 0x3F,
};

} // namespace ugh
