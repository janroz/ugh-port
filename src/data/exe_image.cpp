#include "data/exe_image.hpp"

#include "core/byte_reader.hpp"
#include "core/sha256.hpp"

#include <fstream>
#include <iterator>

namespace ugh {

Result<void> verifyGameExe(std::span<const std::uint8_t> file)
{
    if (toHex(sha256(file)) != kGameExeSha256) {
        return fail("this is not the UGH.EXE of the 1992 DOS release (SHA-256 does not match)");
    }
    return {};
}

Result<ExeImage> ExeImage::load(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return fail(std::format("cannot open {}", path.string()));
    }
    const std::vector<std::uint8_t> file{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return fromFileBytes(file);
}

Result<ExeImage> ExeImage::fromFileBytes(std::span<const std::uint8_t> file)
{
    const ByteReader header(file);
    const auto magic = header.u16le(0);
    const auto lastPageBytes = header.u16le(2);
    const auto pageCount = header.u16le(4);
    const auto headerParagraphs = header.u16le(8);
    if (!magic || !lastPageBytes || !pageCount || !headerParagraphs) {
        return fail("file too short for an MZ header");
    }
    if (*magic != 0x5A4D) {
        return fail("not an MZ executable");
    }

    const std::size_t lastPage = *lastPageBytes == 0 ? 512 : *lastPageBytes;
    const std::size_t fileSize = ((std::size_t{*pageCount} - 1) * 512) + lastPage;
    const std::size_t headerSize = std::size_t{*headerParagraphs} * 16;
    if (*pageCount == 0 || fileSize > file.size() || headerSize > fileSize) {
        return fail("inconsistent MZ header sizes");
    }

    const auto module = file.subspan(headerSize, fileSize - headerSize);
    return ExeImage(std::vector<std::uint8_t>(module.begin(), module.end()));
}

Result<std::span<const std::uint8_t>> ExeImage::segment(std::uint16_t segment) const
{
    if (segment < kLoadSegment) {
        return fail(std::format("segment {:#06x} lies before the load module", segment));
    }
    const std::size_t offset = std::size_t{static_cast<std::uint16_t>(segment - kLoadSegment)} * 16;
    if (offset > image_.size()) {
        return fail(std::format("segment {:#06x} lies past the load module", segment));
    }
    return std::span<const std::uint8_t>(image_).subspan(offset);
}

Result<std::span<const std::uint8_t>> ExeImage::segment(std::uint16_t segment, std::size_t length) const
{
    return this->segment(segment).and_then([&](auto data) { return ByteReader(data).bytes(0, length); });
}

} // namespace ugh
