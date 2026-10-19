#pragma once

#include "core/error.hpp"
#include "data/data_segment.hpp"
#include "data/exe_image.hpp"
#include "data/graphics.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ugh {

/// One byte written by the water-surface routine: plane mask, byte offset in the row, colour.
struct SurfaceWrite {
    std::uint8_t planeMask;
    std::uint8_t offset;
    std::uint8_t color;
};
using WaterSurface = std::array<std::vector<SurfaceWrite>, 3>;

/// Everything loaded from UGH.EXE before the game starts.
struct GameAssets {
    std::vector<Picture> introPictures; // in layout::kIntroPictures order
    std::vector<Sprite> sprites;
    Palette gamePalette;
    DataSegment data;
    std::vector<std::uint8_t> levelMaps; // unpacked layout::kLevels
    WaterSurface waterSurface;
    std::array<std::uint16_t, 4> randomState;
    DataSegment soundData;                 // segment 1664: the sound library's variables
    std::vector<std::uint8_t> loadModule;  // songs and effects are read from here
};

/// Decodes the unrolled water-surface routines (MOV [DI+n], reg8 / MOV AL, imm / OUT DX, AL) into writes.
[[nodiscard]] Result<WaterSurface> decodeWaterSurface(const ExeImage& exe);

[[nodiscard]] Result<GameAssets> loadGameAssets(const ExeImage& exe);

} // namespace ugh
