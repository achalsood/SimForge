#include "graphics.hpp"
#include <simforge/platform/window.hpp>
#include <simforge/renderer/renderer.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <algorithm>
#include <array>
#include <fstream>
#include <cmath>

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
              << " | Synchronization validation: " << (info.synchronization_validation ? "enabled" : "disabled")
              << "\nDepth: " << info.depth_format << " | Static GPU buffers: 2 | Asset init: " << info.asset_initialization_ms
              << " ms | Pipeline creation: " << info.pipeline_creation_ms << " ms\nRendering floor, metric grid and four colored cubes...\n";
}
void verify_scene(const simforge::renderer::CapturedFrame& frame)
{
    std::array<std::uint64_t,4> colors{};
    std::uint64_t changed = 0;
    for (std::size_t i = 0; i < frame.rgba.size(); i += 4)
    {
        const int r = frame.rgba[i], g = frame.rgba[i+1], b = frame.rgba[i+2];
        if (std::abs(r-44)+std::abs(g-60)+std::abs(b-77) > 15) ++changed;
        if (r > 150 && r > g*1.3 && g > b*1.5) ++colors[0];
        if (g > 130 && b > 130 && g > r*1.5 && b > r*1.5) ++colors[1];
        if (r > 150 && g > 150 && b < 120) ++colors[2];
        if (b > 130 && r > 80 && g < 140 && b > g*1.3) ++colors[3];
    }
    if (changed < frame.rgba.size()/40 || std::any_of(colors.begin(),colors.end(),[](auto count) { return count < 100; }))
        throw std::runtime_error("Scene capture lacks meaningful geometry or one of four distinct cube colors");
    std::cout << "Scene image verified | Non-background pixels: " << changed << " | Orange/teal/yellow/violet: "
        << colors[0] << '/' << colors[1] << '/' << colors[2] << '/' << colors[3] << '\n';
}
void save_capture(const simforge::renderer::CapturedFrame& frame, const std::filesystem::path& path)
{
    std::ofstream file{path,std::ios::binary};
    if (!file) throw std::runtime_error("Cannot open diagnostic capture output");
    file << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
    for (std::size_t i = 0; i < frame.rgba.size(); i += 4)
        file.write(reinterpret_cast<const char*>(frame.rgba.data()+i),3);
    if (!file) throw std::runtime_error("Cannot write diagnostic capture output");
}
void submit_and_wait(simforge::renderer::Renderer& graphics, const simforge::renderer::RenderFrameData& frame)
{
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (graphics.render_frame(frame)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    throw std::runtime_error("Visual verification could not present a frame");
}
void verify_depth(simforge::renderer::Renderer& graphics)
{
    using namespace simforge::renderer;
    RenderFrameData probe;
    probe.camera.position = {0,0,8}; probe.camera.target = {0,0,0};
    // Near is deliberately submitted before far. Painter's order would fail.
    probe.objects = {
        {MeshId::Cube,{{0,0,2},{0,0,0},{2,2,2}},{0.035f,0.50f,0.54f}},
        {MeshId::Cube,{{0,0,0},{0,0,0},{3,3,3}},{0.80f,0.24f,0.045f}}
    };
    submit_and_wait(graphics,probe); const auto first = graphics.capture_frame();
    const auto pixel = (static_cast<std::size_t>(first.height/2)*first.width+first.width/2)*4;
    if (first.rgba[pixel+1] <= first.rgba[pixel]*2 || first.rgba[pixel+2] <= first.rgba[pixel]*2)
        throw std::runtime_error("Depth probe: far cube overwrote the nearer teal cube");
    std::reverse(probe.objects.begin(),probe.objects.end());
    submit_and_wait(graphics,probe); const auto reversed = graphics.capture_frame();
    if (first.rgba != reversed.rgba) throw std::runtime_error("Depth probe changed with reversed draw order");
    RenderFrameData empty; submit_and_wait(graphics,empty);
    const auto clear = graphics.capture_frame();
    for (std::size_t i = 0; i < clear.rgba.size(); i += 4)
        if (std::abs(static_cast<int>(clear.rgba[i])-44) > 1 || std::abs(static_cast<int>(clear.rgba[i+1])-60) > 1 ||
            std::abs(static_cast<int>(clear.rgba[i+2])-77) > 1)
            throw std::runtime_error("Empty scene did not clear all previous geometry");
    std::cout << "Depth verified: near cube wins when drawn first; reversed order produces identical pixels; empty scene clears.\n";
}
}
int run_graphics(std::optional<std::uint64_t> smoke_frames, const std::optional<std::filesystem::path>& capture_output)
{
    using namespace simforge;
    using timer = std::chrono::steady_clock;
    platform::Window window;
    window.create("SimForge Vulkan Sandbox",1280,720,platform::WindowMode::Vulkan);
    renderer::Renderer graphics;
    renderer::RendererOptions options;
    if (smoke_frames) { options.validation = true; options.require_validation = true; options.capture_frames = true; }
    graphics.initialize(window,options); summary(graphics.info());
    const auto frame = renderer::make_demo_scene();
    double render_wall_ms = 0; std::uint64_t measured_frames = 0;
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
            if (count >= *smoke_frames)
            {
                const auto image = graphics.capture_frame(); verify_scene(image);
                if (capture_output) save_capture(image,*capture_output);
                verify_depth(graphics); submit_and_wait(graphics,frame);
                window.request_close(); window.poll_events(); break;
            }
            if (timer::now() - last_progress > std::chrono::seconds{30}) throw std::runtime_error("Graphics smoke made no presentation progress for 30 seconds");
        }
        const auto render_started = timer::now();
        const bool presented = graphics.render_frame(frame);
        const auto duration = std::chrono::duration<double,std::milli>{timer::now()-render_started}.count();
        if (presented) { render_wall_ms += duration; ++measured_frames; }
        else std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    graphics.shutdown();
    auto info = graphics.info();
    std::cout << "Graphics shutdown | Presented: " << info.presented_frames << " | Swapchain generations: " << info.swapchain_generations
              << " | Skipped: " << info.skipped_frames << " | Validation warnings: " << info.validation_warnings << " | Validation errors: " << info.validation_errors << '\n';
    std::cout << "Scene draws/frame: " << info.draw_calls << " | Referenced mesh vertices: " << info.submitted_vertices
              << " | Indices submitted: " << info.submitted_indices << " | Pipelines: " << info.pipeline_generations
              << " | Buffer allocations: " << info.buffer_allocations << " | Depth images: " << info.depth_images_created
              << " | Mean CPU render/present wall time: " << (measured_frames ? render_wall_ms/static_cast<double>(measured_frames) : 0)
              << " ms (includes FIFO waits; not GPU throughput)\n";
    if (smoke_frames)
    {
        if (!restored || !resized3 || info.presented_frames < *smoke_frames || info.swapchain_generations < 4 || !info.validation_enabled || !info.synchronization_validation)
            throw std::runtime_error("Graphics smoke did not exercise all required phases");
        if (info.validation_errors != 0) throw std::runtime_error("Vulkan validation errors were reported");
        if (info.pipeline_generations != 1 || info.draw_calls != 6 || info.submitted_vertices != 236 || info.submitted_indices != 354 ||
            info.depth_images_created != info.swapchain_generations || info.buffer_allocations != 2+info.swapchain_generations ||
            std::abs(info.aspect_ratio-static_cast<float>(info.width)/static_cast<float>(info.height)) > 1e-6f)
            throw std::runtime_error("Smoke resource reuse, geometry counts or camera aspect mismatch");
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
        std::cout << "Graphics smoke passed: 3D geometry, image/color checks, depth occlusion, repeated resize, minimize/restore, close, reinitialize and native-window lease; validation errors: 0\n";
    }
    return 0;
}
