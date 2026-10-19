#pragma once

#include <expected>
#include <string>
#include <utility>

namespace ugh {

/// Expected failure (missing file, corrupt data, ...). Carries a human-readable message.
struct Error {
    std::string message;
};

template <typename T>
using Result = std::expected<T, Error>;

[[nodiscard]] inline std::unexpected<Error> fail(std::string message)
{
    return std::unexpected(Error{std::move(message)});
}

} // namespace ugh
