#include "window_state.hpp"

#include <climits>

namespace simforge::platform
{
namespace
{
void check_size(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0 || width > INT_MAX || height > INT_MAX)
        throw std::invalid_argument("Window dimensions must be positive signed-int values");
}
void sdl_check(bool result, const char* operation)
{ if (!result) throw std::runtime_error(std::string{operation} + ": " + SDL_GetError()); }
}
Window::~Window() { destroy(); }
void Window::create(std::string title, std::uint32_t width, std::uint32_t height, WindowMode mode)
{
    if (state_) throw std::logic_error("Window is already created");
    check_size(width,height);
    if (mode != WindowMode::Standard && mode != WindowMode::Vulkan)
        throw std::invalid_argument("Invalid window mode");
    auto state = std::make_shared<WindowState>();
    sdl_check(SDL_InitSubSystem(SDL_INIT_VIDEO), "SDL video initialization failed");
    state->video_initialized = true; state->vulkan = mode == WindowMode::Vulkan;
    const auto flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (state->vulkan ? SDL_WINDOW_VULKAN : 0);
    state->window = SDL_CreateWindow(title.c_str(), static_cast<int>(width), static_cast<int>(height), flags);
    if (!state->window) throw std::runtime_error(std::string{"SDL window creation failed: "} + SDL_GetError());
    state->open = true; state_ = std::move(state);
}
void Window::destroy() noexcept { if (state_) state_->open = false; state_.reset(); }
bool Window::is_open() const noexcept { return state_ && state_->open; }
void Window::poll_events()
{
    if (!state_) throw std::logic_error("Cannot poll an uncreated window");
    SDL_Event event{};
    const auto id = SDL_GetWindowID(state_->window);
    while (SDL_PollEvent(&event))
        if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == id))
            state_->open = false;
}
std::uint32_t Window::width() const noexcept
{ int width = 0; if (state_) SDL_GetWindowSize(state_->window,&width,nullptr); return static_cast<std::uint32_t>(std::max(0,width)); }
std::uint32_t Window::height() const noexcept
{ int height = 0; if (state_) SDL_GetWindowSize(state_->window,nullptr,&height); return static_cast<std::uint32_t>(std::max(0,height)); }
SDL_Window* Window::native_handle() const noexcept { return state_ ? state_->window : nullptr; }
PixelSize Window::pixel_size() const { return state_ ? state_->pixel_size() : PixelSize{}; }
void Window::resize(std::uint32_t width, std::uint32_t height)
{
    if (!state_) throw std::logic_error("Cannot resize an uncreated window");
    check_size(width,height); sdl_check(SDL_SetWindowSize(state_->window,static_cast<int>(width),static_cast<int>(height)),"SDL resize failed");
}
void Window::minimize()
{ if (!state_) throw std::logic_error("Cannot minimize an uncreated window"); sdl_check(SDL_MinimizeWindow(state_->window),"SDL minimize failed"); }
void Window::restore()
{ if (!state_) throw std::logic_error("Cannot restore an uncreated window"); sdl_check(SDL_RestoreWindow(state_->window),"SDL restore failed"); }
void Window::request_close()
{
    if (!state_) throw std::logic_error("Cannot close an uncreated window");
    SDL_Event event{}; event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED; event.window.windowID = SDL_GetWindowID(state_->window);
    sdl_check(SDL_PushEvent(&event),"SDL close event failed");
}
}
