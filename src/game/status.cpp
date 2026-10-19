// Status bar in the split-screen area below the play field (113b:3e79..4171).

#include "game/game.hpp"
#include "game/vars.hpp"

namespace ugh {
namespace {

constexpr int kFirstDigitSprite = 0xCC;
constexpr int kDigitAdvance = 6;
constexpr std::uint8_t kDigitsEnd = 0xFF;
constexpr std::uint16_t kFuelBarOffset = 0x130; // A000:0130, status area row 3
constexpr std::uint8_t kFuelBarColor = 0x32;
constexpr unsigned kFuelPerPixel = 700;         // DS:00c5
constexpr std::uint16_t kSinglePlaneMasks = 0x2614;
constexpr std::uint16_t kLeadingPlaneMasks = 0x2618;

/// 113b:3f43: tens and units of a byte, then the terminator.
void storeTwoDigits(DataSegment& data, std::uint16_t at, std::uint8_t value)
{
    data.setByte(at, static_cast<std::uint8_t>(value / 10));
    data.setByte(static_cast<std::uint16_t>(at + 1), static_cast<std::uint8_t>(value % 10));
    data.setByte(static_cast<std::uint16_t>(at + 2), kDigitsEnd);
}

/// 113b:3f2f: thousands (16-bit quotient, not limited to one digit), hundreds, tens, units.
void storeFourDigits(DataSegment& data, std::uint16_t at, std::uint16_t value)
{
    data.setByte(at, static_cast<std::uint8_t>(value / 1000));
    const auto belowThousand = static_cast<std::uint16_t>(value % 1000);
    data.setByte(static_cast<std::uint16_t>(at + 1), static_cast<std::uint8_t>(belowThousand / 100));
    storeTwoDigits(data, static_cast<std::uint16_t>(at + 2), static_cast<std::uint8_t>(belowThousand % 100));
}

std::uint32_t joinWords(std::int16_t high, std::int16_t low)
{
    return (static_cast<std::uint32_t>(static_cast<std::uint16_t>(high)) << 16U) | static_cast<std::uint16_t>(low);
}

} // namespace

/// 113b:3f0f: six digits of a 32-bit value (hundred-thousands counted by repeated subtraction).
void Game::storeSixDigits(DataSegment& data, std::uint16_t at, std::uint32_t value)
{
    data.setByte(at, static_cast<std::uint8_t>(value / 100000U));
    const std::uint32_t belowHundredThousand = value % 100000U;
    data.setByte(static_cast<std::uint16_t>(at + 1), static_cast<std::uint8_t>(belowHundredThousand / 10000U));
    storeFourDigits(data, static_cast<std::uint16_t>(at + 2), static_cast<std::uint16_t>(belowHundredThousand % 10000U));
}

void Game::drawStatusFrame()
{
    const bool team = data_.get(var::kPlayerCount) == 2;
    video_.drawSprite(team ? 0xB0 : 0xAE, 0, 0xC1);
    video_.drawSprite(team ? 0xB1 : 0xAF, 0xFF, 0xC1);
}

void Game::drawChangedDigits()
{
    for (std::uint16_t i = 0;; ++i) {
        const std::uint8_t digit = data_.byte(static_cast<std::uint16_t>(var::kDigitsNew + i));
        if (digit == kDigitsEnd) {
            return;
        }
        if (digit != data_.byte(static_cast<std::uint16_t>(var::kDigitsOld + i))) {
            video_.drawSprite(kFirstDigitSprite + digit, data_.get(var::kStatusCursorX), data_.get(var::kStatusY));
        }
        data_.set(var::kStatusCursorX, data_.get(var::kStatusCursorX) + kDigitAdvance);
    }
}

void Game::drawTwoDigits(std::uint8_t value, std::uint8_t shown)
{
    storeTwoDigits(data_, var::kDigitsNew, value);
    storeTwoDigits(data_, var::kDigitsOld, shown);
    drawChangedDigits();
}

void Game::drawFourDigits(std::uint16_t value, std::uint16_t shown)
{
    storeFourDigits(data_, var::kDigitsNew, value);
    storeFourDigits(data_, var::kDigitsOld, shown);
    drawChangedDigits();
}

void Game::drawSixDigits(std::uint32_t value, std::uint32_t shown)
{
    storeSixDigits(data_, var::kDigitsNew, value);
    storeSixDigits(data_, var::kDigitsOld, shown);
    drawChangedDigits();
}

void Game::updateStatusBar()
{
    // At most one of score / player icons / tip is redrawn per frame; the fuel bar always follows.
    data_.set(var::kStatusY, 0xC2);

    const std::uint8_t lives = data_.get(var::kLives);
    const std::uint8_t livesShown = data_.get(var::kLivesShown);
    if (lives != livesShown) {
        data_.set(var::kLivesShown, lives);
        data_.set(var::kStatusCursorX, data_.get(var::kLivesX));
        drawTwoDigits(lives, livesShown);
    }

    std::uint32_t score = joinWords(data_.get(var::kScoreHigh), data_.get(var::kScoreLow));
    if (score > 999999U) {
        score = 999999U;
        data_.set(var::kScoreLow, static_cast<int>(score & 0xFFFFU));
        data_.set(var::kScoreHigh, static_cast<int>(score >> 16U));
    }
    const std::uint32_t scoreShown = joinWords(data_.get(var::kScoreShownHigh), data_.get(var::kScoreShownLow));
    if (score != scoreShown) {
        data_.set(var::kScoreShownLow, static_cast<int>(score & 0xFFFFU));
        data_.set(var::kScoreShownHigh, static_cast<int>(score >> 16U));
        data_.set(var::kStatusCursorX, data_.get(var::kScoreX));
        drawSixDigits(score, scoreShown);
        updateFuelBar();
        return;
    }

    const std::uint8_t deliveries = data_.get(var::kDeliveries);
    const std::uint8_t deliveriesShown = data_.get(var::kDeliveriesShown);
    if (deliveries != deliveriesShown) {
        data_.set(var::kDeliveriesShown, deliveries);
        data_.set(var::kStatusCursorX, data_.get(var::kDeliveriesX));
        drawTwoDigits(deliveries, deliveriesShown);
    }

    if (data_.get(var::kPlayerIcon, 0) != data_.get(var::kPlayerIconShown, 0)) {
        data_.set(var::kPlayerIconShown, 0, data_.get(var::kPlayerIcon, 0));
        video_.drawSprite(data_.get(var::kPlayerIcon, 0) + 0xB2, data_.get(var::kPlayer1IconX), 0xC1);
        video_.drawSprite(data_.get(var::kPlayerIcon2, 0) + 0xB7, data_.get(var::kPlayer1Icon2X), 0xC1);
        updateFuelBar();
        return;
    }
    const bool team = data_.get(var::kPlayerCount) == 2;
    if (team && data_.get(var::kPlayerIcon, 1) != data_.get(var::kPlayerIconShown, 1)) {
        data_.set(var::kPlayerIconShown, 1, data_.get(var::kPlayerIcon, 1));
        video_.drawSprite(data_.get(var::kPlayerIcon, 1) + 0xBF, data_.get(var::kPlayer2IconX), 0xC1);
        video_.drawSprite(data_.get(var::kPlayerIcon2, 1) + 0xC4, data_.get(var::kPlayer2Icon2X), 0xC1);
        updateFuelBar();
        return;
    }

    data_.set(var::kStatusCursorX, data_.get(var::kTipX));
    int tipPlayer = -1;
    if (data_.get(var::kPlayerIcon, 0) != 0) {
        tipPlayer = 0;
    } else if (team && data_.get(var::kPlayerIcon, 1) != 0) {
        tipPlayer = 1;
    }
    if (tipPlayer >= 0 || data_.get(var::kTipRefresh) != 0) {
        const auto tip = static_cast<std::uint16_t>(tipPlayer >= 0 ? data_.get(var::kPlayerTip, tipPlayer) : 0);
        const auto tipShown = static_cast<std::uint16_t>(data_.get(var::kTipShown));
        if (tip != tipShown) {
            data_.set(var::kTipShown, tip);
            drawFourDigits(tip, tipShown);
        }
        data_.set(var::kTipRefresh, 0);
    }

    const std::uint8_t counter = data_.get(var::kMultiplier);
    const std::uint8_t counterShown = data_.get(var::kMultiplierShown);
    if (counter != counterShown) {
        data_.set(var::kMultiplierShown, counter);
        data_.set(var::kStatusCursorX, data_.get(var::kMultiplierX));
        drawTwoDigits(counter, counterShown);
    }
    updateFuelBar();
}

void Game::updateFuelBar()
{
    const int fuel = data_.get(var::kFuel);
    if (fuel < 0) {
        // Out of fuel: the attempt ends with a fade out.
        data_.set(var::kFuel, -1);
        if (data_.get(var::kFadeStep) >= 0) {
            data_.set(var::kFadeStep, -2);
            data_.set(var::kFadeLevel, data_.get(var::kFadeLevel) - 2);
        }
        return;
    }

    const auto length = static_cast<std::uint16_t>(static_cast<unsigned>(fuel) / kFuelPerPixel);
    const int shown = data_.get(var::kFuelBarShown);
    if (static_cast<std::int16_t>(length) > shown) {
        // Grow: the partial byte at the new end, then whole bytes back towards the old end.
        const auto last = static_cast<std::uint16_t>(length - 1);
        const std::uint8_t mask = data_.byte(static_cast<std::uint16_t>(kLeadingPlaneMasks + (last & 3U)));
        auto offset = static_cast<std::uint16_t>(kFuelBarOffset + (last >> 2U));
        video_.plotStatusColumn(offset, mask, kFuelBarColor);
        auto bytes = static_cast<std::uint16_t>((last >> 2U) - (static_cast<std::uint16_t>(shown) >> 2U));
        while (bytes != 0) {
            --offset;
            video_.plotStatusColumn(offset, 0x0F, kFuelBarColor);
            --bytes;
        }
    } else if (static_cast<std::int16_t>(length) < shown) {
        // Shrink: only the pixel column at the new end is cleared.
        const std::uint8_t mask = data_.byte(static_cast<std::uint16_t>(kSinglePlaneMasks + (length & 3U)));
        video_.plotStatusColumn(static_cast<std::uint16_t>(kFuelBarOffset + (length >> 2U)), mask, 0);
    }
    data_.set(var::kFuelBarShown, length);
}

} // namespace ugh
