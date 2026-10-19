#include "platform/keyboard.hpp"

#include <algorithm>
#include <array>

namespace ugh {
namespace {

struct Mapping {
    SDL_Scancode sdl;
    std::uint8_t makeCode;
    char ascii;
    bool extended;
};

constexpr Mapping key(SDL_Scancode sdl, std::uint8_t makeCode, char ascii = 0)
{
    return {.sdl = sdl, .makeCode = makeCode, .ascii = ascii, .extended = false};
}

/// Keys an enhanced keyboard sends with the 0xE0 prefix.
constexpr Mapping extendedKey(SDL_Scancode sdl, std::uint8_t makeCode, char ascii = 0)
{
    return {.sdl = sdl, .makeCode = makeCode, .ascii = ascii, .extended = true};
}

constexpr std::array kMappings{
    key(SDL_SCANCODE_ESCAPE, 0x01, '\x1b'), key(SDL_SCANCODE_1, 0x02, '1'),
    key(SDL_SCANCODE_2, 0x03, '2'),         key(SDL_SCANCODE_3, 0x04, '3'),
    key(SDL_SCANCODE_4, 0x05, '4'),         key(SDL_SCANCODE_5, 0x06, '5'),
    key(SDL_SCANCODE_6, 0x07, '6'),         key(SDL_SCANCODE_7, 0x08, '7'),
    key(SDL_SCANCODE_8, 0x09, '8'),         key(SDL_SCANCODE_9, 0x0A, '9'),
    key(SDL_SCANCODE_0, 0x0B, '0'),         key(SDL_SCANCODE_MINUS, 0x0C, '-'),
    key(SDL_SCANCODE_EQUALS, 0x0D, '='),    key(SDL_SCANCODE_BACKSPACE, 0x0E, '\b'),
    key(SDL_SCANCODE_TAB, 0x0F, '\t'),      key(SDL_SCANCODE_Q, 0x10, 'q'),
    key(SDL_SCANCODE_W, 0x11, 'w'),         key(SDL_SCANCODE_E, 0x12, 'e'),
    key(SDL_SCANCODE_R, 0x13, 'r'),         key(SDL_SCANCODE_T, 0x14, 't'),
    key(SDL_SCANCODE_Y, 0x15, 'y'),         key(SDL_SCANCODE_U, 0x16, 'u'),
    key(SDL_SCANCODE_I, 0x17, 'i'),         key(SDL_SCANCODE_O, 0x18, 'o'),
    key(SDL_SCANCODE_P, 0x19, 'p'),         key(SDL_SCANCODE_LEFTBRACKET, 0x1A, '['),
    key(SDL_SCANCODE_RIGHTBRACKET, 0x1B, ']'), key(SDL_SCANCODE_RETURN, 0x1C, '\r'),
    extendedKey(SDL_SCANCODE_KP_ENTER, 0x1C, '\r'), key(SDL_SCANCODE_LCTRL, 0x1D),
    extendedKey(SDL_SCANCODE_RCTRL, 0x1D),       key(SDL_SCANCODE_A, 0x1E, 'a'),
    key(SDL_SCANCODE_S, 0x1F, 's'),         key(SDL_SCANCODE_D, 0x20, 'd'),
    key(SDL_SCANCODE_F, 0x21, 'f'),         key(SDL_SCANCODE_G, 0x22, 'g'),
    key(SDL_SCANCODE_H, 0x23, 'h'),         key(SDL_SCANCODE_J, 0x24, 'j'),
    key(SDL_SCANCODE_K, 0x25, 'k'),         key(SDL_SCANCODE_L, 0x26, 'l'),
    key(SDL_SCANCODE_SEMICOLON, 0x27, ';'), key(SDL_SCANCODE_APOSTROPHE, 0x28, '\''),
    key(SDL_SCANCODE_GRAVE, 0x29, '`'),     key(SDL_SCANCODE_LSHIFT, 0x2A),
    key(SDL_SCANCODE_BACKSLASH, 0x2B, '\\'), key(SDL_SCANCODE_Z, 0x2C, 'z'),
    key(SDL_SCANCODE_X, 0x2D, 'x'),         key(SDL_SCANCODE_C, 0x2E, 'c'),
    key(SDL_SCANCODE_V, 0x2F, 'v'),         key(SDL_SCANCODE_B, 0x30, 'b'),
    key(SDL_SCANCODE_N, 0x31, 'n'),         key(SDL_SCANCODE_M, 0x32, 'm'),
    key(SDL_SCANCODE_COMMA, 0x33, ','),     key(SDL_SCANCODE_PERIOD, 0x34, '.'),
    key(SDL_SCANCODE_SLASH, 0x35, '/'),     key(SDL_SCANCODE_RSHIFT, 0x36),
    key(SDL_SCANCODE_KP_MULTIPLY, 0x37, '*'), key(SDL_SCANCODE_LALT, 0x38),
    key(SDL_SCANCODE_SPACE, 0x39, ' '),
    key(SDL_SCANCODE_CAPSLOCK, 0x3A),    key(SDL_SCANCODE_F1, 0x3B),
    key(SDL_SCANCODE_F2, 0x3C),          key(SDL_SCANCODE_F3, 0x3D),
    key(SDL_SCANCODE_F4, 0x3E),          key(SDL_SCANCODE_F5, 0x3F),
    key(SDL_SCANCODE_F6, 0x40),          key(SDL_SCANCODE_F7, 0x41),
    key(SDL_SCANCODE_F8, 0x42),          key(SDL_SCANCODE_F9, 0x43),
    key(SDL_SCANCODE_F10, 0x44),         key(SDL_SCANCODE_NUMLOCKCLEAR, 0x45),
    key(SDL_SCANCODE_SCROLLLOCK, 0x46),  key(SDL_SCANCODE_KP_7, 0x47, '7'),
    key(SDL_SCANCODE_KP_8, 0x48, '8'),      extendedKey(SDL_SCANCODE_UP, 0x48),
    key(SDL_SCANCODE_KP_9, 0x49, '9'),      key(SDL_SCANCODE_KP_MINUS, 0x4A, '-'),
    key(SDL_SCANCODE_KP_4, 0x4B, '4'),      extendedKey(SDL_SCANCODE_LEFT, 0x4B),
    key(SDL_SCANCODE_KP_5, 0x4C, '5'),      key(SDL_SCANCODE_KP_6, 0x4D, '6'),
    extendedKey(SDL_SCANCODE_RIGHT, 0x4D),       key(SDL_SCANCODE_KP_PLUS, 0x4E, '+'),
    key(SDL_SCANCODE_KP_1, 0x4F, '1'),      key(SDL_SCANCODE_KP_2, 0x50, '2'),
    extendedKey(SDL_SCANCODE_DOWN, 0x50),        key(SDL_SCANCODE_KP_3, 0x51, '3'),
    key(SDL_SCANCODE_KP_0, 0x52, '0'),      key(SDL_SCANCODE_KP_PERIOD, 0x53, '.'),
    key(SDL_SCANCODE_F11, 0x57),         key(SDL_SCANCODE_F12, 0x58),
    extendedKey(SDL_SCANCODE_KP_DIVIDE, 0x35, '/'), extendedKey(SDL_SCANCODE_HOME, 0x47),
    extendedKey(SDL_SCANCODE_PAGEUP, 0x49), extendedKey(SDL_SCANCODE_END, 0x4F),
    extendedKey(SDL_SCANCODE_PAGEDOWN, 0x51), extendedKey(SDL_SCANCODE_INSERT, 0x52),
    extendedKey(SDL_SCANCODE_DELETE, 0x53),
    // Mac keyboards have neither Insert nor (on laptops) a right Ctrl, player 1's fire keys. The right Option and
    // Command keys, which the game does not use, send right Ctrl instead.
    extendedKey(SDL_SCANCODE_RALT, 0x1D),        extendedKey(SDL_SCANCODE_RGUI, 0x1D),
};

} // namespace

std::optional<DosKey> toDosKey(SDL_Scancode key)
{
    const auto* const found = std::ranges::find(kMappings, key, &Mapping::sdl);
    if (found == kMappings.end()) {
        return std::nullopt;
    }
    return DosKey{.makeCode = found->makeCode,
                  .extended = found->extended,
                  .ascii = found->ascii != 0 ? std::optional<char>(found->ascii) : std::nullopt};
}

} // namespace ugh
