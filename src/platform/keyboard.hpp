#pragma once

#include <SDL3/SDL_scancode.h>

#include <cstdint>
#include <optional>

namespace ugh {

struct DosKey {
    std::uint8_t makeCode;     // PC keyboard set-1 make code
    bool extended;             // sent with the 0xE0 prefix
    std::optional<char> ascii; // what INT 16h would return (unshifted)
};

/// Maps an SDL key to what a DOS PC would have produced; nullopt for keys without an XT equivalent.
[[nodiscard]] std::optional<DosKey> toDosKey(SDL_Scancode key);

} // namespace ugh
