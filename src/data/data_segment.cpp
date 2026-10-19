#include "data/data_segment.hpp"

#include "data/layout.hpp"

#include <algorithm>
#include <format>

namespace ugh {

Result<DataSegment> DataSegment::load(const ExeImage& exe)
{
    return fromSegment(exe, layout::kDataSegment);
}

Result<DataSegment> DataSegment::fromSegment(const ExeImage& exe, std::uint16_t segment)
{
    auto initialized = exe.segment(segment);
    if (!initialized) {
        return std::unexpected(initialized.error());
    }
    std::vector<std::uint8_t> bytes(kBytes);
    std::ranges::copy(initialized->first(std::min(initialized->size(), kBytes)), bytes.begin());
    return DataSegment(std::move(bytes));
}

Result<std::span<std::uint8_t>> DataSegment::window(std::uint16_t offset, std::size_t count)
{
    if (count > kBytes - offset) {
        return fail(std::format("data segment window {:#06x}+{} crosses the segment end", offset, count));
    }
    return std::span<std::uint8_t>(bytes_).subspan(offset, count);
}

void DataSegment::copyString(std::uint16_t destination, std::uint16_t source)
{
    for (std::uint8_t value = byte(source); value != 0; value = byte(source)) {
        setByte(destination, value);
        ++source;
        ++destination;
    }
}

} // namespace ugh
