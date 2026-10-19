#pragma once

#include "core/error.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace ugh {

/// Finds the player's copy of UGH.EXE and returns its bytes, or nullopt when the player cancelled.
///
/// Looks at, in this order: `commandLine` (when given, nothing else is tried), the copy kept in
/// `dataDirectory`, UGH.EXE or ugh/UGH.EXE next to the program and in the working directory; then asks with
/// a file dialog until a valid file is chosen. Every candidate must pass verifyGameExe(). A file found
/// anywhere but in `dataDirectory` is copied there, so later runs need neither the original nor the dialog.
[[nodiscard]] Result<std::optional<std::vector<std::uint8_t>>>
findGameExe(const std::optional<std::filesystem::path>& commandLine,
            const std::optional<std::filesystem::path>& dataDirectory);

} // namespace ugh
