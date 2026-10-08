#include <simforge/platform/window.hpp>
#include <simforge/renderer/renderer.hpp>
#include <SDL3/SDL.h>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <type_traits>

namespace
{
void check(bool value, std::source_location location = std::source_location::current())
{
    if (!value) throw std::runtime_error("Renderer lifecycle check failed at line " + std::to_string(location.line()));
}
template<class Exception, class Function> void rejects(Function&& function)
{
    bool rejected = false;
    try { function(); } catch (const Exception&) { rejected = true; }
    check(rejected);
}
}
int main()
{
    using namespace simforge;
    static_assert(!std::is_copy_constructible_v<renderer::Renderer>);
    static_assert(!std::is_move_constructible_v<renderer::Renderer>);
    static_assert(!std::is_copy_constructible_v<platform::Window>);
    try
    {
        check(SDL_WasInit(SDL_INIT_VIDEO) == 0);
        platform::Window window;
        check(!window.is_open() && !window.native_handle());
        check(window.width() == 0 && window.height() == 0 && window.pixel_size() == platform::PixelSize{});
        rejects<std::invalid_argument>([&] { window.create("Invalid",0,720); });
        rejects<std::invalid_argument>([&] { window.create("Invalid",1280,0); });
        rejects<std::invalid_argument>([&] { window.create("Invalid",UINT32_MAX,720); });
        rejects<std::invalid_argument>([&] { window.create("Invalid",100,100,static_cast<platform::WindowMode>(99)); });
        rejects<std::logic_error>([&] { window.poll_events(); });
        rejects<std::logic_error>([&] { window.resize(100,100); });
        rejects<std::logic_error>([&] { window.minimize(); });
        rejects<std::logic_error>([&] { window.restore(); });
        rejects<std::logic_error>([&] { window.request_close(); });
        renderer::Renderer graphics;
        const renderer::RenderFrameData frame;
        check(!graphics.initialized());
        check(graphics.info().presented_frames == 0 && graphics.info().gpu_name.empty());
        rejects<std::logic_error>([&] { (void)graphics.render_frame(frame); });
        rejects<std::logic_error>([&] { (void)graphics.capture_frame(); });
        // The native-window precondition fails before any Vulkan instance/GPU call.
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            rejects<std::invalid_argument>([&] { graphics.initialize(window); });
            check(!graphics.initialized());
            graphics.shutdown(); graphics.shutdown();
            check(SDL_WasInit(SDL_INIT_VIDEO) == 0);
        }
        rejects<std::logic_error>([&] { (void)graphics.render_frame(frame); });
        window.destroy(); window.destroy();
        check(!window.is_open() && SDL_WasInit(SDL_INIT_VIDEO) == 0);
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
