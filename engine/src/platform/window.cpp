#include <simforge/platform/window.hpp>

#include <SDL3/SDL.h>

#include <stdexcept>
#include <utility>

namespace simforge::platform
{

Window::~Window()
{
    destroy();
}

void Window::create(
    std::string title,
    std::uint32_t width,
    std::uint32_t height
)
{
    if (window_ != nullptr)
    {
        throw std::logic_error("Window is already created");
    }

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        throw std::runtime_error(
            std::string{"Failed to initialize SDL: "} + SDL_GetError()
        );
    }

    window_ = SDL_CreateWindow(
        title.c_str(),
        static_cast<int>(width),
        static_cast<int>(height),
        SDL_WINDOW_RESIZABLE
    );

    if (window_ == nullptr)
    {
        SDL_Quit();

        throw std::runtime_error(
            std::string{"Failed to create SDL window: "} + SDL_GetError()
        );
    }

    width_ = width;
    height_ = height;
    open_ = true;
}

void Window::destroy()
{
    if (window_ != nullptr)
    {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    if (open_)
    {
        SDL_Quit();
    }

    open_ = false;
    width_ = 0;
    height_ = 0;
}

bool Window::is_open() const noexcept
{
    return open_;
}

void Window::poll_events()
{
    SDL_Event event{};

    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT)
        {
            open_ = false;
        }

        if (event.type == SDL_EVENT_WINDOW_RESIZED)
        {
            width_ =
                static_cast<std::uint32_t>(
                    event.window.data1
                );

            height_ =
                static_cast<std::uint32_t>(
                    event.window.data2
                );
        }
    }
}

std::uint32_t Window::width() const noexcept
{
    return width_;
}

std::uint32_t Window::height() const noexcept
{
    return height_;
}

SDL_Window* Window::native_handle() const noexcept
{
    return window_;
}

} // namespace simforge::platform