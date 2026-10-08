#pragma once
#include <simforge/platform/window.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <stdexcept>

namespace simforge::platform
{
struct WindowState
{
    SDL_Window* window{nullptr};
    bool video_initialized{false}, open{false}, vulkan{false};
    ~WindowState()
    {
        if (window) SDL_DestroyWindow(window);
        if (video_initialized) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    [[nodiscard]] PixelSize pixel_size() const
    {
        if (!open || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0) return {};
        int width = 0, height = 0;
        if (!SDL_GetWindowSizeInPixels(window, &width, &height))
            throw std::runtime_error(std::string{"SDL pixel size query failed: "} + SDL_GetError());
        return {static_cast<std::uint32_t>(std::max(0,width)), static_cast<std::uint32_t>(std::max(0,height))};
    }
};
}
