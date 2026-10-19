#pragma once

#include "core/error.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace ugh {

/// SHA-256 of the only release the port is made for: UGH! for DOS by Play Byte (1992), UGH.EXE of 411 838 bytes.
inline constexpr std::string_view kGameExeSha256 = "ef93d2cd5eb558f6a7d0007e0109f2e9ee952dc125389d7a6256646087636d7c";

/// Fails unless `file` is exactly that UGH.EXE: the port reads data and code at fixed addresses inside it.
[[nodiscard]] Result<void> verifyGameExe(std::span<const std::uint8_t> file);

/// Load module of the original UGH.EXE (the file without its MZ header).
/// Segments are numbered as in the Ghidra analysis, which loads the module at segment 0x1000.
class ExeImage {
public:
    static constexpr std::uint16_t kLoadSegment = 0x1000;

    [[nodiscard]] static Result<ExeImage> load(const std::filesystem::path& path);
    [[nodiscard]] static Result<ExeImage> fromFileBytes(std::span<const std::uint8_t> file);

    /// Everything from the start of `segment` to the end of the load module.
    [[nodiscard]] Result<std::span<const std::uint8_t>> segment(std::uint16_t segment) const;

    /// The whole load module (linear address = (segment - kLoadSegment) * 16 + offset).
    [[nodiscard]] std::span<const std::uint8_t> loadModule() const { return image_; }

    /// Exactly `length` bytes starting at `segment`.
    [[nodiscard]] Result<std::span<const std::uint8_t>> segment(std::uint16_t segment, std::size_t length) const;

private:
    explicit ExeImage(std::vector<std::uint8_t> image) : image_(std::move(image)) {}

    std::vector<std::uint8_t> image_;
};

} // namespace ugh
