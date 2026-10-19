#pragma once

#include <SDL3/SDL.h>

#include <memory>

/// Owning RAII handles for SDL objects. Never call SDL_Destroy* outside these deleters.
namespace ugh::sdl {

struct WindowDeleter {
    void operator()(SDL_Window* window) const noexcept { SDL_DestroyWindow(window); }
};
struct RendererDeleter {
    void operator()(SDL_Renderer* renderer) const noexcept { SDL_DestroyRenderer(renderer); }
};
struct TextureDeleter {
    void operator()(SDL_Texture* texture) const noexcept { SDL_DestroyTexture(texture); }
};
struct SurfaceDeleter {
    void operator()(SDL_Surface* surface) const noexcept { SDL_DestroySurface(surface); }
};

struct AudioStreamDeleter {
    void operator()(SDL_AudioStream* stream) const noexcept { SDL_DestroyAudioStream(stream); }
};

/// Strings SDL allocates for the caller (SDL_GetPrefPath, …).
struct FreeDeleter {
    // This deleter is the RAII wrapper; SDL_free is the only way to release SDL's allocation.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
    void operator()(char* text) const noexcept { SDL_free(text); }
};

using SdlStringPtr = std::unique_ptr<char, FreeDeleter>;
using AudioStreamPtr = std::unique_ptr<SDL_AudioStream, AudioStreamDeleter>;
using WindowPtr = std::unique_ptr<SDL_Window, WindowDeleter>;
using RendererPtr = std::unique_ptr<SDL_Renderer, RendererDeleter>;
using TexturePtr = std::unique_ptr<SDL_Texture, TextureDeleter>;
using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

/// SDL_Init / SDL_Quit pair.
class Context {
public:
    explicit Context(SDL_InitFlags flags) : ok_(SDL_Init(flags)) {}
    ~Context()
    {
        if (ok_) {
            SDL_Quit();
        }
    }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;

    [[nodiscard]] bool ok() const { return ok_; }

private:
    bool ok_;
};

} // namespace ugh::sdl
