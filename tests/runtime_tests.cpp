// Tests of the platform-independent runtime: VGA model, coroutine tasks, keyboard model and the video layer.

#include "check.hpp"
#include "data/graphics.hpp"
#include "game/keyboard.hpp"
#include "game/video.hpp"
#include "runtime/task.hpp"
#include "vga/vga.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <span>
#include <vector>

namespace {

using ugh::test::Checker;

std::vector<std::uint8_t> scanout(const ugh::Vga& vga)
{
    std::vector<std::uint8_t> frame(static_cast<std::size_t>(ugh::Vga::kScreenWidth) * ugh::Vga::kScreenHeight);
    vga.scanout(frame);
    return frame;
}

std::uint8_t pixel(std::span<const std::uint8_t> frame, int x, int y)
{
    return frame[(static_cast<std::size_t>(y) * ugh::Vga::kScreenWidth) + static_cast<std::size_t>(x)];
}

void testVgaPlanesAndScanout(Checker& check)
{
    ugh::Vga vga;
    vga.setRowBytes(80);
    vga.setMapMask(0b0101);
    vga.write(0, 7);
    auto frame = scanout(vga);
    check(pixel(frame, 0, 0) == 7 && pixel(frame, 1, 0) == 0 && pixel(frame, 2, 0) == 7, "map mask selects planes");

    vga.setMapMask(0x0F);
    vga.write(80, 9); // second row in 80-byte rows
    frame = scanout(vga);
    check(pixel(frame, 3, 1) == 9, "row pitch");

    // Latch copy (write mode 1) moves all four planes of one byte.
    static_cast<void>(vga.read(0));
    vga.setWriteMode(1);
    vga.write(1, 0);
    vga.setWriteMode(0);
    frame = scanout(vga);
    check(pixel(frame, 4, 0) == 7 && pixel(frame, 5, 0) == 0 && pixel(frame, 6, 0) == 7, "latch copy");

    // Split screen: rows from the split come from address 0.
    vga.setStartAddress(80);
    vga.setSplitRow(199);
    frame = scanout(vga);
    check(pixel(frame, 0, 0) == 9 && pixel(frame, 0, 199) == 7, "start address and split row");
}

// Coroutine parameters are pointers: a reference parameter could outlive its referent across suspensions.
ugh::Task<int> childWaits(ugh::FrameClock* clock, int frames)
{
    for (int i = 0; i < frames; ++i) {
        co_await clock->nextFrame();
    }
    co_return frames * 10;
}

ugh::Task<> parentSums(ugh::FrameClock* clock, int* result)
{
    const int first = co_await childWaits(clock, 2);
    const int second = co_await childWaits(clock, 1);
    *result = first + second;
}

void testTasks(Checker& check)
{
    ugh::FrameClock clock;
    int result = 0;
    {
        ugh::Task<> root = parentSums(&clock, &result);
        root.start();
        int ticks = 0;
        while (!root.done() && ticks < 10) {
            clock.tick();
            ++ticks;
        }
        check(root.done(), "root task completes");
        check(ticks == 3, "one tick per awaited frame");
        check(result == 30, "child results propagate");
    }
    {
        // Destroying a suspended chain must free every frame (AddressSanitizer reports leaks otherwise).
        ugh::FrameClock other;
        ugh::Task<> abandoned = parentSums(&other, &result);
        abandoned.start();
        other.tick();
    }
}

void testKeyboard(Checker& check)
{
    ugh::Keyboard keyboard;
    const auto drain = [&] {
        while (keyboard.nextInterruptByte()) {
        }
    };
    check(!keyboard.poll().changed, "no key at start");
    keyboard.keyEvent(0x3B, false, true, std::nullopt);
    drain();
    auto poll = keyboard.poll();
    check(poll.changed && poll.code == 0x3B, "make code seen once");
    check(!keyboard.poll().changed, "unchanged on the next poll");
    keyboard.keyEvent(0x3B, false, false, std::nullopt);
    drain();
    poll = keyboard.poll();
    check(poll.changed && poll.code == 0xBB, "break code");

    keyboard.keyEvent(0x48, true, true, std::nullopt);
    check(keyboard.nextInterruptByte() == 0xE0 && keyboard.nextInterruptByte() == 0x48, "extended key sends E0 prefix");

    keyboard.setHandlerInstalled(false);
    keyboard.keyEvent(0x1E, false, true, 'a');
    check(keyboard.readBios() == 'a' && !keyboard.readBios(), "BIOS buffer while unhooked");
    check(!keyboard.nextInterruptByte(), "handler sees nothing while unhooked");
}

void testVideoSprite(Checker& check)
{
    ugh::Vga vga;
    ugh::FrameClock clock;
    // 3x2 sprite, column-major source already converted: rows "1 0 2" / "3 4 5".
    const ugh::Sprite sprite{.width = 3, .height = 2, .pixels = {1, 0, 2, 3, 4, 5}};
    ugh::Video video(vga, clock, {sprite}, ugh::Palette{});

    ugh::Task<> mode = video.setMode384();
    mode.start();
    while (!mode.done()) {
        clock.tick();
    }

    video.setWaterline(10);
    video.drawSprite(0, 6, 10); // second row lies below the waterline
    video.flip();
    const auto frame = scanout(vga);
    check(pixel(frame, 6, 10) == 1 && pixel(frame, 7, 10) == 0 && pixel(frame, 8, 10) == 2, "sprite row above water");
    check(pixel(frame, 6, 11) == (3 | 0x40) && pixel(frame, 8, 11) == (5 | 0x40), "water tint below the line");

    video.drawSprite(0, 0, 192); // status area, shown below the split row
    video.flip();
    const auto status = scanout(vga);
    check(pixel(status, 0, 192) == 1, "status area via split screen");
}

int run()
{
    Checker check;
    testVgaPlanesAndScanout(check);
    testTasks(check);
    testKeyboard(check);
    testVideoSprite(check);
    if (check.failures() == 0) {
        std::println("all runtime tests passed");
    }
    return check.failures() == 0 ? 0 : 1;
}

} // namespace

int main()
{
    try {
        return run();
    } catch (const std::exception& e) {
        static_cast<void>(std::fputs("ugh-runtime-tests: fatal: ", stderr));
        static_cast<void>(std::fputs(e.what(), stderr));
        static_cast<void>(std::fputs("\n", stderr));
        return 1;
    } catch (...) {
        return 1;
    }
}
