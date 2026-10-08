#pragma once

#include <cstdint>
#include <string>

struct SDL_Window;

namespace simforge::platform
{

class Window
{
public:
    Window() = default;

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    ~Window();

    void create(
        std::string title,
        std::uint32_t width,
        std::uint32_t height
    );

    void destroy();

    [[nodiscard]] bool is_open() const noexcept;

    void poll_events();

    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;

    [[nodiscard]] SDL_Window* native_handle() const noexcept;

private:
    SDL_Window* window_{nullptr};

    std::uint32_t width_{0};
    std::uint32_t height_{0};

    bool open_{false};
};

} // namespace simforge::platform