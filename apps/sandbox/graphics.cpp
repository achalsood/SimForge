#include "graphics.hpp"
#include <simforge/platform/window.hpp>
#include <simforge/renderer/renderer.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
void summary(const simforge::renderer::RendererInfo& info)
{
    std::cout << "SimForge Graphics Sandbox\nBackend: Vulkan | Surface: SDL3\nGPU: " << info.gpu_name << " (" << info.gpu_type << ")\n"
              << "Device API: " << info.api_version << " | Instance API: 1.1 | Loader: " << info.loader_version << '\n'
              << "Queues: graphics " << info.graphics_family << ", present " << info.present_family << '\n'
              << "Swapchain: " << info.width << 'x' << info.height << " | Images: " << info.image_count
              << " | Format: " << info.surface_format << " | Present mode: " << info.present_mode << '\n'
              << "Validation: " << (info.validation_enabled ? "enabled" : "disabled")
              << " | Synchronization validation: " << (info.synchronization_validation ? "enabled" : "disabled") << "\nRendering dark blue-gray clear color...\n";
}
}
int run_graphics(std::optional<std::uint64_t> smoke_frames)
{
    using namespace simforge;
    using timer = std::chrono::steady_clock;
    platform::Window window;
    window.create("SimForge Vulkan Sandbox",1280,720,platform::WindowMode::Vulkan);
    renderer::Renderer graphics;
    renderer::RendererOptions options;
    if (smoke_frames) { options.validation = true; options.require_validation = true; }
    graphics.initialize(window,options); summary(graphics.info());
    const renderer::RenderFrameData frame;
    auto last_progress = timer::now();
    std::uint64_t last_presented = 0;
    bool resized1 = false, resized2 = false, minimized = false, restored = false, resized3 = false;
    std::optional<timer::time_point> minimize_started;
    std::uint64_t skipped_before_minimize = 0, generations_before_minimize = 0;
    while (window.is_open())
    {
        window.poll_events(); if (!window.is_open()) break;
        if (smoke_frames)
        {
            const auto count = graphics.info().presented_frames;
            if (count != last_presented) { last_presented = count; last_progress = timer::now(); }
            if (count >= 10 && !resized1) { window.resize(900,600); resized1 = true; }
            if (count >= 30 && !resized2) { window.resize(1400,800); resized2 = true; }
            if (count >= 50 && !minimized)
            {
                skipped_before_minimize = graphics.info().skipped_frames;
                generations_before_minimize = graphics.info().swapchain_generations;
                window.minimize(); minimize_started = timer::now(); minimized = true;
            }
            if (minimize_started && !restored && timer::now() - *minimize_started >= std::chrono::milliseconds{250})
            {
                const auto info = graphics.info();
                if (info.skipped_frames == skipped_before_minimize) throw std::runtime_error("Smoke: minimized rendering was not skipped");
                if (info.swapchain_generations != generations_before_minimize) throw std::runtime_error("Smoke: swapchain was recreated while minimized");
                window.restore(); restored = true;
            }
            if (count >= 80 && !resized3) { window.resize(1280,720); resized3 = true; }
            if (count >= *smoke_frames) { window.request_close(); window.poll_events(); break; }
            if (timer::now() - last_progress > std::chrono::seconds{30}) throw std::runtime_error("Graphics smoke made no presentation progress for 30 seconds");
        }
        if (!graphics.render_frame(frame)) std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    graphics.shutdown();
    auto info = graphics.info();
    std::cout << "Graphics shutdown | Presented: " << info.presented_frames << " | Swapchain generations: " << info.swapchain_generations
              << " | Skipped: " << info.skipped_frames << " | Validation warnings: " << info.validation_warnings << " | Validation errors: " << info.validation_errors << '\n';
    if (smoke_frames)
    {
        if (!restored || !resized3 || info.presented_frames < *smoke_frames || info.swapchain_generations < 4 || !info.validation_enabled || !info.synchronization_validation)
            throw std::runtime_error("Graphics smoke did not exercise all required phases");
        if (info.validation_errors != 0) throw std::runtime_error("Vulkan validation errors were reported");
        // Reuse the renderer after full shutdown and intentionally release the
        // Window wrapper first. Its native lease must survive Vulkan cleanup.
        window.destroy(); graphics.shutdown();
        window.create("SimForge Vulkan Lifecycle Smoke",640,480,platform::WindowMode::Vulkan);
        graphics.initialize(window,options);
        for (int i = 0; i < 3; ++i) { window.poll_events(); (void)graphics.render_frame(frame); }
        window.destroy();
        if (graphics.render_frame(frame)) throw std::runtime_error("Renderer presented after window close");
        graphics.shutdown(); info = graphics.info();
        if (info.validation_errors != 0) throw std::runtime_error("Validation errors during lifecycle smoke");
        std::cout << "Graphics smoke passed: clear/present, repeated resize, minimize/restore, close, reinitialize and native-window lease; validation errors: 0\n";
    }
    return 0;
}
