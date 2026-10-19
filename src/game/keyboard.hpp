#pragma once

#include <cstdint>
#include <deque>
#include <optional>

namespace ugh {

/// The keyboard as the game sees it.
/// The host queues the bytes a PC keyboard controller would deliver (0xE0 prefix for extended keys, break =
/// make | 0x80). With the game's handler installed (113b:450a) each byte is taken by the interrupt routine
/// (113b:4567, see Game::serviceKeyboard), which stores it at CS:4509; 113b:44f1 polls that byte into
/// current / previous / changed. While the handler is unhooked (113b:454c, password entry) the BIOS buffers
/// ASCII characters instead (INT 16h).
class Keyboard {
public:
    struct Poll {
        std::uint8_t code; // last byte from the keyboard
        bool changed;      // differs from the previous poll
    };

    /// Host input: set-1 make code, whether it is an extended (0xE0) key, press/release, and the character the
    /// BIOS would deliver.
    void keyEvent(std::uint8_t makeCode, bool extended, bool pressed, std::optional<char> ascii)
    {
        if (!handlerInstalled_) {
            if (pressed && ascii) {
                bios_.push_back(*ascii);
            }
            return;
        }
        if (extended) {
            pending_.push_back(0xE0);
        }
        pending_.push_back(pressed ? makeCode : static_cast<std::uint8_t>(makeCode | 0x80U));
    }

    /// Next byte for the interrupt routine; it also becomes the byte seen by poll() (CS:4509).
    std::optional<std::uint8_t> nextInterruptByte()
    {
        if (pending_.empty()) {
            return std::nullopt;
        }
        lastByte_ = pending_.front();
        pending_.pop_front();
        return lastByte_;
    }

    /// 113b:44f1.
    Poll poll()
    {
        previous_ = current_;
        current_ = lastByte_;
        return {.code = current_, .changed = current_ != previous_};
    }

    /// 113b:450a / 113b:454c. Both empty the BIOS buffer.
    void setHandlerInstalled(bool installed)
    {
        handlerInstalled_ = installed;
        bios_.clear();
    }

    /// INT 16h, AH=1 then AH=0.
    std::optional<char> readBios()
    {
        if (bios_.empty()) {
            return std::nullopt;
        }
        const char next = bios_.front();
        bios_.pop_front();
        return next;
    }

private:
    bool handlerInstalled_ = true;
    std::deque<std::uint8_t> pending_;
    std::uint8_t lastByte_ = 0;
    std::uint8_t current_ = 0;
    std::uint8_t previous_ = 0;
    std::deque<char> bios_;
};

} // namespace ugh
