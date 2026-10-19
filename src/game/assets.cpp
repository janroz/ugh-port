#include "game/assets.hpp"

#include "data/ice.hpp"
#include "data/layout.hpp"

#include "core/byte_reader.hpp"

#include <format>

namespace ugh {

Result<WaterSurface> decodeWaterSurface(const ExeImage& exe)
{
    auto code = exe.segment(layout::kCodeSegment);
    if (!code) {
        return std::unexpected(code.error());
    }
    const ByteReader reader(*code);
    WaterSurface surface;
    for (std::size_t phase = 0; phase < layout::kWaterSurfaceCode.size(); ++phase) {
        // Register values set up by 113b:2df2..2df9: AX = 0x5401, BX = 0x5655, CX = 0x5857, DX = 0x03c5.
        std::array<std::uint8_t, 8> registers{0x01, 0x57, 0xC5, 0x55, 0x54, 0x58, 0x03, 0x56}; // AL CL DL BL AH CH DH BH
        std::uint8_t planeMask = 0x01;
        std::size_t position = layout::kWaterSurfaceCode.at(phase);
        auto& writes = surface.at(phase);
        while (true) {
            const auto opcode = reader.u8(position);
            if (!opcode) {
                return std::unexpected(opcode.error());
            }
            if (*opcode == 0xC3) {
                break;
            }
            if (*opcode == 0x1F) { // POP DS
                ++position;
                continue;
            }
            if (*opcode == 0xEE) { // OUT DX, AL
                planeMask = registers[0];
                ++position;
                continue;
            }
            if (*opcode == 0xD0 && reader.u8(position + 1) == 0xE0) { // SHL AL, 1
                registers[0] = static_cast<std::uint8_t>(registers[0] << 1U);
                position += 2;
                continue;
            }
            if (*opcode == 0xB0) { // MOV AL, imm8
                const auto value = reader.u8(position + 1);
                if (!value) {
                    return std::unexpected(value.error());
                }
                registers[0] = *value;
                position += 2;
                continue;
            }
            const auto modrm = reader.u8(position + 1);
            if (*opcode != 0x88 || !modrm || (*modrm & 0x07U) != 0x05) {
                return fail(std::format("unexpected instruction at 113b:{:04x}", position));
            }
            const unsigned mode = *modrm >> 6U;
            std::uint8_t offset = 0;
            if (mode == 1) {
                const auto displacement = reader.u8(position + 2);
                if (!displacement) {
                    return std::unexpected(displacement.error());
                }
                offset = *displacement;
            } else if (mode != 0) {
                return fail(std::format("unexpected addressing at 113b:{:04x}", position));
            }
            writes.push_back({.planeMask = planeMask, .offset = offset, .color = registers[(*modrm >> 3U) & 0x07U]});
            position += mode == 1 ? 3 : 2;
        }
    }
    return surface;
}

Result<GameAssets> loadGameAssets(const ExeImage& exe)
{
    std::vector<Picture> pictures;
    for (const auto& entry : layout::kIntroPictures) {
        auto picture = loadPicture(exe, entry.segment);
        if (!picture) {
            return std::unexpected(picture.error());
        }
        pictures.push_back(std::move(*picture));
    }
    auto sprites = loadSprites(exe);
    if (!sprites) {
        return std::unexpected(sprites.error());
    }
    auto palette = loadGamePalette(exe);
    if (!palette) {
        return std::unexpected(palette.error());
    }
    auto data = DataSegment::load(exe);
    if (!data) {
        return std::unexpected(data.error());
    }
    auto levelMaps = exe.segment(layout::kLevels).and_then(unpackIce);
    if (!levelMaps) {
        return std::unexpected(levelMaps.error());
    }
    auto waterSurface = decodeWaterSurface(exe);
    if (!waterSurface) {
        return std::unexpected(waterSurface.error());
    }
    std::array<std::uint16_t, 4> randomState{};
    for (std::size_t i = 0; i < randomState.size(); ++i) {
        const auto value = exe.segment(layout::kCodeSegment).and_then([&](auto code) {
            return ByteReader(code).u16le(layout::kRandomState + (2 * i));
        });
        if (!value) {
            return std::unexpected(value.error());
        }
        randomState.at(i) = *value;
    }
    auto soundData = DataSegment::fromSegment(exe, 0x1664);
    if (!soundData) {
        return std::unexpected(soundData.error());
    }
    const auto module = exe.loadModule();
    return GameAssets{.introPictures = std::move(pictures),
                      .sprites = std::move(*sprites),
                      .gamePalette = *palette,
                      .data = std::move(*data),
                      .levelMaps = std::move(*levelMaps),
                      .waterSurface = std::move(*waterSurface),
                      .randomState = randomState,
                      .soundData = std::move(*soundData),
                      .loadModule = std::vector<std::uint8_t>(module.begin(), module.end())};
}

} // namespace ugh
