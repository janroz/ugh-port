#pragma once

#include "data/data_segment.hpp"
#include "sound/opl_chip.hpp"
#include "sound/pit_timers.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ugh {

/// Port of the game's sound library: the C interface in segment 1878 and the AdLib driver in segment 1664
/// (only the AdLib part is ever initialised; the Sound Blaster code is unused).
///
/// Songs and effects are "ADLX" files (AHDR / IMAP / TRCK chunks) with a MIDI-like event stream for the OPL
/// chip. Slot 12 plays music; slots 8..11 play effects on channels taken from the top of the chip, the music
/// keeps writing to those channels into shadow records meanwhile. The library's variables live at the start
/// of segment 1664 and are emulated byte for byte (`memory_`), so its quirks are kept.
///
/// Time advances only through render(): it runs the timer interrupts (sequencer at ~192 Hz, fades at ~10 Hz)
/// at their PIT rate and produces the matching OPL samples.
class AdlibDriver {
public:
    static constexpr std::uint16_t kSegment = 0x1664;

    AdlibDriver(DataSegment memory, std::vector<std::uint8_t> loadModule);

    /// 1878:0003 with flags = 1 (AdLib only), as called by the game at start-up (113b:4f43).
    void initialize();

    /// 1878:008a
    void shutdown();

    /// 1878:0f88: plays the ADLX song at segment:offset (endless loop).
    int playMusic(std::uint16_t segment, std::uint16_t offset, int fadeTicks, int volume);

    /// 1878:1059, first half: stops all effects; with fadeTicks the music fades and the caller must wait
    /// while fading() before calling finishStopMusic() (the original busy-waits on its timer interrupt).
    /// Returns true when finishStopMusic() is needed.
    bool beginStopMusic(int fadeTicks);
    [[nodiscard]] bool fading() const;
    void finishStopMusic();

    /// 1878:1852: plays an ADLX effect `loops` times (0 = until stopped); returns its slot (8..11) or -1.
    int playSound(std::uint16_t segment, std::uint16_t offset, int volume, int loops);

    /// 1878:1768
    void stopSound(int slot);

    /// Advances time by `frames` samples at OplChip::kSampleRate; fills `stereo` (2 * frames values) unless
    /// it is empty (the timers and sequencer still run).
    void render(std::size_t frames, std::span<std::int16_t> stereo);

private:
    // 1878 (C interface)
    int setMusicVolume(int volume);                                                   // 1878:1128
    int loadSlot(int slot, std::uint16_t segment, std::uint16_t offset, int loops);    // 1878:0aa9
    int loadSound(int slot, std::uint16_t segment, std::uint16_t offset, int volume, int loops); // 1878:0691
    int startSlot(int slot, int volume);                                              // 1878:0cfc
    void releaseChannels(int slot);                                                   // 1878:14bb

    // 1664 (driver)
    void writeRegister(std::uint8_t reg, std::uint8_t value);                         // 1664:11f2
    void setVolume(int channel, int volume);                                          // 1664:1211
    void setInstrument(int channel, std::uint16_t segment, std::uint16_t offset);     // 1664:1377
    void keyOff(int channel);                                                         // 1664:14b1
    void setFrequency(int channel, std::uint16_t value);                              // 1664:1503
    void setChannelLevel(int channel, int level);                                     // 1664:1571
    void silence();                                                                   // 1664:1a37
    bool detect();                                                                    // 1664:1a89
    void shutdownDevice();                                                            // 1664:1a65
    void startFadeTimer();                                                            // 1664:134e
    void stopFadeTimer();                                                             // 1664:1331
    void fadeTick();                                                                  // 1664:12e5
    void sequencerTick();                                                             // 1664:1939
    void runMusic();                                                                  // 1664:15d3
    void runSound(std::uint16_t record);                                              // 1664:176b

    [[nodiscard]] bool reservedForEffects(int channel) const;
    [[nodiscard]] std::uint8_t farByte(std::uint16_t segment, std::uint16_t offset) const;
    [[nodiscard]] std::uint16_t farWord(std::uint16_t segment, std::uint16_t offset) const;
    [[nodiscard]] std::uint16_t slotWord(int slot, std::uint16_t field) const;
    void setSlotWord(int slot, std::uint16_t field, int value);
    [[nodiscard]] std::uint16_t word(std::uint16_t offset) const { return memory_.word(offset); }
    void setWord(std::uint16_t address, int value) { memory_.setWord(address, static_cast<std::uint16_t>(value)); }
    [[nodiscard]] std::uint32_t dword(std::uint16_t offset) const;
    void setDword(std::uint16_t offset, std::uint32_t value);

    DataSegment memory_;                    // segment 1664
    std::vector<std::uint8_t> loadModule_;  // songs and effects are read from here
    OplChip chip_;
    PitTimers timers_;
    std::uint64_t pitPhase_ = 0;            // in 1 / OplChip::kSampleRate PIT ticks
    bool libraryReady_ = false;             // 2a37:0000
    bool libraryClosed_ = false;            // 2a37:0002
};

} // namespace ugh
