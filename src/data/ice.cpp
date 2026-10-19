#include "data/ice.hpp"

#include "core/byte_reader.hpp"

#include <array>
#include <cstddef>
#include <iterator>
#include <utility>

namespace ugh {
namespace {

/// Literal-run length codes (tables at 113b:4ddd): read N bits; the all-ones value escapes to the next code.
struct LiteralCode {
    int bits;
    int escape;
    int add;
};
constexpr std::array<LiteralCode, 5> kLiteralCodes{{
    {.bits = 2, .escape = 3, .add = 1},
    {.bits = 2, .escape = 3, .add = 4},
    {.bits = 3, .escape = 7, .add = 7},
    {.bits = 8, .escape = 0xFF, .add = 0xE},
    {.bits = 15, .escape = 0x7FFF, .add = 0x10D},
}};

/// Match length codes indexed by the number of leading 1 bits (tables at 113b:4e05 and 113b:4e0a).
struct LengthCode {
    int bits;
    int base;
};
constexpr std::array<LengthCode, 5> kLengthCodes{{
    {.bits = 0, .base = 0},
    {.bits = 0, .base = 1},
    {.bits = 1, .base = 2},
    {.bits = 2, .base = 4},
    {.bits = 10, .base = 8},
}};

/// Match distance codes indexed by the number of leading 1 bits (tables at 113b:4e0f and 113b:4e13).
struct DistanceCode {
    int bits;
    int add;
};
constexpr std::array<DistanceCode, 3> kDistanceCodes{{
    {.bits = 8, .add = 0x1F},
    {.bits = 5, .add = -1},
    {.bits = 12, .add = 0x11F},
}};

constexpr std::size_t kHeaderSize = 12;
constexpr std::uint32_t kMagic = 0x49434521; // "ICE!"

/// Reads the stream backwards like the original (SI decrements, bit buffer refilled with a sentinel bit).
/// Any out-of-range access sets `failed_` and yields zeros; callers check `failed()` once per step.
class Decoder {
public:
    Decoder(std::span<const std::uint8_t> packed, std::size_t unpackedSize)
        : packed_(packed), out_(unpackedSize), src_(std::ssize(packed) - 1), dst_(std::ssize(out_) - 1),
          bitBuffer_(nextSourceByte())
    {
    }

    [[nodiscard]] std::vector<std::uint8_t> takeOutput() { return std::move(out_); }

    [[nodiscard]] bool failed() const { return failed_; }
    [[nodiscard]] bool done() const { return dst_ < 0; }

    void run()
    {
        while (!failed_) {
            copyLiterals();
            if (done() || failed_) {
                return;
            }
            copyMatch();
        }
    }

private:
    std::uint8_t nextSourceByte()
    {
        if (src_ < 0) {
            failed_ = true;
            return 0;
        }
        return packed_[static_cast<std::size_t>(src_--)];
    }

    int bit()
    {
        unsigned shifted = unsigned{bitBuffer_} << 1U;
        int carry = static_cast<int>(shifted >> 8U);
        if ((shifted & 0xFFU) == 0) {
            shifted = (unsigned{nextSourceByte()} << 1U) | static_cast<unsigned>(carry);
            carry = static_cast<int>(shifted >> 8U);
        }
        bitBuffer_ = static_cast<std::uint8_t>(shifted & 0xFFU);
        return carry;
    }

    int bits(int count)
    {
        int value = 0;
        for (int i = 0; i < count; ++i) {
            value = (value << 1) | bit();
        }
        return value;
    }

    int leadingOnes(int limit)
    {
        int ones = 0;
        while (ones < limit && bit() == 1) {
            ++ones;
        }
        return ones;
    }

    void put(std::uint8_t value)
    {
        if (dst_ < 0) {
            failed_ = true;
            return;
        }
        out_[static_cast<std::size_t>(dst_--)] = value;
    }

    void copyLiterals()
    {
        if (bit() == 0) {
            return;
        }
        int count = 1;
        if (bit() == 1) {
            int value = 0;
            const LiteralCode* code = nullptr;
            for (const LiteralCode& candidate : kLiteralCodes) {
                code = &candidate;
                value = bits(candidate.bits);
                if (value != candidate.escape) {
                    break;
                }
            }
            count = value + code->add + 1;
        }
        for (int i = 0; i < count && !failed_; ++i) {
            put(nextSourceByte());
        }
    }

    void copyMatch()
    {
        const LengthCode& lengthCode = kLengthCodes.at(static_cast<std::size_t>(leadingOnes(4)));
        const int lengthExtra = lengthCode.base + bits(lengthCode.bits);

        int distance = 0;
        if (lengthExtra == 0) {
            distance = bit() == 1 ? bits(9) + 0x3F : bits(6) - 1;
        } else {
            const DistanceCode& distanceCode = kDistanceCodes.at(static_cast<std::size_t>(leadingOnes(2)));
            distance = bits(distanceCode.bits) + distanceCode.add;
            if (distance < 0) {
                distance -= lengthExtra;
            }
        }

        const int count = lengthExtra + 2;
        std::ptrdiff_t from = dst_ + count + distance;
        for (int i = 0; i < count && !failed_; ++i) {
            if (from < 0 || from >= std::ssize(out_)) {
                failed_ = true;
                return;
            }
            put(out_[static_cast<std::size_t>(from--)]);
        }
    }

    std::span<const std::uint8_t> packed_;
    std::vector<std::uint8_t> out_;
    std::ptrdiff_t src_;
    std::ptrdiff_t dst_;
    std::uint8_t bitBuffer_;
    bool failed_ = false;
};

} // namespace

Result<std::vector<std::uint8_t>> unpackIce(std::span<const std::uint8_t> packed)
{
    const ByteReader header(packed);
    const auto magic = header.u32be(0);
    const auto packedSize = header.u32be(4);
    const auto unpackedSize = header.u32be(8);
    if (!magic || !packedSize || !unpackedSize || *magic != kMagic) {
        return fail("missing ICE! header");
    }
    // The original works in a single 16-bit segment.
    if (*packedSize <= kHeaderSize || *packedSize > packed.size() || *unpackedSize == 0 || *unpackedSize > 0xFFFF) {
        return fail("invalid ICE! block sizes");
    }

    Decoder decoder(packed.first(*packedSize), *unpackedSize);
    decoder.run();
    if (decoder.failed() || !decoder.done()) {
        return fail("corrupt ICE! stream");
    }
    return decoder.takeOutput();
}

} // namespace ugh
