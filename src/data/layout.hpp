#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

/// Where the assets live inside UGH.EXE. Segments are numbered as in the Ghidra analysis (load segment 0x1000).
namespace ugh::layout {

/// Raw 768-byte VGA palette used during play (far pointer at DS:00b3); only colours 0..127 are used.
inline constexpr std::uint16_t kGamePalette = 0x3BC6;

/// Sprite directory: 4 bytes per sprite (u16 offset, u8 width, u8 height), far pointer at CS:4628.
inline constexpr std::uint16_t kSpriteDirectory = 0x6B63;
inline constexpr std::size_t kSpriteDirectoryBytes = std::size_t{0x6C09 - 0x6B63} * 16;

/// Sprite pixel data is split across four segments by sprite number (113b:4268..428f).
struct SpriteBankLayout {
    std::uint16_t segment;
    std::uint16_t firstSprite;
    std::size_t bytes;
};
inline constexpr std::array<SpriteBankLayout, 4> kSpriteBanks{{
    {.segment = 0x2E1F, .firstSprite = 0x000, .bytes = std::size_t{0x47A7 - 0x2E1F} * 16},
    {.segment = 0x47A7, .firstSprite = 0x0D9, .bytes = std::size_t{0x538A - 0x47A7} * 16},
    {.segment = 0x538A, .firstSprite = 0x12A, .bytes = std::size_t{0x5E83 - 0x538A} * 16},
    {.segment = 0x5E83, .firstSprite = 0x223, .bytes = std::size_t{0x6B63 - 0x5E83} * 16},
}};

/// ICE!-packed full-screen pictures shown at start-up (113b:0bb8) and how many frames each is held.
struct IntroPictureLayout {
    std::uint16_t segment;
    std::uint16_t holdFrames;
};
inline constexpr std::array<IntroPictureLayout, 4> kIntroPictures{{
    {.segment = 0x3D9A, .holdFrames = 0x118}, // Play Byte logo
    {.segment = 0x3BF6, .holdFrames = 0x118}, // Bones Park
    {.segment = 0x3F9E, .holdFrames = 0x08C}, // "present"
    {.segment = 0x3FB7, .holdFrames = 0x1A4}, // UGH! title
}};

/// ICE!-packed level maps: 100 levels of 320 bytes (20x16 tiles), unpacked by 113b:0720.
inline constexpr std::uint16_t kLevels = 0x2A38;

/// The game's code segment; a few tables live in it.
inline constexpr std::uint16_t kCodeSegment = 0x113B;

/// Unrolled routines drawing the three phases of the animated water surface (jump table at CS:2db3).
inline constexpr std::array<std::uint16_t, 3> kWaterSurfaceCode{0x2E03, 0x31CD, 0x3597};

/// State of the random number generator (113b:4f09), four words; never seeded at run time.
inline constexpr std::uint16_t kRandomState = 0x4EF7;

/// The data segment (DGROUP).
inline constexpr std::uint16_t kDataSegment = 0x6C09;

/// Offsets inside the data segment.
namespace ds {

/// Text format (113b:077c): 0xFE centres the line, 0x0D ends a line (+16 px), 0xFD ends a half line (+8 px),
/// 0x00 ends the text.
inline constexpr std::uint16_t kCreditsText = 0x091F;       // pages separated by 0x00, terminated by 0xFF
inline constexpr std::uint16_t kMenuText = 0x0AE4;          // main menu with the password line
inline constexpr std::uint16_t kMenuDifficultySlot = 0x0B0D;
inline constexpr std::uint16_t kMenuPlayerModeSlot = 0x0B19;
inline constexpr std::uint16_t kPasswordLine = 0x0B4E;      // 20 characters shown under "PASSWORD:"
inline constexpr std::size_t kPasswordLength = 20;
inline constexpr std::uint16_t kControlsText = 0x0B64;
inline constexpr std::uint16_t kControlsPlayer1Slot = 0x0B73;
inline constexpr std::uint16_t kControlsPlayer2Slot = 0x0B8D;
inline constexpr std::uint16_t kHighScoreText = 0x0C7B;
inline constexpr std::uint16_t kHighScoreTable = 0x0C86;    // contents of UGH!.HI
inline constexpr std::size_t kHighScoreTableBytes = 0x6B;
inline constexpr std::uint16_t kHighScoreFileName = 0x0CF1;

inline constexpr std::uint16_t kLevelTitle = 0x0BF6;        // "LEVEL 00", digits at +7 / +8
inline constexpr std::uint16_t kLevelPasswordTitle = 0x0C00;
inline constexpr std::uint16_t kGameFailedText = 0x0C0C;     // "BAD LUCK / YOU FAILED"
inline constexpr std::uint16_t kNameEntryText = 0x0C23;      // "CONGRATULATIONS ... ENTER YOUR NAME:", then the name
inline constexpr std::uint16_t kNameEntryLine = 0x0C6D;      // the name being typed, last line of kNameEntryText
inline constexpr std::size_t kNameLength = 12;

/// High-score entries: the name at kHighScoreTable + n * stride, the six score characters (digits, leading
/// zeros as spaces) at kHighScoreScores + n * stride; the stride is the byte at kHighScoreStride (21).
inline constexpr std::uint16_t kHighScoreStride = 0x00C7;
inline constexpr std::uint16_t kHighScoreScores = 0x0C94;
inline constexpr int kHighScoreEntries = 5;

/// Per-mode password lists (word at kPasswordLists + 2 * (players - 1)): one NUL-terminated password per level,
/// the list ends with 0xFF.
inline constexpr std::uint16_t kPasswordLists = 0x00EA;

/// Word tables of string offsets.
inline constexpr std::uint16_t kDifficultyNames = 0x00DA;   // EASY, MEDIUM, HARD
inline constexpr std::uint16_t kPlayerModeNames = 0x00E0;   // ONE PLAYER, TEAM MODE
inline constexpr std::uint16_t kControlNames = 0x00E4;      // KEYBOARD, JOYSTICK A, JOYSTICK B

} // namespace ds

} // namespace ugh::layout
