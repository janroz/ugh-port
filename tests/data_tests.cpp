// Data-layer regression tests against the original UGH.EXE.
// Reference hashes come from an independent Python prototype of the decoders.

#include "core/sha256.hpp"
#include "data/exe_image.hpp"
#include "data/graphics.hpp"
#include "data/ice.hpp"
#include "data/layout.hpp"
#include "game/assets.hpp"
#include "check.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <print>
#include <source_location>
#include <span>
#include <vector>

namespace {

using ugh::test::Checker;

std::uint32_t fnv1a(std::span<const std::uint8_t> data)
{
    std::uint32_t hash = 0x811C9DC5;
    for (const std::uint8_t byte : data) {
        hash = (hash ^ byte) * 0x01000193U;
    }
    return hash;
}

void testIceBlocks(Checker& check, const ugh::ExeImage& exe)
{
    struct Expected {
        std::uint16_t segment;
        std::size_t size;
        std::uint32_t hash;
    };
    constexpr std::array<Expected, 5> kBlocks{{
        {.segment = ugh::layout::kLevels, .size = 32000, .hash = 0x05EC1997},
        {.segment = 0x3BF6, .size = 64768, .hash = 0x5A997B07},
        {.segment = 0x3D9A, .size = 64768, .hash = 0xCCF044DC},
        {.segment = 0x3F9E, .size = 64768, .hash = 0xAFC1C798},
        {.segment = 0x3FB7, .size = 64768, .hash = 0x5F060FAC},
    }};
    for (const auto& block : kBlocks) {
        const auto unpacked = exe.segment(block.segment).and_then(ugh::unpackIce);
        check(unpacked.has_value(), "ICE block unpacks");
        if (unpacked) {
            check(unpacked->size() == block.size, "ICE block size");
            check(fnv1a(*unpacked) == block.hash, "ICE block content hash");
        }
    }
}

void testIceRejectsCorruptInput(Checker& check, const ugh::ExeImage& exe)
{
    check(!ugh::unpackIce({}).has_value(), "empty input rejected");

    const auto block = exe.segment(0x3F9E, 385);
    check(block.has_value(), "test block readable");
    if (!block) {
        return;
    }
    std::vector<std::uint8_t> damaged(block->begin(), block->end());
    damaged.at(7) = 0x40; // packed size larger than the buffer
    check(!ugh::unpackIce(damaged).has_value(), "oversized packed length rejected");

    std::vector<std::uint8_t> truncated(block->begin(), block->begin() + 100);
    truncated.at(4) = 0;
    truncated.at(5) = 0;
    truncated.at(6) = 0;
    truncated.at(7) = 100; // valid header, stream ends far too early
    check(!ugh::unpackIce(truncated).has_value(), "truncated stream rejected");
}

void testPicturesAndSprites(Checker& check, const ugh::ExeImage& exe)
{
    for (const auto& entry : ugh::layout::kIntroPictures) {
        const auto picture = ugh::loadPicture(exe, entry.segment);
        check(picture.has_value(), "intro picture loads");
    }

    const auto sprites = ugh::loadSprites(exe);
    check(sprites.has_value(), "sprites load");
    if (sprites) {
        check(sprites->size() == 664, "sprite directory has 664 entries");
        check(sprites->at(0).width == 16 && sprites->at(0).height == 12, "sprite 0 is a 16x12 tile");
        check(sprites->at(0x223).width == 32 && sprites->at(0x223).height == 22, "first sprite of bank 3");
    }
    check(ugh::loadGamePalette(exe).has_value(), "game palette loads");
}

void testWaterSurface(Checker& check, const ugh::ExeImage& exe)
{
    const auto surface = ugh::decodeWaterSurface(exe);
    check(surface.has_value(), "water surface routines decode");
    if (!surface) {
        return;
    }
    for (const auto& phase : *surface) {
        check(phase.size() == 320, "one write per pixel of the row");
        const bool coloursOk = std::ranges::all_of(phase, [](const ugh::SurfaceWrite& write) {
            return write.color >= 0x54 && write.color <= 0x58 && write.offset < 80 &&
                   (write.planeMask == 1 || write.planeMask == 2 || write.planeMask == 4 || write.planeMask == 8);
        });
        check(coloursOk, "surface colours 0x54..0x58, one plane at a time");
    }
}

std::vector<std::uint8_t> bytesOf(std::string_view text)
{
    return {text.begin(), text.end()};
}

void testSha256(Checker& check)
{
    // FIPS 180-4 examples, plus a message whose padding needs a second block and one of exactly 64 bytes.
    check(ugh::toHex(ugh::sha256({})) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "SHA-256 of nothing");
    check(ugh::toHex(ugh::sha256(bytesOf("abc"))) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 of abc");
    check(ugh::toHex(ugh::sha256(bytesOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))) ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "SHA-256 of a 56-byte message");
    check(ugh::toHex(ugh::sha256(std::vector<std::uint8_t>(64, 'a'))) ==
              "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb",
          "SHA-256 of exactly one block");
}

void testGameExeVerification(Checker& check, const char* path)
{
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> file{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    check(ugh::verifyGameExe(file).has_value(), "the original UGH.EXE is recognised");
    if (!file.empty()) {
        file[file.size() / 2] ^= 0x01U;
    }
    check(!ugh::verifyGameExe(file).has_value(), "a changed byte is rejected");
}

int run(std::span<char*> args)
{
    if (args.size() != 2) {
        std::println(stderr, "usage: ugh-tests <UGH.EXE>");
        return 2;
    }
    Checker check;
    testSha256(check);
    testGameExeVerification(check, args[1]);
    const auto exe = ugh::ExeImage::load(args[1]);
    check(exe.has_value(), "UGH.EXE loads");
    if (exe) {
        testIceBlocks(check, *exe);
        testIceRejectsCorruptInput(check, *exe);
        testPicturesAndSprites(check, *exe);
        testWaterSurface(check, *exe);
    }
    if (check.failures() == 0) {
        std::println("all data tests passed");
    }
    return check.failures() == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        return run(std::span(argv, static_cast<std::size_t>(argc)));
    } catch (const std::exception& e) {
        static_cast<void>(std::fputs("ugh-tests: fatal: ", stderr));
        static_cast<void>(std::fputs(e.what(), stderr));
        static_cast<void>(std::fputs("\n", stderr));
        return 1;
    } catch (...) {
        return 1;
    }
}
