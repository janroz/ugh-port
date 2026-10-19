#pragma once

#include "data/data_segment.hpp"

/// Game variables in the original data segment, named by how the code uses them.
/// Names ending in an address mark variables whose meaning is not established yet.
namespace ugh::var {

// Game setup (menu).
inline constexpr WordVar kPlayerCount{0x2634};     // 1 or 2 (team mode)
inline constexpr WordVar kPlayerCountTimes2{0x2636};
inline constexpr WordVar kDifficulty{0x2638};      // 0..2
inline constexpr WordArray kControl{0x263F};        // per player: -1 keyboard, 0 / 1 joystick A / B

// Progress.
inline constexpr WordVar kLevel{0x261C};            // 0-based
inline constexpr WordVar kScoreLow{0x261E};         // score, 32 bits
inline constexpr WordVar kScoreHigh{0x2620};
inline constexpr WordVar kFuel{0x2622};             // < 0 ends the attempt
inline constexpr WordArray kLevelCount{0x2624};     // [players - 1]: 69 / 81
inline constexpr WordArray kCrashThreshold{0x262E}; // [difficulty]
inline constexpr ByteVar kLives{0x263C};
inline constexpr ByteVar kMultiplier{0x263D};       // score multiplier: 1 at game start and after a lost life

/// 113b:3d66 zeroes this range at every level start.
inline constexpr std::uint16_t kLevelStateStart = 0x2648;
inline constexpr std::size_t kLevelStateBytes = std::size_t{0xC4} * 2;

// Controls per player (word; keyboard handler writes the low byte, joysticks the whole word): nonzero = held.
inline constexpr WordArray kUp{0x278C};
inline constexpr WordArray kDown{0x2790};
inline constexpr WordArray kLeft{0x2794};
inline constexpr WordArray kRight{0x2798};
inline constexpr WordArray kFire{0x279C};

/// Keyboard handler (113b:4567): 6-byte entries {sequence[3] (0xFF-terminated), target offset from kUp,
/// value, still-matching flag}, list terminated by 0xFF; kKeySequencePosition counts bytes of a sequence.
inline constexpr std::uint16_t kKeyTable = 0x281C;
inline constexpr ByteVar kKeySequencePosition{0x2647};

// Level fade: level 0..256 (+step each frame while <= 256); negative ends the level.
inline constexpr WordVar kFadeLevel{0x27A8};
inline constexpr WordVar kFadeStep{0x27AA};

// Status bar (drawn in the split-screen area).
inline constexpr WordVar kFuelBarShown{0x27A6};     // bar length in pixels
inline constexpr WordVar kStatusCursorX{0x27AC};
inline constexpr WordVar kStatusY{0x27AE};
inline constexpr WordVar kLivesX{0x27B0};
inline constexpr WordVar kScoreX{0x27B2};
inline constexpr WordVar kDeliveriesX{0x27B4};
inline constexpr WordVar kPlayer1IconX{0x27B6};
inline constexpr WordVar kPlayer1Icon2X{0x27B8};
inline constexpr WordVar kTipX{0x27BA};
inline constexpr WordVar kMultiplierX{0x27BC};
inline constexpr WordVar kPlayer2IconX{0x27BE};
inline constexpr WordVar kPlayer2Icon2X{0x27C0};
inline constexpr WordVar kScoreShownLow{0x27C2};
inline constexpr WordVar kScoreShownHigh{0x27C4};
inline constexpr WordVar kTipShown{0x27C6};
inline constexpr WordVar kUnknown27c8{0x27C8};
inline constexpr ByteVar kMultiplierShown{0x27CA};
inline constexpr ByteVar kLivesShown{0x27CB};
inline constexpr ByteVar kDeliveriesShown{0x27CC};
inline constexpr ByteVar kTipRefresh{0x27CD};
inline constexpr ByteVar kLevelCompleted{0x27CF}; // sign bit set when the level was finished

// Players (index 0 / 1).
inline constexpr WordArray kPlayerX{0x27D0};        // position * 32
inline constexpr WordArray kPlayerY{0x27D4};
inline constexpr WordArray kPlayerScreenX{0x27D8};
inline constexpr WordArray kPlayerScreenY{0x27DC};
inline constexpr WordArray kPlayerDrawnX{0x27E0};
inline constexpr WordArray kPlayerDrawnY{0x27E4};
inline constexpr WordArray kPlayerSprite{0x27E8};
inline constexpr WordArray kPlayerDrawnSprite{0x27EC};
inline constexpr WordArray kRotorTimer{0x27F0};
inline constexpr WordArray kRotorSpeed{0x27F4};
inline constexpr WordArray kLandedPlatform{0x27F8}; // platform index, -1 while flying
inline constexpr WordArray kPlayerIcon{0x27FC};     // status sprite 0xb2 + n (player 2: 0xbf + n)
inline constexpr WordArray kPlayerIconShown{0x2800};
inline constexpr WordArray kPlayerIcon2{0x2804};    // status sprite 0xb7 + n (player 2: 0xc4 + n)
inline constexpr WordArray kPlayerTip{0x2808};
inline constexpr WordArray kVelocityX{0x2810};      // 1/64 of position units (1/2048 pixel) per frame
inline constexpr WordArray kVelocityY{0x2814};
inline constexpr WordArray kImpact{0x2818};         // strongest bounce this frame; >= crash threshold crashes
inline constexpr WordArray kRotorFirstSprite{0x0CFA}; // [player]..[player + 1] is the rotor animation range

// Current level record (13 words copied from the level table by 113b:3976).
inline constexpr std::uint16_t kLevelRecord = 0x28E9;
inline constexpr std::size_t kLevelRecordBytes = 0x1A;
inline constexpr WordVar kMapOffset{0x28E9};        // into the unpacked level maps
inline constexpr WordVar kPlatformList{0x28EB};
inline constexpr WordVar kPassengerList{0x28ED};
inline constexpr WordVar kEnemyList{0x28EF};
inline constexpr ByteVar kDeliveries{0x28F1};       // shown in the status bar
inline constexpr WordVar kLevelName{0x28F3};
inline constexpr ByteVar kRain{0x28F5};
inline constexpr WordVar kPlayer1StartX{0x28F6};    // start positions * 32 (Amiga coordinates for y)
inline constexpr WordVar kPlayer1StartY{0x28F8};
inline constexpr WordVar kPlayer2StartX{0x28FA};
inline constexpr WordVar kPlayer2StartY{0x28FC};
inline constexpr WordVar kWater{0x28FE};            // water level * 32 (Amiga coordinates until converted)
inline constexpr WordVar kWaterSpeed{0x2900};
inline constexpr WordVar kWaterline{0x2903};        // water level in pixels
inline constexpr WordVar kWaterlineDrawn{0x2905};
inline constexpr WordVar kWaterlineOffset{0x2907};  // waterline * 96

/// Platforms of the level (113b:3a67), terminated by a negative left edge.
inline constexpr WordArray kPlatformLeft{0x290D};
inline constexpr WordArray kPlatformRight{0x2921};
inline constexpr WordArray kPlatformY{0x2935};      // landing height (copter y + 20)
inline constexpr WordArray kPlatform2949{0x2949};
inline constexpr WordArray kPlatform295d{0x295D};
inline constexpr WordArray kPlatformStopX{0x2971};  // passengers wait here
inline constexpr WordArray kPlatform2985{0x2985};
inline constexpr WordArray kPlatform2999{0x2999};

// Passengers (16 slots, list terminated by type -1). Fields marked "* 2" hold byte offsets into word arrays.
inline constexpr int kPassengerSlots = 16;
inline constexpr WordArray kPassengerType{0x29AD};        // type record in the data segment (handlers, sizes)
inline constexpr WordArray kPassengerStartPlatform{0x29CD};
inline constexpr WordArray kPassengerRoute{0x29ED};       // pairs {platform, wait}; advances by 4 per trip
inline constexpr WordArray kPassengerHandler{0x2A0D};     // state routine (code offset in segment 113b)
inline constexpr WordArray kPassengerTimer{0x2A2D};       // countdown, or the carrying player * 2
inline constexpr WordArray kPassengerPlatform{0x2A4D};
inline constexpr WordArray kPassengerDestination{0x2A6D};
inline constexpr WordArray kPassengerPlatformOffset{0x2A8D};    // platform * 2
inline constexpr WordArray kPassengerDestinationOffset{0x2AAD}; // destination * 2
inline constexpr WordArray kPassengerCountdown{0x2ACD};   // also horizontal speed while thrown
inline constexpr WordArray kPassengerX{0x2AED};           // * 32
inline constexpr WordArray kPassengerY{0x2B0D};
inline constexpr WordArray kPassengerScreenX{0x2B2D};
inline constexpr WordArray kPassengerDrawnX{0x2B4D};
inline constexpr WordArray kPassengerScreenY{0x2B6D};
inline constexpr WordArray kPassengerDrawnY{0x2B8D};
inline constexpr WordArray kPassengerAnimationStep{0x2BAD}; // byte offset into the current animation list
inline constexpr WordArray kPassengerAnimationTimer{0x2BCD};
inline constexpr WordArray kPassengerSprite{0x2BED};      // -1 = invisible
inline constexpr WordArray kPassengerDrawnSprite{0x2C0D};
inline constexpr WordArray kPassengerBubble{0x2C2D};      // speech bubble sprite, -1 = none
inline constexpr WordArray kPassengerDrawnBubble{0x2C4D};
inline constexpr WordArray kPassengerVelocityY{0x2C6D};
inline constexpr WordArray kPassengerQuickBonus{0x2C8D}; // frames left for the quick-delivery bonus

// Scripted sequences (113b:08d5: demos, opening flight, closing sequence) reuse the passenger arrays for up to
// 16 actors; the actor list ends at the first script pointer -1. A script is a word list: a sprite (>= 0)
// followed by signed byte steps dx, dy (* 4 in 1/32 px); a negative word -n starts a loop of n runs, 0x8000
// ends it (or the script, without an open loop).
inline constexpr WordArray kActorScript{0x2A0D};
inline constexpr WordArray kActorLoopStart{0x2A2D};
inline constexpr WordArray kActorFinished{0x2A4D};     // -1 once the script has ended
inline constexpr WordArray kActorLoopCount{0x2ACD};    // counts up to 0
inline constexpr WordArray kActorDelay{0x2BCD};        // frames until the next script step
inline constexpr WordArray kActorHandler{0x2C2D};      // routine called after each step (code offset in 113b)
inline constexpr WordArray kActorDelayReload{0x2C6D};
inline constexpr WordVar kSequenceResult{0x264A};      // -1 quit, 1 escape, 0 otherwise
inline constexpr int kInvisibleSprite = 0x7FFF;

// Bonus items (12 slots, free when the sprite is -1).
inline constexpr int kItemSlots = 12;
inline constexpr WordArray kItemType{0x2D6B};
inline constexpr WordArray kItemHandler{0x2D83};
inline constexpr WordArray kItemSprite{0x2D9B};
inline constexpr WordArray kItemDrawnSprite{0x2DB3};
inline constexpr WordArray kItemX{0x2DCB};
inline constexpr WordArray kItemY{0x2DE3};
inline constexpr WordArray kItemScreenX{0x2DFB};
inline constexpr WordArray kItemDrawnX{0x2E13};
inline constexpr WordArray kItemScreenY{0x2E2B};
inline constexpr WordArray kItemDrawnY{0x2E43};
inline constexpr WordArray kItemVelocityX{0x2E5B};        // also the lifetime once landed
inline constexpr WordArray kItemVelocityY{0x2E73};
inline constexpr std::uint16_t kBonusItemType = 0x7A38;

inline constexpr WordArray kMaxMultiplier{0x2628};        // [difficulty]
inline constexpr WordVar kMultiplierWord{0x263D};         // word read of kMultiplier (DS:263e is 0)
inline constexpr WordArray kPlayerTipMinimum{0x280C};

// Enemies (5 slots, list terminated by type -1).
inline constexpr int kEnemySlots = 5;
inline constexpr WordArray kEnemyType{0x2CAD};
inline constexpr WordArray kEnemyPlatform{0x2CB7};
inline constexpr WordArray kEnemyX{0x2CC1};               // * 32
inline constexpr WordArray kEnemyY{0x2CCB};
inline constexpr WordArray kEnemySequence{0x2CD5};        // animation list, or a tree's list of items to drop
inline constexpr WordArray kEnemyDelay{0x2CDF};           // pterodactyl: frames between attacks
inline constexpr WordArray kEnemyVelocityX{0x2CE9};
inline constexpr WordArray kEnemyHandler{0x2CF3};
inline constexpr WordArray kEnemyTimer{0x2CFD};           // also the dinosaur's charge acceleration
inline constexpr WordArray kEnemyScreenX{0x2D07};
inline constexpr WordArray kEnemyDrawnX{0x2D11};
inline constexpr WordArray kEnemyScreenY{0x2D1B};
inline constexpr WordArray kEnemyDrawnY{0x2D25};
inline constexpr WordArray kEnemyAnimationStep{0x2D2F};
inline constexpr WordArray kEnemyAnimationTimer{0x2D39};
inline constexpr WordArray kEnemySprite{0x2D43};
inline constexpr WordArray kEnemyDrawnSprite{0x2D4D};
inline constexpr WordArray kEnemyDirection{0x2D57};       // dinosaur: 0 left / 2 right; pterodactyl: target player
inline constexpr WordArray kEnemySound{0x2D61};           // handle of the looping wing sound

// Water and rain.
inline constexpr ByteVar kWaterRowPending{0x27A2};   // nonzero: copy the newly tinted row to the other page
inline constexpr ByteVar kSurfaceTimer{0x27A3};
inline constexpr WordVar kSurfacePhase{0x27A4};
inline constexpr ByteVar kWaterToggle{0x27CE};       // the water moves every second frame
inline constexpr int kRainDrops = 0xC1;
inline constexpr WordArray kRainOffset{0x2E8B};     // plane byte offset in the page
inline constexpr WordArray kRainDrawnOffset{0x300D};
inline constexpr WordArray kRainPlane{0x318F};

/// Per-mode tables of level-record offsets.
inline constexpr WordArray kLevelTables{0x2909};

/// The level map (20 x 16 tile sprite numbers) copied from the unpacked level data.
inline constexpr std::uint16_t kLevelMap = 0x264C;
inline constexpr int kMapColumns = 20;
inline constexpr int kMapRows = 16;

// Number formatting scratch (113b:3f0f..3f43): digits, terminated by 0xFF.
inline constexpr std::uint16_t kDigitsNew = 0x00C8;
inline constexpr std::uint16_t kDigitsOld = 0x00CF;

} // namespace ugh::var
