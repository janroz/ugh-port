# UGH! — a faithful C++ port of the 1992 DOS game

This project is an experiment with [Claude Code](https://claude.com/claude-code), Anthropic's AI coding agent:
the reverse engineering and the port were done by Claude Code, working with me. I know the original game well
and checked every step against it. It is not a remake or a rewrite "in the spirit of" the original. It is a
port: each routine of the original program was translated into C++, one by one, so the game behaves exactly as
it did in 1992.

*UGH!* (Play Byte, 1992) is a stone-age taxi game: you fly a pedal-powered helicopter, pick up
cave people waiting on ledges and drop them off where they want to go, while dinosaurs, rain and rising
water get in the way.

It is also a piece of software archaeology. The original `UGH.EXE` (Borland C++, 16-bit real mode, most
of the game written in hand-made assembly) was disassembled and decompiled routine by routine, and every
routine was ported to modern C++23 on top of [SDL 3](https://libsdl.org), keeping its logic, its data layout
and its original address in a comment. The goal is a port that plays
**exactly** like the original: the same physics, timing, random numbers, sounds and even the same bugs.
It has been played side by side with the original running in DOSBox-X, and every difference found that way
was traced back to the original code and fixed.

**No game data is included.** The port reads graphics, levels, texts, music and sounds from your own copy of
the original `UGH.EXE` at run time.

## What you need

- The DOS release of UGH!: `UGH.EXE`, 411 838 bytes, SHA-256
  `ef93d2cd5eb558f6a7d0007e0109f2e9ee952dc125389d7a6256646087636d7c`.
  Other versions (e.g. the Amiga original) are not supported, because the port reads data at fixed addresses
  inside this exact file.
- A C++23 compiler: Apple Clang 16 or newer (Xcode 16), Clang 18+, or GCC 14+. MSVC is not supported.
- [CMake](https://cmake.org) 3.28 or newer and [Ninja](https://ninja-build.org).
- **SDL 3** (3.2 or newer) with its CMake package.
- Git and network access during the first configure: CMake downloads
  [Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3) (the AdLib/OPL3 chip emulator, LGPL-2.1) at a pinned
  commit.

### Installing SDL 3

| System | Command |
|---|---|
| macOS (Homebrew) | `brew install sdl3 cmake ninja` |
| Debian 13 / Ubuntu 25.04 and newer | `sudo apt install libsdl3-dev cmake ninja-build` |
| Fedora | `sudo dnf install SDL3-devel cmake ninja-build` |
| Arch Linux | `sudo pacman -S sdl3 cmake ninja` |
| Anywhere else | build SDL 3 from source (see [SDL's installation guide](https://wiki.libsdl.org/SDL3/Installation)) and pass `-DCMAKE_PREFIX_PATH=/path/to/sdl3` to CMake |

The port is developed and tested on macOS. Linux should work; Windows (with Clang or MinGW) is untested.

## Building

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The default build type is `Debug`. It turns on AddressSanitizer, UndefinedBehaviorSanitizer and libc++
hardening, which makes the game noticeably slower. Use `Release` to play.

## Running

```sh
build/ugh                  # finds UGH.EXE on its own, or asks for it
build/ugh /path/to/UGH.EXE # use this file
```

On start the port looks for `UGH.EXE`:

1. at the path given on the command line;
2. in its data folder (see below), where it keeps its own copy;
3. as `UGH.EXE` or `ugh/UGH.EXE` next to the program and in the current directory;
4. otherwise it opens a file dialog and asks you to pick it.

Every candidate is checked against the SHA-256 above. The first valid file is **copied into the data folder**.
From then on the game starts without asking, even if the original file is moved or deleted.

The data folder also holds the high-score table `UGH!.HI` (stored XOR-encoded, exactly like the original
does). The files next to your original `UGH.EXE` are never written to.

| System | Data folder |
|---|---|
| macOS | `~/Library/Application Support/Play Byte/UGH/` |
| Linux | `~/.local/share/Play Byte/UGH/` |
| Windows | `%APPDATA%\Play Byte\UGH\` |

## Controls

| | Player 1 | Player 2 (team mode) |
|---|---|---|
| Fly | arrow keys | `W` `Z` `A` `S`, or keypad `8` `2` `4` `6` |
| Fire (throw the rock) | right `Ctrl` or keypad `0` / `Ins` | left `Ctrl` or keypad `Enter` |

Mac keyboards have neither `Insert` nor, on laptops, a right `Ctrl`, so the **right Option (⌥) and right
Command (⌘) keys also work as player 1's fire button**.

Main menu: `F1` start, `F2` enter a level password, `F3` difficulty, `F4` one player / team mode,
`F5` controls, `Q` quit. In the game: `P` pause, `Esc` give up.

Joysticks, which the original also supports, are not ported.

## Tests and tools

The tests run against the original game file, which they expect at `ugh/UGH.EXE` in the source tree:

```sh
mkdir ugh && cp /path/to/UGH.EXE ugh/
ctest --test-dir build --output-on-failure
```

- `data`: decoders (Pack-Ice, sprites, pictures, water surface), SHA-256, EXE verification.
- `runtime`: the coroutine frame clock.
- `play`: scripted games. The menu leads to level 1, then free fall, a landing, a full passenger delivery,
  a game over with name entry into the high-score table, and sound output.

Two helper programs:

- `build/ugh-extract UGH.EXE out/` exports the pictures, sprites and palette as PNG files.
- `build/ugh-snap UGH.EXE out/ 100:01 400=shot …` runs the game headless at 70 frames per second, presses keys
  at given frames and saves screenshots. It is used for visual checks without a window; see the comment at the
  top of `tools/snap/main.cpp` for the event syntax.

## How it works

- **No emulation of the CPU.** Each routine of the original is a C++ function, with its original address
  (`113b:0c07`, …) in a comment.
- **VGA Mode X is emulated.** The original draws through the VGA hardware: planes, latches, write modes,
  page flipping and split screen. Collision detection reads bit 7 of the background pixels in video memory,
  so the drawing code has to behave exactly like the hardware (`src/vga/`).
- **Waiting is a coroutine.** Where the original busy-waits for the vertical retrace, the port does
  `co_await clock.nextFrame()` (C++20 coroutines, 70.086 Hz).
- **The original's memory layout is kept.** The game state lives in a copy of the original data segment at the
  original offsets, accessed through named, typed variables. Texts, level records, character tables and actor
  scripts are used in place, with 16-bit wrap-around where the original has it.
- **Sound is a port of the game's AdLib driver.** It drives a cycle-accurate OPL3 emulator (Nuked-OPL3),
  timed by the same PIT timer rates as on a PC.
- **Memory-safe C++.** No raw `new`/`delete`, no C arrays or pointer arithmetic, bounds-checked access to
  everything read from the game file, and RAII wrappers for every SDL resource. The code builds with
  `-Werror` and must be clean under clang-tidy.

Some things the original does that the port reproduces:

- The random number generator is never seeded, so the rain falls the same way in every game.
- The music volume fade-out is inaudible: the driver's music channel count is always 0.
- An empty (0-byte) `UGH!.HI` turns the high-score table into garbage: the game "decrypts" a table that was
  never read from the file.
- In the attract-mode demo, the big "UGH!" sign is clipped at the right screen edge by writing its width
  straight into the sprite directory, through a far pointer that the EXE's relocation table fills in.

## Repository layout

```
src/core/       error type, bounds-checked byte reader, SHA-256
src/data/       MZ loader, Pack-Ice decoder, graphics decoders, data segment, address layout
src/vga/        VGA Mode X emulation
src/runtime/    coroutine task and frame clock
src/game/       the game: menus, levels, physics, passengers, enemies, water, status bar, scripted sequences
src/sound/      OPL chip wrapper, PIT timer multiplexer, AdLib driver port
src/platform/   SDL 3 front end: screen, keyboard, finding UGH.EXE
tools/          ugh-extract, ugh-snap
tests/          data, runtime and play tests
analysis/       Ghidra scripts used for the disassembly
```

## License

The source code of this port is released under the [MIT License](LICENSE). It covers only the code in this
repository, not the original game, its data or the third-party libraries listed below, which keep their own
licenses.

## Credits

- *UGH!* © 1992 Play Byte. The original game is not part of this repository. From the game's own credits:
  PC version programmed by Mario Knezovic with Carsten Neubauer, intros by Mario Knezovic with Claudia Scholz;
  original Amiga version programmed by Thomas Klinger and Bjorn Roy, Amiga graphics by Thomas Klinger;
  PC graphics by Michael Detert with Carsten Neubauer and Mario Knezovic; music and effects by Haiko Ruttmann;
  levels by Peter Schmitz and Bjorn Roy.
- [Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3) by Nuke.YKT (LGPL-2.1).
- [SDL 3](https://libsdl.org) (zlib license).
