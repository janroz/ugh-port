#pragma once

#include "data/data_segment.hpp"
#include "data/graphics.hpp"
#include "game/assets.hpp"
#include "game/keyboard.hpp"
#include "game/video.hpp"
#include "runtime/task.hpp"
#include "sound/adlib_driver.hpp"
#include "vga/vga.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace ugh {

/// Next state routine of a passenger (the target of the original's JMP), with the player register it passes.
struct PassengerJump {
    std::uint16_t handler;
    int player;
};
using PassengerStep = std::optional<PassengerJump>;

/// Next state routine of an enemy, with the rock (passenger slot) that hit it, if any.
struct EnemyJump {
    std::uint16_t handler;
    int rock;
};
using EnemyStep = std::optional<EnemyJump>;

/// Port of the game's control flow (segment 113b). Every member coroutine mirrors one routine of the
/// original; blocking waits on vertical retrace become `co_await` on the frame clock.
class Game {
public:
    Game(GameAssets assets, Vga& vga, Keyboard& keyboard, FrameClock& clock);

    /// 113b:0405: loads UGH!.HI from `directory` into the high-score text; a missing file keeps the built-in
    /// table. New records are saved back there (without this call they stay in memory only).
    void loadHighScores(const std::filesystem::path& directory);

    /// 113b:0008 from the start-up pictures on; completes when the player quits.
    Task<> run();

    /// 113b:4567: feeds the bytes received from the keyboard through the game's interrupt handler.
    /// The host calls this before every frame.
    void serviceKeyboard();

    /// The sound library; the host advances it every frame (AdlibDriver::render) and plays its output.
    [[nodiscard]] AdlibDriver& sound() { return sound_; }

    /// Read-only view of the game variables (tools, tests, comparisons with the original).
    [[nodiscard]] const DataSegment& data() const { return data_; }

private:
    /// Sign/zero results the original returns in the flags (JS = quit, JNZ = back to the menu).
    enum class Outcome : std::int8_t { Quit = -1, Continue = 0, Menu = 1 };

    struct HoldResult {
        std::uint8_t code; // scancode that ended the wait, 0x39 (space) on timeout
        bool keyPressed;
    };

    // Sound (game.cpp): the library calls the original makes, by data segment of the ADLX file.
    void playMusic(std::uint16_t segment);                       // 1878:0f88 (offset 0, no fade, volume 0xa5)
    int playSound(std::uint16_t segment, int volume, int loops); // 1878:1852
    Task<> stopMusic(int fadeTicks);                             // 1878:1059 (waits for the fade)

    // Front end (game.cpp).
    Task<> waitFrames(int count);                                 // 113b:44bb
    Task<HoldResult> holdForKey(int frames);                      // 113b:4497
    Task<> waitForKey();                                          // 113b:44db
    Task<Outcome> introPicture(std::size_t index, int holdFrames); // 113b:0b83
    Task<Outcome> intro();                                        // 113b:0bb8
    Task<> mainMenu();                                            // 113b:0063
    Task<> startGame();                                           // 113b:014b (F1)
    Task<Outcome> attractSequence();                              // 113b:0080..00aa
    Task<Outcome> firstDemo();                                    // 113b:088d
    Task<Outcome> secondDemo();                                   // 113b:08cf
    Task<Outcome> credits();                                      // 113b:07f6
    Task<Outcome> highScores();                                   // 113b:01b1
    Task<> controlOptions();                                      // 113b:05e6
    Task<> passwordEntry();                                       // 113b:04ca
    Task<> enterHighScore();                                      // 113b:0210
    void drawNameLine();                                          // 113b:01f1
    [[nodiscard]] bool scoreQualifies(int entry) const;           // 113b:0452
    void toggleHighScoreCipher();                                 // 113b:03cd
    void saveHighScores();                                        // 113b:03da

    void drawTitleBackground(); // 113b:06f7
    void drawMenu();            // 113b:0039
    void drawControls();        // 113b:05bb
    void cycleControl(std::size_t player);

    // Scripted sequences (sequence.cpp).
    Task<> closingSequence();                                    // 113b:08a2
    Task<> openingFlight();                                      // 113b:08c7
    Task<> sequenceScene(int waterline, std::uint16_t mapOffset, bool titleBackground); // 113b:072a
    Task<Outcome> playSequence(std::uint16_t actors);            // 113b:08d5
    void startActors(std::uint16_t actors);                      // 113b:0aeb
    void stepActors();                                           // 113b:09d7
    void runActorHandler(int actor, std::uint16_t handler);
    void clipSign(int actor);                                    // 113b:099f
    void eraseActors();                                          // 113b:0ab1
    void drawActors();                                           // 113b:0a58
    [[nodiscard]] bool actorsFinished() const;                   // 113b:09bc
    [[nodiscard]] static bool joystickButtonPressed();           // 113b:51ec (joysticks are not ported yet)

    // Playing (play.cpp).
    Task<> play();                         // 113b:0c07
    [[nodiscard]] int passwordLevel() const; // 113b:0c07..0c50
    Task<> startLevel();                   // 113b:3d66
    void loadLevel();                      // 113b:3976
    Task<> levelIntro();                   // 113b:0664
    void drawLevelMap();                   // 113b:4172
    void erasePlayer(int player);          // 113b:436d
    void drawPlayer(int player);           // 113b:418d
    Task<> pause();                        // 113b:0d85
    void setWaterline(int y);              // DS:2903, also used by sprite drawing

    // Copter physics (physics.cpp).
    void updatePlayer(int player);                          // 113b:1095
    [[nodiscard]] bool collides(std::uint16_t offset, int plane); // 113b:1457
    void loadPlatforms();                                   // 113b:3a67

    // Passengers, the rock and bonus items (passengers.cpp).
    void loadPassengers();                                   // 113b:3aa9
    void resetCharacterSprites();                            // 113b:0b4f
    void updatePassengers();                                 // 113b:1486
    void erasePassengers();                                  // 113b:0cb6
    void drawPassengers();                                   // 113b:0de7
    void drawPassengerBubbles();                             // 113b:0f54
    void runPassenger(int slot, std::uint16_t handler, int player);
    PassengerStep passengerRoutine(int slot, std::uint16_t handler, int player);
    [[nodiscard]] PassengerJump jumpPassenger(int slot, int field, int player) const;
    PassengerStep changePassengerType(int slot);
    void restartPassengerAnimation(int slot);
    [[nodiscard]] bool passengerAnimationTick(int slot);
    [[nodiscard]] std::int16_t passengerFrame(int slot, int listField);
    [[nodiscard]] std::int16_t typeWord(std::uint16_t type, int field) const;
    [[nodiscard]] std::uint16_t passengerType(int slot) const;
    [[nodiscard]] int platformWord(WordArray array, int platformOffset) const;
    [[nodiscard]] int playerTouchingPassenger(int slot) const; // 113b:2276
    [[nodiscard]] int emptyCopterOnPlatform(int platform) const;
    [[nodiscard]] int copterOnWater() const;
    PassengerStep passengerFallsInWater(int slot);
    PassengerStep passengerKnockedOff(int slot);
    void followWaterline(int slot);
    PassengerStep walkTowards(int slot, int player, bool moved);
    void boardCopter(int slot, int player);
    void reportUnknownRoutine(const char* kind, std::uint16_t handler);

    PassengerStep passengerStartTrip(int slot);         // 113b:149c
    PassengerStep passengerAwaitAppearance(int slot);   // 113b:1509
    PassengerStep passengerAppear(int slot);            // 113b:153b
    PassengerStep passengerAppearing(int slot);         // 113b:1582
    PassengerStep passengerStartWaiting(int slot);      // 113b:15b4
    PassengerStep passengerWaiting(int slot);           // 113b:15d7
    PassengerStep passengerStartCalling(int slot);      // 113b:16f6
    PassengerStep passengerCalling(int slot);           // 113b:172a
    PassengerStep passengerStartHailing(int slot);      // 113b:17e6
    PassengerStep passengerHailing(int slot);           // 113b:180a
    PassengerStep passengerStartWalkToCopter(int slot); // 113b:18c8
    PassengerStep passengerWalkToCopter(int slot);      // 113b:18e6
    PassengerStep swimmerBoard(int slot, int player);   // 113b:19e0
    PassengerStep passengerBoard(int slot, int player); // 113b:19fb
    PassengerStep passengerRiding(int slot);            // 113b:1a42
    PassengerStep passengerArrive(int slot, int player); // 113b:1a7e
    PassengerStep passengerWalkAway(int slot);          // 113b:1b29
    PassengerStep passengerStartVanishing(int slot);    // 113b:1bbe
    PassengerStep passengerVanishing(int slot);         // 113b:1bd6
    PassengerStep rockStartLying(int slot);             // 113b:1c0f
    PassengerStep rockLying(int slot);                  // 113b:1c27
    PassengerStep rockPickUp(int slot, int player);     // 113b:1c48
    PassengerStep rockCarried(int slot);                // 113b:1c6b
    PassengerStep rockDrop(int slot, int player);       // 113b:1c81
    PassengerStep rockFalling(int slot);                // 113b:1cee
    PassengerStep swimmerStart(int slot);               // 113b:1da8
    PassengerStep swimmerSurfacing(int slot);           // 113b:1dd5
    PassengerStep swimmerStartDrowning(int slot);       // 113b:1e9c
    PassengerStep swimmerDrowning(int slot);            // 113b:1ec0
    PassengerStep swimmerStartFloating(int slot);       // 113b:1f24
    PassengerStep swimmerFloating(int slot);            // 113b:1f43
    PassengerStep swimmerCalling(int slot);             // 113b:1fe2
    PassengerStep swimmerHailing(int slot);             // 113b:2068
    PassengerStep swimmerSwimToCopter(int slot);        // 113b:20c1

    void spawnItem(std::uint16_t type, int x, int y, int velocityX, int velocityY); // 113b:2b96
    void updateItems();                        // 113b:2b7f
    void itemFalling(int slot);                // 113b:2be4
    void itemLand(int slot);                   // 113b:2c97
    void itemLying(int slot);                  // 113b:2ca9
    [[nodiscard]] int playerTouchingItem(int slot) const; // 113b:2207
    void eraseItems();                         // 113b:0d44
    void drawItems();                          // 113b:0f0e

    // Enemies (enemies.cpp).
    void loadEnemies();                                      // 113b:3b1b
    void updateEnemies();                                    // 113b:2363
    void eraseEnemies();                                     // 113b:0d14
    void drawEnemies(bool behindCopters);                    // 113b:0e3e / 113b:0ead
    void runEnemy(int slot, std::uint16_t handler, int rock);
    EnemyStep enemyRoutine(int slot, std::uint16_t handler, int rock);
    [[nodiscard]] EnemyJump jumpEnemy(int slot, int field, int rock) const;
    [[nodiscard]] std::uint16_t enemyType(int slot) const;
    [[nodiscard]] std::int16_t enemyFrame(int slot, std::uint16_t list);
    [[nodiscard]] std::uint16_t enemyDirectionalList(int slot, int field) const;
    [[nodiscard]] int rockHittingEnemy(int slot) const;      // 113b:2196
    [[nodiscard]] int playerTouchingEnemy(int slot) const;   // 113b:22f1
    [[nodiscard]] int playerLandedOnEnemyPlatform(int slot) const;
    EnemyStep enemyHitByRock(int slot);
    void faceDinosaurTowards(int slot, int player);
    void addScore(std::uint16_t points);

    EnemyStep pterodactylStart(int slot);       // 113b:2379
    EnemyStep pterodactylWaiting(int slot);     // 113b:239f
    EnemyStep pterodactylCry(int slot);         // 113b:23b0
    EnemyStep pterodactylCrying(int slot);      // 113b:23d9
    EnemyStep pterodactylLaunch(int slot);      // 113b:23ea
    EnemyStep pterodactylFlying(int slot);      // 113b:2493
    EnemyStep pterodactylHit(int slot);         // 113b:252b
    EnemyStep pterodactylFalling(int slot);     // 113b:255e
    EnemyStep dinosaurStartWalking(int slot);   // 113b:25b1
    EnemyStep dinosaurWalking(int slot);        // 113b:25c9
    EnemyStep dinosaurStartRoaring(int slot);   // 113b:2667
    EnemyStep dinosaurRoaring(int slot);        // 113b:2681
    EnemyStep dinosaurStartCharging(int slot);  // 113b:272e
    EnemyStep dinosaurCharging(int slot);       // 113b:2748
    EnemyStep dinosaurStartStopping(int slot);  // 113b:2830
    EnemyStep dinosaurStopping(int slot);       // 113b:2844
    EnemyStep dinosaurStartRecovering(int slot); // 113b:288f
    EnemyStep dinosaurRecovering(int slot);     // 113b:28a3
    EnemyStep dinosaurPlayOnce(int slot, int listField);
    EnemyStep dinosaurHit(int slot);            // 113b:28ee
    EnemyStep dinosaurStunned(int slot);        // 113b:2914
    EnemyStep blowerStart(int slot);            // 113b:295b
    EnemyStep blowerBlowing(int slot);          // 113b:2973
    EnemyStep blowerHit(int slot);              // 113b:2a53
    EnemyStep blowerStunned(int slot);          // 113b:2a76
    EnemyStep treeStart(int slot);              // 113b:2a87
    EnemyStep treeIdle(int slot);               // 113b:2ab5
    EnemyStep treeHit(int slot, int rock);      // 113b:2b0c
    EnemyStep treeShaking(int slot);            // 113b:2b58

    // Water and rain (water.cpp).
    [[nodiscard]] std::uint16_t random(std::uint16_t range); // 113b:4f09
    void updateWaterLevel();                                 // 113b:2d1c
    void drawWaterSurfaceFrame();                            // 113b:2db9
    void spawnRaindrop(int drop);                            // 113b:3c35
    void loadRain();                                         // 113b:3c04
    void updateRain();                                       // 113b:3c78
    void eraseRain();                                        // 113b:3cde
    void drawRain();                                         // 113b:3d19

    // Status bar (status.cpp).
    void drawStatusFrame();                                    // 113b:3e79
    void updateStatusBar();                                    // 113b:3f55
    void updateFuelBar();                                      // 113b:40ba
    void drawChangedDigits();                                  // 113b:3edd
    void drawTwoDigits(std::uint8_t value, std::uint8_t shown); // 113b:3eff
    void drawFourDigits(std::uint16_t value, std::uint16_t shown); // 113b:3ecf
    void drawSixDigits(std::uint32_t value, std::uint32_t shown);  // 113b:3ebd
    static void storeSixDigits(DataSegment& data, std::uint16_t at, std::uint32_t value); // 113b:3f0f

    Video video_;
    Keyboard* keyboard_;
    FrameClock* clock_;
    DataSegment data_;
    std::vector<Picture> introPictures_;
    std::vector<std::uint8_t> levelMaps_;
    WaterSurface waterSurface_;
    std::array<std::uint16_t, 4> randomState_; // CS:4ef7

    AdlibDriver sound_;
    std::array<bool, 2> joystickPresent_; // DS:3311 / DS:3313 (joystick detection is not ported yet)
    bool unknownRoutineReported_ = false;
    std::optional<std::filesystem::path> highScoreFile_; // where UGH!.HI is saved (the original: current directory)
};

} // namespace ugh
