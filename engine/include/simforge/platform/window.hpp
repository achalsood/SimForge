#pragma once

#include <cstdint>
#include <string>
#include <memory>

struct SDL_Window;

namespace simforge::platform
{
struct WindowState;
class VulkanWindow;
enum class WindowMode { Standard, Vulkan };
struct PixelSize
{
    std::uint32_t width{0}, height{0};
    bool operator==(const PixelSize&) const = default;
};

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
        std::uint32_t height,
        WindowMode mode = WindowMode::Standard
    );

    void destroy() noexcept;

    [[nodiscard]] bool is_open() const noexcept;

    void poll_events();
    [[nodiscard]] bool consume_space_press() noexcept;
    void set_title(const std::string& title);

    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;

    // Borrowed handle; callers must not destroy it or quit SDL video themselves.
    [[nodiscard]] SDL_Window* native_handle() const noexcept;
    [[nodiscard]] PixelSize pixel_size() const;
    void resize(std::uint32_t width, std::uint32_t height);
    void minimize();
    void restore();
    void request_close();
    // Opt-in integration automation: routes a Space event through SDL polling.
    void request_space_press();

private:
    friend class VulkanWindow;
    // Leases keep the native window/video subsystem alive through Vulkan cleanup.
    std::shared_ptr<WindowState> state_;
    bool space_pressed_{false};
};

} // namespace simforge::platform
