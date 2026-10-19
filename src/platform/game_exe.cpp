#include "platform/game_exe.hpp"

#include "data/exe_image.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <format>
#include <fstream>
#include <iterator>
#include <print>
#include <string>

namespace ugh {
namespace {

constexpr const char* kFileName = "UGH.EXE";
constexpr const char* kTitle = "UGH!";

using Bytes = std::vector<std::uint8_t>;

Result<Bytes> readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return fail(std::format("cannot open {}", path.string()));
    }
    return Bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// Reads `path` and checks that it is the known UGH.EXE.
Result<Bytes> readGameExe(const std::filesystem::path& path)
{
    return readFile(path).and_then([&](Bytes bytes) -> Result<Bytes> {
        if (auto valid = verifyGameExe(bytes); !valid) {
            return fail(std::format("{}: {}", path.string(), valid.error().message));
        }
        return bytes;
    });
}

void keepCopy(const Bytes& bytes, const std::filesystem::path& dataDirectory)
{
    const std::filesystem::path target = dataDirectory / kFileName;
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    const std::string text(bytes.begin(), bytes.end());
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
        std::println(stderr, "ugh: could not keep a copy of UGH.EXE in {}", dataDirectory.string());
    }
}

void showMessage(const std::string& text)
{
    if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, kTitle, text.c_str(), nullptr)) {
        std::println(stderr, "ugh: {}", text);
    }
}

/// Filled in by SDL's file dialog callback, which may run on another thread.
struct DialogState {
    std::atomic<bool> done{false};
    std::optional<std::filesystem::path> chosen;
};

void SDLCALL onFileChosen(void* userdata, const char* const* files, int /*filter*/)
{
    auto* state = static_cast<DialogState*>(userdata);
    if (files != nullptr && *files != nullptr) {
        state->chosen = std::filesystem::path(*files);
    }
    state->done.store(true, std::memory_order_release);
}

/// Shows the open-file dialog and waits for it; nullopt when cancelled or unavailable.
std::optional<std::filesystem::path> askForFile()
{
    const std::array<SDL_DialogFileFilter, 2> filters{{{.name = "UGH.EXE", .pattern = "exe;EXE"},
                                                       {.name = "All files", .pattern = "*"}}};
    DialogState state;
    SDL_ShowOpenFileDialog(onFileChosen, &state, nullptr, filters.data(), static_cast<int>(filters.size()),
                           nullptr, false);
    while (!state.done.load(std::memory_order_acquire)) {
        SDL_Event event;
        static_cast<void>(SDL_WaitEventTimeout(&event, 50));
    }
    return state.chosen;
}

std::vector<std::filesystem::path> searchPlaces(const std::optional<std::filesystem::path>& dataDirectory)
{
    std::vector<std::filesystem::path> places;
    if (dataDirectory) {
        places.push_back(*dataDirectory / kFileName);
    }
    std::vector<std::filesystem::path> folders;
    if (const char* base = SDL_GetBasePath()) {
        folders.emplace_back(base);
    }
    std::error_code error;
    if (auto current = std::filesystem::current_path(error); !error) {
        folders.push_back(std::move(current));
    }
    for (const std::filesystem::path& folder : folders) {
        places.push_back(folder / kFileName);
        places.push_back(folder / "ugh" / kFileName);
    }
    return places;
}

} // namespace

Result<std::optional<Bytes>> findGameExe(const std::optional<std::filesystem::path>& commandLine,
                                        const std::optional<std::filesystem::path>& dataDirectory)
{
    const auto found = [&](Bytes bytes, const std::filesystem::path& from) -> std::optional<Bytes> {
        const bool isKeptCopy = dataDirectory && from == *dataDirectory / kFileName;
        if (dataDirectory && !isKeptCopy) {
            keepCopy(bytes, *dataDirectory);
        }
        return bytes;
    };

    if (commandLine) {
        return readGameExe(*commandLine).transform([&](Bytes bytes) { return found(std::move(bytes), *commandLine); });
    }

    for (const std::filesystem::path& place : searchPlaces(dataDirectory)) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(place, error)) {
            continue;
        }
        auto bytes = readGameExe(place);
        if (bytes) {
            return found(std::move(*bytes), place);
        }
        std::println(stderr, "ugh: skipping {}", bytes.error().message);
    }

    showMessage("This is a port of the 1992 DOS game UGH! and needs the original game file.\n\n"
                "Please choose your UGH.EXE. It is copied into the port's data folder, so you will be asked "
                "only once.");
    while (true) {
        const std::optional<std::filesystem::path> chosen = askForFile();
        if (!chosen) {
            return std::optional<Bytes>{};
        }
        auto bytes = readGameExe(*chosen);
        if (bytes) {
            return found(std::move(*bytes), *chosen);
        }
        showMessage(std::format("{}\n\nPlease choose the UGH.EXE from the DOS release of UGH! (Play Byte, 1992).",
                                bytes.error().message));
    }
}

} // namespace ugh
