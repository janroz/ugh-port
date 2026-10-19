#include "game/text.hpp"

namespace ugh {
namespace {

constexpr std::uint8_t kCentre = 0xFE;
constexpr std::uint8_t kLineEnd = 0x0D;
constexpr std::uint8_t kHalfLineEnd = 0xFD;
constexpr std::uint8_t kTextEnd = 0x00;
constexpr int kFirstFontSprite = 0x237;
constexpr int kCharacterWidth = 16;

/// The font has Ä, Ö, Ü in the slots after 'Z' (code page 437 input).
std::uint8_t fontSlot(std::uint8_t character)
{
    switch (character) {
    case 0x8E:
        return 0x5B;
    case 0x99:
        return 0x5C;
    case 0x9A:
        return 0x5D;
    default:
        return character;
    }
}

bool endsLine(std::uint8_t character)
{
    return character == kTextEnd || character == kLineEnd || character == kHalfLineEnd;
}

} // namespace

TextPosition drawTextLine(Video& video, const DataSegment& data, std::uint16_t offset, int y)
{
    std::uint16_t x = 0;
    std::uint8_t character = data.byte(offset++);

    if (character == kCentre) {
        unsigned length = 0;
        for (std::uint16_t scan = offset; !endsLine(data.byte(scan)); ++scan) {
            ++length;
        }
        x = static_cast<std::uint16_t>(static_cast<std::uint16_t>(320 - (length * kCharacterWidth)) >> 1U);
        character = data.byte(offset++);
    }

    while (true) {
        switch (character) {
        case kLineEnd:
            return {.offset = offset, .y = y + 16, .more = true};
        case kHalfLineEnd:
            return {.offset = offset, .y = y + 8, .more = true};
        case kTextEnd:
            return {.offset = offset, .y = y, .more = false};
        default:
            video.drawSprite(fontSlot(character) + kFirstFontSprite, static_cast<std::int16_t>(x), y);
            x = static_cast<std::uint16_t>(x + kCharacterWidth);
            character = data.byte(offset++);
            break;
        }
    }
}

TextPosition drawText(Video& video, const DataSegment& data, std::uint16_t offset, int y)
{
    TextPosition position{.offset = offset, .y = y, .more = true};
    while (position.more) {
        position = drawTextLine(video, data, position.offset, position.y);
    }
    return position;
}

} // namespace ugh
