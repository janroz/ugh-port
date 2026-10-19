#include "sound/adlib_driver.hpp"

#include <array>
#include <utility>

namespace ugh {
namespace {

// Library variables (offsets in segment 1664).
constexpr std::uint16_t kMusicTempo = 0x000C;          // sequencer period of the music, in PIT ticks
constexpr std::uint16_t kClearStart = 0x000C;
constexpr std::size_t kClearBytes = 0x488;              // 1664:112b clears 0x000c..0x0493 at start-up

// Slot records (13 x 0x2a bytes, field offsets for slot 0). Slots 8..11: effects, 12: music.
constexpr std::uint16_t kSlotStride = 0x2A;
constexpr std::uint16_t kSlotRecord = 0x14;             // record base used by the interpreters
constexpr std::uint16_t kSlotAgeLow = 0x14;             // compared to pick the effect to replace
constexpr std::uint16_t kSlotAgeHigh = 0x16;
constexpr std::uint16_t kSlotLoops = 0x18;
constexpr std::uint16_t kSlotStartOffset = 0x1A;
constexpr std::uint16_t kSlotStartSegment = 0x1C;
constexpr std::uint16_t kSlotLength = 0x1E;
constexpr std::uint16_t kSlotOffset = 0x24;
constexpr std::uint16_t kSlotSegment = 0x26;
constexpr std::uint16_t kSlotVolume = 0x30;
constexpr std::uint16_t kSlotNextTime = 0x32;           // 32 bits
constexpr std::uint16_t kSlotChannels = 0x36;
constexpr std::uint16_t kSlotFirstChannel = 0x38;
constexpr std::uint16_t kSlotTempoScale = 0x3A;         // 0x100 = the music's tempo
constexpr std::uint16_t kSlotFlags = 0x3C;
constexpr std::uint16_t kSlotPlaying = 1;
constexpr std::uint16_t kSlotLooping = 2;
constexpr std::uint16_t kSlotLoaded = 4;
constexpr int kFirstEffectSlot = 8;
constexpr int kLastEffectSlot = 11;
constexpr int kMusicSlot = 12;

// The same fields relative to a record base (as the interpreters address them).
constexpr std::uint16_t kRecordLoops = kSlotLoops - kSlotRecord;
constexpr std::uint16_t kRecordStartOffset = kSlotStartOffset - kSlotRecord;
constexpr std::uint16_t kRecordStartSegment = kSlotStartSegment - kSlotRecord;
constexpr std::uint16_t kRecordOffset = kSlotOffset - kSlotRecord;
constexpr std::uint16_t kRecordSegment = kSlotSegment - kSlotRecord;
constexpr std::uint16_t kRecordNextTime = kSlotNextTime - kSlotRecord;
constexpr std::uint16_t kRecordFirstChannel = kSlotFirstChannel - kSlotRecord;
constexpr std::uint16_t kRecordTempoScale = kSlotTempoScale - kSlotRecord;
constexpr std::uint16_t kRecordFlags = kSlotFlags - kSlotRecord;
constexpr std::uint16_t kFirstEffectRecord = kSlotRecord + (kFirstEffectSlot * kSlotStride); // 0x164
constexpr std::uint16_t kMusicRecord = kSlotRecord + (kMusicSlot * kSlotStride);             // 0x20c

// The music slot's fields at their absolute places.
constexpr std::uint16_t kMusicLoops = kMusicRecord + kRecordLoops;
constexpr std::uint16_t kMusicStartOffset = kMusicRecord + kRecordStartOffset;
constexpr std::uint16_t kMusicStartSegment = kMusicRecord + kRecordStartSegment;
constexpr std::uint16_t kMusicOffset = kMusicRecord + kRecordOffset;
constexpr std::uint16_t kMusicSegment = kMusicRecord + kRecordSegment;
constexpr std::uint16_t kMusicNextTime = kMusicRecord + kRecordNextTime;
constexpr std::uint16_t kMusicFlags = kMusicRecord + kRecordFlags;

constexpr std::uint16_t kMusicChannels = 0x022E;       // never written: 0 after start-up
constexpr std::uint16_t kBusy = 0x0236;
constexpr std::uint16_t kSongInfo = 0x0238;
constexpr std::uint16_t kTicks = 0x023A;               // 32 bits
constexpr std::uint16_t kMusicStartTime = 0x023E;      // 32 bits
constexpr std::uint16_t kFadeStep = 0x0246;
constexpr std::uint16_t kFadeLength = 0x0248;
constexpr std::uint16_t kFadeFrom = 0x024A;
constexpr std::uint16_t kFadeTo = 0x024C;
constexpr std::uint16_t kMusicActive = 0x0256;
constexpr std::uint16_t kFadeActive = 0x0258;
constexpr std::uint16_t kMusicVolume = 0x025A;
constexpr std::uint16_t kInstrumentMapOffset = 0x0266;
constexpr std::uint16_t kInstrumentMapSegment = 0x0268;
constexpr std::uint16_t kDevices = 0x026A;             // bit 0: AdLib
constexpr std::uint16_t kEffectChannels = 0x026C;      // channels taken by effects, from channel 8 down
constexpr std::uint16_t kCurrentSlot = 0x0274;

// OPL channel records (9 x 0x14 bytes) and the music's shadow copies of channels taken by effects.
constexpr std::uint16_t kChannelStride = 0x14;
constexpr std::uint16_t kChannelInstrument = 0x0278;   // 12 bytes: OPL register values
constexpr std::uint16_t kChannelVolume = 0x0284;
constexpr std::uint16_t kChannelLevel = 0x0286;
constexpr std::uint16_t kChannelOwner = 0x0288;
constexpr std::uint16_t kChannelFrequency = 0x028A;    // A0 / B0 register pair
constexpr std::uint16_t kShadowInstrument = 0x032C;
constexpr std::uint16_t kShadowVolume = 0x0338;
constexpr std::uint16_t kShadowLevel = 0x033A;
constexpr std::uint16_t kShadowFrequency = 0x033E;
constexpr std::size_t kInstrumentBytes = 12;

constexpr std::uint16_t kCurrentRecord = 0x10EF;
constexpr std::uint16_t kOperatorOffsets = 0x10F5;     // 9 bytes
constexpr std::uint16_t kSequencerTimer = 0x1122;
constexpr std::uint16_t kFadeTimer = 0x1124;
constexpr std::uint16_t kDeviceReady = 0x1126;

constexpr std::uint32_t kSequencerPeriod = 0x1843;     // ~192 Hz
constexpr std::uint32_t kFadePeriod = 0x1D1EB;         // ~10 Hz
constexpr PitTimers::Routine kSequencerRoutine = 0x1939;
constexpr PitTimers::Routine kFadeRoutine = 0x12E5;
constexpr int kChannels = 9;
constexpr std::uint8_t kKeyOn = 0x20;

std::uint16_t swapBytes(std::uint16_t value)
{
    return static_cast<std::uint16_t>((value >> 8U) | (value << 8U));
}

std::uint16_t channelField(std::uint16_t base, int index)
{
    return static_cast<std::uint16_t>(base + (index * kChannelStride));
}

} // namespace

AdlibDriver::AdlibDriver(DataSegment memory, std::vector<std::uint8_t> loadModule)
    : memory_(std::move(memory)), loadModule_(std::move(loadModule))
{
}

// ---------------------------------------------------------------------------------------------------------
// Helpers

std::uint8_t AdlibDriver::farByte(std::uint16_t segment, std::uint16_t offset) const
{
    if (segment == kSegment) {
        return memory_.byte(offset);
    }
    const std::size_t linear = (static_cast<std::size_t>(segment - 0x1000U) * 16) + offset;
    return linear < loadModule_.size() ? loadModule_[linear] : 0;
}

std::uint16_t AdlibDriver::farWord(std::uint16_t segment, std::uint16_t offset) const
{
    return static_cast<std::uint16_t>(farByte(segment, offset) |
                                      (farByte(segment, static_cast<std::uint16_t>(offset + 1)) << 8U));
}

std::uint16_t AdlibDriver::slotWord(int slot, std::uint16_t field) const
{
    return word(static_cast<std::uint16_t>(field + (slot * kSlotStride)));
}

void AdlibDriver::setSlotWord(int slot, std::uint16_t field, int value)
{
    setWord(static_cast<std::uint16_t>(field + (slot * kSlotStride)), value);
}

std::uint32_t AdlibDriver::dword(std::uint16_t offset) const
{
    return word(offset) | (static_cast<std::uint32_t>(word(static_cast<std::uint16_t>(offset + 2))) << 16U);
}

void AdlibDriver::setDword(std::uint16_t offset, std::uint32_t value)
{
    setWord(offset, static_cast<int>(value & 0xFFFFU));
    setWord(static_cast<std::uint16_t>(offset + 2), static_cast<int>(value >> 16U));
}

bool AdlibDriver::reservedForEffects(int channel) const
{
    // 8-bit arithmetic like the original: channels from (9 - taken) up belong to the effects.
    return std::cmp_greater_equal(channel, static_cast<std::uint8_t>(kChannels - memory_.byte(kEffectChannels)));
}

// ---------------------------------------------------------------------------------------------------------
// Time

void AdlibDriver::render(std::size_t frames, std::span<std::int16_t> stereo)
{
    const bool produce = stereo.size() >= frames * 2;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        pitPhase_ += PitTimers::kFrequency;
        while (true) {
            const auto threshold = std::uint64_t{timers_.interval()} * OplChip::kSampleRate;
            if (pitPhase_ < threshold) {
                break;
            }
            pitPhase_ -= threshold;
            for (const PitTimers::Routine routine : timers_.interrupt()) {
                if (routine == kSequencerRoutine) {
                    sequencerTick();
                } else if (routine == kFadeRoutine) {
                    fadeTick();
                }
            }
        }
        if (produce) {
            chip_.generate(stereo.subspan(frame * 2, 2));
        }
    }
}

// ---------------------------------------------------------------------------------------------------------
// 1878: C interface

void AdlibDriver::initialize()
{
    if (libraryReady_ && word(kDevices) != 0) {
        shutdown();
    }
    memory_.clear(kClearStart, kClearBytes);
    int devices = 0;
    if (detect()) {
        devices |= 1;
    }
    setWord(kDevices, devices);
    libraryReady_ = true;
}

void AdlibDriver::shutdown()
{
    if (libraryClosed_ && word(kDevices) == 0) {
        return;
    }
    setWord(kBusy, 1);
    stopFadeTimer();
    if ((word(kDevices) & 1U) != 0) {
        shutdownDevice();
    }
    setWord(kDevices, 0);
    libraryClosed_ = true;
}

int AdlibDriver::playMusic(std::uint16_t segment, std::uint16_t offset, int fadeTicks, int volume)
{
    if ((word(kDevices) & 1U) == 0) {
        return -1;
    }
    stopFadeTimer();
    if (loadSlot(kMusicSlot, segment, offset, 0) < 0) {
        return -1;
    }
    setWord(kMusicActive, 1);
    if (fadeTicks == 0) {
        static_cast<void>(startSlot(kMusicSlot, volume));
        static_cast<void>(setMusicVolume(volume));
    } else {
        static_cast<void>(startSlot(kMusicSlot, 0));
        setWord(kFadeFrom, 0);
        setWord(kFadeTo, volume);
        setWord(kFadeStep, 0);
        setWord(kFadeLength, fadeTicks);
        startFadeTimer();
    }
    setDword(kMusicStartTime, dword(kTicks));
    setWord(kMusicVolume, volume);
    return 1;
}

bool AdlibDriver::beginStopMusic(int fadeTicks)
{
    if ((word(kDevices) & 1U) == 0) {
        return false;
    }
    for (int slot = kFirstEffectSlot; slot <= kLastEffectSlot; ++slot) {
        stopSound(slot);
    }
    if (word(kMusicActive) == 0) {
        return false;
    }
    stopFadeTimer();
    if (fadeTicks == 0) {
        setWord(kMusicActive, 0);
        return false;
    }
    setWord(kFadeTo, 0);
    setWord(kFadeFrom, word(kMusicVolume));
    setWord(kFadeStep, 0);
    setWord(kFadeLength, fadeTicks);
    startFadeTimer();
    return true;
}

bool AdlibDriver::fading() const
{
    return word(kFadeActive) != 0;
}

void AdlibDriver::finishStopMusic()
{
    setWord(kMusicActive, 0);
    stopFadeTimer();
    setWord(kCurrentSlot, 0);
    silence();
}

int AdlibDriver::setMusicVolume(int volume)
{
    const std::uint16_t current = word(kCurrentSlot);
    int result = -1;
    if ((word(kDevices) & 1U) != 0 && word(kMusicActive) != 0) {
        setWord(kMusicVolume, volume);
        setWord(kCurrentSlot, kMusicSlot);
        // With the music channel count stuck at 0 this changes nothing audible (fades are silent).
        for (int channel = 0; std::cmp_less(channel, word(kMusicChannels)); ++channel) {
            setVolume(channel, volume);
        }
        result = 1;
    }
    setWord(kCurrentSlot, current);
    return result;
}

int AdlibDriver::loadSlot(int slot, std::uint16_t segment, std::uint16_t offset, int loops)
{
    if ((word(kDevices) & 1U) == 0 || slot >= 0xD || slot < kFirstEffectSlot) {
        return -1;
    }
    const auto tagAt = [&](std::uint16_t at, std::array<char, 4> tag) {
        for (std::size_t i = 0; i < tag.size(); ++i) {
            if (farByte(segment, static_cast<std::uint16_t>(at + i)) != static_cast<std::uint8_t>(tag.at(i))) {
                return false;
            }
        }
        return true;
    };
    if (!tagAt(offset, {'A', 'D', 'L', 'X'}) || !tagAt(static_cast<std::uint16_t>(offset + 8), {'A', 'H', 'D', 'R'})) {
        return -1;
    }
    const auto headerLength = swapBytes(farWord(segment, static_cast<std::uint16_t>(offset + 0xE)));
    const auto instruments = static_cast<std::uint16_t>(offset + headerLength + 0x10);
    if (!tagAt(instruments, {'I', 'M', 'A', 'P'})) {
        return -1;
    }
    const auto mapLength = swapBytes(farWord(segment, static_cast<std::uint16_t>(instruments + 6)));
    const auto track = static_cast<std::uint16_t>(instruments + mapLength + 8);
    const std::uint8_t channels = farByte(segment, static_cast<std::uint16_t>(offset + 0x12));
    if (!tagAt(track, {'T', 'R', 'C', 'K'}) ||
        (slot != kMusicSlot && channels > static_cast<std::uint16_t>(kChannels - word(kEffectChannels)))) {
        return -1;
    }
    if (slot == kMusicSlot) {
        setWord(kInstrumentMapOffset, instruments + 8);
        setWord(kInstrumentMapSegment, segment);
        setWord(kSongInfo, farWord(segment, static_cast<std::uint16_t>(offset + 0x16)));
    }
    setSlotWord(slot, kSlotTempoScale, 0x100);
    setSlotWord(slot, kSlotLength, swapBytes(farWord(segment, static_cast<std::uint16_t>(offset + 6))) - 0x22);
    setSlotWord(slot, kSlotChannels, channels);
    setSlotWord(slot, kSlotSegment, segment);
    setSlotWord(slot, kSlotOffset, track + 8);
    setSlotWord(slot, kSlotStartSegment, segment);
    setSlotWord(slot, kSlotStartOffset, track + 8);
    setSlotWord(slot, kSlotFlags, kSlotLoaded);
    if (loops == 0) {
        setSlotWord(slot, kSlotFlags, slotWord(slot, kSlotFlags) | kSlotLooping);
    } else {
        setSlotWord(slot, kSlotLoops, loops);
    }
    return 1;
}

int AdlibDriver::loadSound(int slot, std::uint16_t segment, std::uint16_t offset, int volume, int loops)
{
    if (slot < kFirstEffectSlot) {
        return -1;
    }
    stopSound(slot);
    // The original tests "!= 0", so a failed load (-1) also counts as success.
    if (loadSlot(slot, segment, offset, loops) != 0) {
        setSlotWord(slot, kSlotVolume, volume);
        return 1;
    }
    return -1;
}

int AdlibDriver::startSlot(int slot, int volume)
{
    // 1878:0cfc: take the channels and start at the next sequencer tick + 3.
    const std::uint16_t busy = word(kBusy);
    if ((word(kDevices) & 1U) == 0) {
        return -1;
    }
    setWord(kCurrentSlot, slot);
    if ((volume & 0xFF00) != 0) {
        volume = 0xFF;
    }
    const std::uint16_t flags = slotWord(slot, kSlotFlags);
    if (slot >= 0xD || slot < kFirstEffectSlot || (flags & kSlotLoaded) == 0 || (flags & kSlotPlaying) != 0) {
        return -1;
    }
    setWord(kBusy, 1);
    setSlotWord(slot, kSlotVolume, volume);
    if (slot < kMusicSlot) {
        stopSound(slot);
        const int taken = static_cast<std::int16_t>(word(kEffectChannels) + slotWord(slot, kSlotChannels));
        setWord(kEffectChannels, taken);
        if (taken > kChannels) {
            // The original leaves the busy flag set here (the sequencer would stop); it cannot happen with
            // this game's effects.
            setWord(kEffectChannels, taken - slotWord(slot, kSlotChannels));
            return -1;
        }
        setSlotWord(slot, kSlotFirstChannel, kChannels - word(kEffectChannels));
    }
    const std::uint16_t first = slotWord(slot, kSlotFirstChannel);
    const auto end = static_cast<std::uint16_t>(first + slotWord(slot, kSlotChannels));
    for (std::uint16_t channel = first; channel < end; ++channel) {
        if (channel < word(kMusicChannels)) {
            setWord(channelField(kShadowFrequency, channel), word(channelField(kChannelFrequency, channel)));
            for (std::uint16_t i = 0; i < kInstrumentBytes; ++i) {
                memory_.setByte(static_cast<std::uint16_t>(channelField(kShadowInstrument, channel) + i),
                                memory_.byte(static_cast<std::uint16_t>(channelField(kChannelInstrument, channel) + i)));
            }
            setWord(channelField(kShadowVolume, channel), word(kMusicVolume));
            setWord(channelField(kShadowLevel, channel), word(channelField(kChannelLevel, channel)));
        }
        setVolume(channel, 0);
        keyOff(channel);
        setWord(channelField(kChannelVolume, channel), volume);
    }
    setSlotWord(slot, kSlotFlags, slotWord(slot, kSlotFlags) | kSlotPlaying);
    const auto nextTime = static_cast<std::uint16_t>(kSlotNextTime + (slot * kSlotStride));
    setDword(nextTime, dword(kTicks) + 3);
    setWord(kBusy, busy);
    return 1;
}

int AdlibDriver::playSound(std::uint16_t segment, std::uint16_t offset, int volume, int loops)
{
    if ((word(kDevices) & 1U) == 0) {
        return -1;
    }
    for (int slot = kFirstEffectSlot; slot <= kLastEffectSlot; ++slot) {
        if ((slotWord(slot, kSlotFlags) & kSlotPlaying) == 0) {
            if (loadSound(slot, segment, offset, volume, loops) >= 0) {
                static_cast<void>(startSlot(slot, volume));
            }
            return slot;
        }
    }
    // All four busy: replace the one with the lowest age value.
    int victim = kFirstEffectSlot;
    for (int slot = kFirstEffectSlot; slot <= kLastEffectSlot; ++slot) {
        const std::uint16_t high = slotWord(slot, kSlotAgeHigh);
        const std::uint16_t victimHigh = slotWord(victim, kSlotAgeHigh);
        if (high <= victimHigh && (high < victimHigh || slotWord(slot, kSlotAgeLow) < slotWord(victim, kSlotAgeLow))) {
            victim = slot;
        }
    }
    stopSound(victim);
    if ((slotWord(victim, kSlotFlags) & kSlotPlaying) == 0 && loadSound(victim, segment, offset, volume, loops) >= 0 &&
        startSlot(victim, volume) >= 0) {
        return victim;
    }
    return -1;
}

void AdlibDriver::stopSound(int slot)
{
    const std::uint16_t busy = word(kBusy);
    const std::uint16_t flags = slot >= 0 && slot < 0xD ? slotWord(slot, kSlotFlags) : 0;
    if ((word(kDevices) & 1U) != 0 && slot >= kFirstEffectSlot && slot <= kLastEffectSlot &&
        (flags & kSlotLoaded) != 0 && (flags & kSlotPlaying) != 0) {
        setWord(kBusy, 1);
        setSlotWord(slot, kSlotFlags, 0);
        setSlotWord(slot, kSlotSegment, slotWord(slot, kSlotStartSegment));
        setSlotWord(slot, kSlotOffset, slotWord(slot, kSlotStartOffset));
        for (std::uint16_t i = 0; i < slotWord(slot, kSlotChannels); ++i) {
            keyOff(slotWord(slot, kSlotFirstChannel) + i);
        }
        releaseChannels(slot);
    }
    setWord(kBusy, busy);
}

void AdlibDriver::releaseChannels(int slot)
{
    // 1878:14bb: effects placed below the released one move up; the freed channels return to the music
    // (only those below the music channel count, i.e. none: they are silenced).
    const auto count = static_cast<std::int16_t>(slotWord(slot, kSlotChannels));
    const auto low = static_cast<std::int16_t>(kChannels - word(kEffectChannels));
    const auto high = static_cast<std::int16_t>(low + count);
    setSlotWord(slot, kSlotFlags, 0);
    setWord(kEffectChannels, word(kEffectChannels) - count);
    for (int other = kFirstEffectSlot; other < kMusicSlot; ++other) {
        if (other != slot && (slotWord(other, kSlotFlags) & kSlotLoaded) != 0 &&
            slotWord(other, kSlotFirstChannel) < slotWord(slot, kSlotFirstChannel)) {
            setSlotWord(other, kSlotFirstChannel, slotWord(other, kSlotFirstChannel) + count);
        }
    }

    if (slotWord(slot, kSlotFirstChannel) > static_cast<std::uint16_t>(low)) {
        for (auto channel = static_cast<std::int16_t>(slotWord(slot, kSlotFirstChannel) + count - 1); channel >= high;
             --channel) {
            const int from = channel - count;
            for (std::uint16_t i = 0; i < kInstrumentBytes; ++i) {
                memory_.setByte(static_cast<std::uint16_t>(channelField(kChannelInstrument, channel) + i),
                                memory_.byte(static_cast<std::uint16_t>(channelField(kChannelInstrument, from) + i)));
            }
            setWord(channelField(kChannelVolume, channel), word(channelField(kChannelVolume, from)));
            setWord(channelField(kChannelLevel, channel), word(channelField(kChannelLevel, from)));
            setWord(channelField(kChannelFrequency, channel), word(channelField(kChannelFrequency, from)));
            setWord(channelField(kChannelOwner, channel), word(channelField(kChannelOwner, from)));
            setWord(kCurrentSlot, word(channelField(kChannelOwner, channel)));
            const std::uint16_t frequency = word(channelField(kChannelFrequency, channel));
            keyOff(channel);
            setInstrument(channel, kSegment, channelField(kChannelInstrument, channel));
            setFrequency(channel, frequency);
        }
    }

    for (int channel = low; channel < high; ++channel) {
        if (word(kMusicChannels) > static_cast<std::uint16_t>(channel)) {
            setWord(channelField(kChannelVolume, channel), word(kMusicVolume));
            setWord(channelField(kChannelLevel, channel), word(channelField(kShadowLevel, channel)));
            setWord(kCurrentSlot, kMusicSlot);
            setWord(channelField(kChannelOwner, channel), kMusicSlot);
            keyOff(channel);
            setInstrument(channel, kSegment, channelField(kShadowInstrument, channel));
            setFrequency(channel, word(channelField(kShadowFrequency, channel)));
        } else {
            keyOff(channel);
            setVolume(channel, 0);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------
// 1664: AdLib driver

void AdlibDriver::writeRegister(std::uint8_t reg, std::uint8_t value)
{
    chip_.write(reg, value);
}

void AdlibDriver::setVolume(int channel, int volume)
{
    // 1664:1211: total level of the carrier (and of the modulator in additive mode) scaled by the channel
    // level and the volume, 8-bit arithmetic as in the original.
    if (channel < 0 || channel >= kChannels) {
        return;
    }
    const auto loudness = static_cast<std::uint8_t>(volume);
    if ((word(kMusicActive) & 1U) != 0 && word(kCurrentSlot) >= kMusicSlot && (word(kEffectChannels) & 0xFU) != 0 &&
        reservedForEffects(channel)) {
        memory_.setByte(channelField(kShadowVolume, channel), loudness);
        return;
    }
    memory_.setByte(channelField(kChannelVolume, channel), loudness);
    const std::uint8_t level = memory_.byte(channelField(kChannelLevel, channel));
    const std::uint8_t operatorOffset = memory_.byte(static_cast<std::uint16_t>(kOperatorOffsets + channel));
    const auto scaled = [&](std::uint8_t registerValue) {
        const auto keyScale = static_cast<std::uint8_t>(registerValue & 0xC0U);
        auto amount = static_cast<std::uint8_t>(0x3F - (registerValue & 0x3FU));
        amount = static_cast<std::uint8_t>((amount * level) >> 8U);
        amount = static_cast<std::uint8_t>((amount * loudness) >> 8U);
        return static_cast<std::uint8_t>(static_cast<std::uint8_t>(0x3F - amount) | keyScale);
    };
    const std::uint16_t instrument = channelField(kChannelInstrument, channel);
    writeRegister(static_cast<std::uint8_t>(0x43 + operatorOffset),
                  scaled(memory_.byte(static_cast<std::uint16_t>(instrument + 6))));
    if ((memory_.byte(static_cast<std::uint16_t>(instrument + 0xA)) & 1U) != 0) {
        writeRegister(static_cast<std::uint8_t>(0x40 + operatorOffset),
                      scaled(memory_.byte(static_cast<std::uint16_t>(instrument + 1))));
    }
}

void AdlibDriver::setInstrument(int channel, std::uint16_t segment, std::uint16_t offset)
{
    // 1664:1377: 11 register values (the record keeps 12 bytes).
    if (channel < 0 || channel >= kChannels) {
        return;
    }
    const bool shadow = word(kCurrentSlot) == kMusicSlot && (word(kEffectChannels) & 0xFU) != 0 &&
                        reservedForEffects(channel);
    std::array<std::uint8_t, kInstrumentBytes> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes.at(i) = farByte(segment, static_cast<std::uint16_t>(offset + i));
    }
    const std::uint16_t record = channelField(shadow ? kShadowInstrument : kChannelInstrument, channel);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        memory_.setByte(static_cast<std::uint16_t>(record + i), bytes.at(i));
    }
    if (shadow) {
        return;
    }
    auto operatorOffset = memory_.byte(static_cast<std::uint16_t>(kOperatorOffsets + channel));
    const auto at = [&](int base) { return static_cast<std::uint8_t>(base + operatorOffset); };
    writeRegister(at(0x20), bytes[0]);
    writeRegister(at(0x40), (bytes[10] & 1U) != 0 ? static_cast<std::uint8_t>(bytes[1] | 0x3FU) : bytes[1]);
    writeRegister(at(0x60), bytes[2]);
    writeRegister(at(0x80), bytes[3]);
    writeRegister(at(0xE0), bytes[4]);
    operatorOffset = static_cast<std::uint8_t>(operatorOffset + 3);
    writeRegister(at(0x20), bytes[5]);
    writeRegister(at(0x40), static_cast<std::uint8_t>(bytes[6] | 0x3FU));
    writeRegister(at(0x60), bytes[7]);
    writeRegister(at(0x80), bytes[8]);
    writeRegister(0x01, 0x20);
    writeRegister(at(0xE0), bytes[9]);
    writeRegister(static_cast<std::uint8_t>(0xC0 + channel), bytes[10]);
    setWord(channelField(kChannelOwner, channel), word(kCurrentSlot));
    setVolume(channel, word(channelField(kChannelVolume, channel)));
}

void AdlibDriver::keyOff(int channel)
{
    if (channel < 0 || channel >= kChannels) {
        return;
    }
    const auto high = static_cast<std::uint16_t>(channelField(kChannelFrequency, channel) + 1);
    const auto value = static_cast<std::uint8_t>(memory_.byte(high) & ~kKeyOn);
    memory_.setByte(high, value);
    writeRegister(static_cast<std::uint8_t>(0xB0 + channel), value);
}

void AdlibDriver::setFrequency(int channel, std::uint16_t value)
{
    if (channel < 0 || channel >= kChannels) {
        return;
    }
    setWord(channelField(kChannelFrequency, channel), value);
    writeRegister(static_cast<std::uint8_t>(0xA0 + channel), static_cast<std::uint8_t>(value & 0xFFU));
    writeRegister(static_cast<std::uint8_t>(0xB0 + channel), static_cast<std::uint8_t>(value >> 8U));
}

void AdlibDriver::setChannelLevel(int channel, int level)
{
    if (channel < 0 || channel >= kChannels) {
        return;
    }
    if (word(kCurrentSlot) >= kMusicSlot && (word(kEffectChannels) & 0xFU) != 0 && reservedForEffects(channel)) {
        memory_.setByte(channelField(kShadowLevel, channel), static_cast<std::uint8_t>(level));
        return;
    }
    memory_.setByte(channelField(kChannelLevel, channel), static_cast<std::uint8_t>(level));
    setVolume(channel, word(channelField(kChannelVolume, channel)));
}

void AdlibDriver::silence()
{
    writeRegister(0x01, 0x00);
    writeRegister(0x01, 0x20);
    for (int channel = 0; channel < kChannels; ++channel) {
        const std::uint8_t operatorOffset = memory_.byte(static_cast<std::uint16_t>(kOperatorOffsets + channel));
        writeRegister(static_cast<std::uint8_t>(0x40 + operatorOffset), 0x3F);
        writeRegister(static_cast<std::uint8_t>(0x43 + operatorOffset), 0x3F);
    }
}

bool AdlibDriver::detect()
{
    // 1664:1a89: the OPL timer test always succeeds on the emulated chip.
    if (memory_.byte(kDeviceReady) != 0) {
        return true;
    }
    memory_.setByte(kDeviceReady, 1);
    writeRegister(0x01, 0x00);
    writeRegister(0x04, 0x60);
    writeRegister(0x04, 0x80);
    writeRegister(0x02, 0xFF);
    writeRegister(0x04, 0x21);
    writeRegister(0x04, 0x60);
    writeRegister(0x04, 0x80);
    silence();
    writeRegister(0x01, 0x20);
    timers_.install();
    setWord(kSequencerTimer, timers_.add(kSequencerPeriod, kSequencerRoutine));
    return true;
}

void AdlibDriver::shutdownDevice()
{
    if (memory_.byte(kDeviceReady) != 0) {
        timers_.remove(word(kSequencerTimer));
    }
    timers_.uninstall();
    memory_.setByte(kDeviceReady, 0);
    silence();
}

void AdlibDriver::startFadeTimer()
{
    if ((word(kFadeActive) & 1U) == 0) {
        setWord(kFadeTimer, timers_.add(kFadePeriod, kFadeRoutine));
        setWord(kFadeActive, 1);
    }
}

void AdlibDriver::stopFadeTimer()
{
    if ((word(kFadeActive) & 1U) != 0) {
        timers_.remove(word(kFadeTimer));
        setWord(kFadeActive, 0);
    }
}

void AdlibDriver::fadeTick()
{
    const auto step = static_cast<std::uint16_t>(word(kFadeStep) + 1);
    setWord(kFadeStep, step);
    const std::uint16_t length = word(kFadeLength);
    if (step > length) {
        timers_.remove(word(kFadeTimer));
        setWord(kFadeActive, 0);
        return;
    }
    const auto from = static_cast<std::int16_t>(word(kFadeFrom));
    const auto range = static_cast<std::int16_t>(word(kFadeTo) - from);
    const auto value = static_cast<std::int16_t>(
        (static_cast<std::int32_t>(range) * static_cast<std::int16_t>(step)) / static_cast<std::int16_t>(length));
    static_cast<void>(setMusicVolume(static_cast<std::int16_t>(value + from)));
}

void AdlibDriver::sequencerTick()
{
    setDword(kTicks, dword(kTicks) + 1);
    if ((word(kBusy) & 1U) != 0) {
        return;
    }
    setWord(kBusy, 1);
    if ((word(kMusicActive) & 1U) != 0 && dword(kTicks) >= dword(kMusicNextTime)) {
        setWord(kCurrentRecord, kMusicRecord);
        setWord(kCurrentSlot, kMusicSlot);
        runMusic();
    }
    if ((word(kEffectChannels) & 0xFU) != 0) {
        setWord(kCurrentSlot, kFirstEffectSlot);
        for (std::uint16_t record = kFirstEffectRecord; record < kMusicRecord; record += kSlotStride) {
            if ((memory_.byte(static_cast<std::uint16_t>(record + kRecordFlags)) & 5U) == 5 &&
                dword(kTicks) >= dword(static_cast<std::uint16_t>(record + kRecordNextTime))) {
                setWord(kCurrentRecord, record);
                runSound(record);
            }
            setWord(kCurrentSlot, word(kCurrentSlot) + 1);
        }
    }
    setWord(kBusy, 0);
}

void AdlibDriver::runMusic()
{
    // 1664:15d3: events until the next wait.
    const std::uint16_t segment = word(kMusicSegment);
    std::uint16_t offset = word(kMusicOffset);
    const auto next = [&] { return farByte(segment, offset++); };
    while (true) {
        const std::uint8_t command = next();
        const int channel = command & 0xF;
        switch (command >> 4U) {
        case 0x0: { // long wait
            const auto ticks = static_cast<std::uint16_t>(((channel << 8U) | next()) << 7U);
            setDword(kMusicNextTime, dword(kMusicNextTime) + ticks);
            setWord(kMusicOffset, offset);
            return;
        }
        case 0x1: // note: A0/B0 pair
            if (channel >= kChannels) {
                break;
            }
            {
                const std::uint16_t value = farWord(segment, offset);
                offset = static_cast<std::uint16_t>(offset + 2);
                if (reservedForEffects(channel)) {
                    setWord(channelField(kShadowFrequency, channel), value);
                } else {
                    setFrequency(channel, value);
                }
            }
            break;
        case 0x2: // note off
            if (channel >= kChannels) {
                break;
            }
            if (reservedForEffects(channel)) {
                setWord(channelField(kShadowFrequency, channel), word(channelField(kShadowFrequency, channel)) & 0xFFDF);
            } else {
                keyOff(channel);
            }
            break;
        case 0x3: // channel level
            if (channel < kChannels) {
                setChannelLevel(channel, farByte(segment, offset));
            }
            ++offset;
            break;
        case 0x4: // instrument
            if (channel < kChannels) {
                setInstrument(channel, segment, offset);
                offset = static_cast<std::uint16_t>(offset + 0xB);
            }
            break;
        case 0x5:
            break;
        case 0x6: { // tempo
            const std::uint16_t tempo = swapBytes(farWord(segment, offset));
            setWord(kMusicTempo, tempo);
            timers_.setPeriod(word(kSequencerTimer), tempo != 0 ? tempo : 0x10000U);
            offset = static_cast<std::uint16_t>(offset + 2);
            break;
        }
        case 0x7:
            if (command == 0x77) { // end of track
                if ((word(kMusicFlags) & kSlotLooping) == 0) {
                    const auto loops = static_cast<std::int16_t>(word(kMusicLoops) - 1);
                    setWord(kMusicLoops, loops);
                    if (loops <= 0) {
                        setWord(kMusicFlags, word(kMusicFlags) & ~kSlotPlaying);
                        return;
                    }
                }
                setWord(kMusicSegment, word(kMusicStartSegment));
                setWord(kMusicOffset, word(kMusicStartOffset));
                setDword(kMusicNextTime, dword(kTicks));
                setDword(kMusicStartTime, dword(kTicks));
                return;
            }
            {
                const std::uint8_t length = next();
                offset = static_cast<std::uint16_t>(offset + (channel == 0 ? 2 : length));
            }
            break;
        default: // short wait
            setDword(kMusicNextTime, dword(kMusicNextTime) + (command & 0x7FU));
            setWord(kMusicOffset, offset);
            return;
        }
    }
}

void AdlibDriver::runSound(std::uint16_t record)
{
    // 1664:176b: like runMusic, but on the effect's channels and with its tempo scale.
    const auto field = [&](std::uint16_t at) { return static_cast<std::uint16_t>(record + at); };
    const std::uint16_t segment = word(field(kRecordSegment));
    std::uint16_t offset = word(field(kRecordOffset));
    const auto next = [&] { return farByte(segment, offset++); };
    const auto scaledWait = [&](std::uint32_t ticks) {
        const std::uint32_t scaled = (ticks * word(field(kRecordTempoScale))) >> 8U;
        setDword(field(kRecordNextTime), dword(field(kRecordNextTime)) + scaled);
        setWord(field(kRecordOffset), offset);
    };
    while (true) {
        const std::uint8_t command = next();
        const int channel = static_cast<std::uint8_t>((command & 0xFU) + memory_.byte(field(kRecordFirstChannel)));
        switch (command >> 4U) {
        case 0x0:
            scaledWait(static_cast<std::uint16_t>((((command & 0xFU) << 8U) | next()) << 7U));
            return;
        case 0x1:
            if (channel < kChannels) {
                setFrequency(channel, farWord(segment, offset));
                offset = static_cast<std::uint16_t>(offset + 2);
            }
            break;
        case 0x2:
            if (channel < kChannels) {
                keyOff(channel);
            }
            break;
        case 0x3:
            if (channel < kChannels) {
                setChannelLevel(channel, farByte(segment, offset));
            }
            ++offset;
            break;
        case 0x4:
            if (channel <= kChannels) {
                setWord(channelField(kChannelOwner, channel), word(kCurrentSlot));
                setInstrument(channel, segment, offset);
                offset = static_cast<std::uint16_t>(offset + 0xB);
            }
            break;
        case 0x5:
            break;
        case 0x6: {
            const std::uint16_t musicTempo = word(kMusicTempo);
            if ((word(kMusicActive) & 1U) != 0 || musicTempo != 0) {
                // Relative to the music's tempo. (The original faults on a zero tempo or an overflowing
                // quotient; neither happens with this game's data.)
                const std::uint32_t dividend =
                    (std::uint32_t{farByte(segment, offset)} << 16U) |
                    (std::uint32_t{farByte(segment, static_cast<std::uint16_t>(offset + 1))} << 8U);
                if (musicTempo != 0 && dividend / musicTempo <= 0xFFFF) {
                    setWord(field(kRecordTempoScale), static_cast<int>(dividend / musicTempo));
                }
            } else {
                const std::uint16_t tempo = swapBytes(farWord(segment, offset));
                setWord(kMusicTempo, tempo);
                timers_.setPeriod(word(kSequencerTimer), tempo != 0 ? tempo : 0x10000U);
            }
            offset = static_cast<std::uint16_t>(offset + 2);
            break;
        }
        case 0x7:
            if (command == 0x77) {
                if ((word(field(kRecordFlags)) & kSlotLooping) == 0) {
                    const auto loops = static_cast<std::int16_t>(word(field(kRecordLoops)) - 1);
                    setWord(field(kRecordLoops), loops);
                    if (loops <= 0) {
                        stopSound(word(kCurrentSlot));
                        return;
                    }
                }
                setWord(field(kRecordSegment), word(field(kRecordStartSegment)));
                setWord(field(kRecordOffset), word(field(kRecordStartOffset)));
                return;
            }
            {
                const std::uint8_t length = next();
                offset = static_cast<std::uint16_t>(offset + ((command & 0xFU) == 0 ? 2 : length));
            }
            break;
        default:
            scaledWait(command & 0x7FU);
            return;
        }
    }
}

} // namespace ugh
