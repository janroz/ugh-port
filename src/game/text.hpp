#pragma once

#include "data/data_segment.hpp"
#include "game/video.hpp"

#include <cstdint>

namespace ugh {

struct TextPosition {
    std::uint16_t offset; // data-segment offset of the next unread byte
    int y;
    bool more;            // false once the terminating 0 was read
};

/// 113b:077c: draws one line of the text at DS:offset with the font sprites (16 px per character).
/// A leading 0xFE centres the line; 0x0D advances 16 px, 0xFD 8 px, 0x00 ends the text.
TextPosition drawTextLine(Video& video, const DataSegment& data, std::uint16_t offset, int y);

/// 113b:0776: draws lines until the terminating 0.
TextPosition drawText(Video& video, const DataSegment& data, std::uint16_t offset, int y);

} // namespace ugh
