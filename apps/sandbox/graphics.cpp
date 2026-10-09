#include "graphics.hpp"
#include "viewer.hpp"
#include "dashboard.hpp"
#include "capture.hpp"
#include <imgui.h>
#include <SDL3/SDL.h>
#include <simforge/platform/window.hpp>
#include <simforge/renderer/renderer.hpp>
#include <simforge/renderer/geometry.hpp>
#include <simforge/visualization/warehouse_layout.hpp>
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
              << " ms | Pipeline creation: " << info.pipeline_creation_ms << " ms\n";
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
void verify_warehouse(const simforge::renderer::CapturedFrame& frame)
{
    std::array<std::uint64_t,4> regions{};
    std::array<std::uint64_t,3> palette{};
    for (std::uint32_t y = 0; y < frame.height; ++y) for (std::uint32_t x = 0; x < frame.width; ++x)
    {
        const auto i = (static_cast<std::size_t>(y)*frame.width+x)*4;
        const int r = frame.rgba[i], g = frame.rgba[i+1], b = frame.rgba[i+2];
        if (std::abs(r-44)+std::abs(g-60)+std::abs(b-77) > 15)
            ++regions[(y >= frame.height/2 ? 2 : 0)+(x >= frame.width/2 ? 1 : 0)];
        if (r > 145 && g > 90 && r > b*1.4) ++palette[0]; // Safety yellow / vehicle amber.
        if (g > 95 && b > 100 && b > r*1.12 && g > r*1.05) ++palette[1]; // Cargo / structural blue-gray.
        if (r > 135 && g > 135 && b > 135 && std::abs(r-g) < 25) ++palette[2]; // Shelves / straps.
    }
    std::cout << "Warehouse capture | Four regions: " << regions[0] << '/' << regions[1] << '/' << regions[2] << '/' << regions[3]
              << " | Amber/blue-gray/metal pixels: " << palette[0] << '/' << palette[1] << '/' << palette[2] << '\n';
    if (std::count_if(regions.begin(),regions.end(),[](auto n) { return n >= 1000; }) < 2 ||
        std::any_of(palette.begin(),palette.end(),[](auto n) { return n < 100; }))
        throw std::runtime_error("Warehouse GPU image lacks distinct regions or industrial colors");
    std::cout << "Warehouse image verified: geometry in multiple regions and three industrial palette groups.\n";
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
int run_graphics(std::optional<std::uint64_t> smoke_frames, const std::optional<std::filesystem::path>& capture_output,
    const std::optional<std::filesystem::path>& scenario_path, bool live, double speed)
{
    if (live)
    {
        if (!scenario_path) throw std::invalid_argument("Live graphics requires a warehouse scenario");
        return run_viewer(smoke_frames,capture_output,*scenario_path,speed);
    }
    using namespace simforge;
    using timer = std::chrono::steady_clock;
    // Load, validate and build before any SDL or Vulkan initialization.
    const std::optional<visualization::WarehouseLayout> layout = scenario_path ?
        std::optional{visualization::build_warehouse_layout(scenario::load_scenario(*scenario_path))} : std::nullopt;
    const auto frame = layout ? visualization::make_warehouse_frame(*layout) : renderer::make_demo_scene();
    std::uint32_t expected_vertices = 0, expected_indices = 0;
    const std::array meshes{renderer::make_builtin_mesh(renderer::MeshId::Cube),renderer::make_builtin_mesh(renderer::MeshId::Floor),
        renderer::make_builtin_mesh(renderer::MeshId::Grid),renderer::make_builtin_mesh(renderer::MeshId::Dock),
        renderer::make_builtin_mesh(renderer::MeshId::Rack),renderer::make_builtin_mesh(renderer::MeshId::Pallet),
        renderer::make_builtin_mesh(renderer::MeshId::Forklift),renderer::make_builtin_mesh(renderer::MeshId::Worker)};
    for (const auto& object : frame.objects)
    {
        const auto& mesh = meshes.at(static_cast<std::size_t>(object.mesh)-1);
        expected_vertices += static_cast<std::uint32_t>(mesh.vertices.size());
        expected_indices += static_cast<std::uint32_t>(mesh.indices.size());
    }
    platform::Window window;
    window.create("SimForge Vulkan Sandbox",1280,720,platform::WindowMode::Vulkan);
    renderer::Renderer graphics;
    renderer::RendererOptions options;
    options.ui = !smoke_frames || !scenario_path;
    if (smoke_frames) { options.validation = true; options.require_validation = true; options.capture_frames = true; }
    graphics.initialize(window,options); FailureCapture failure_capture{graphics}; summary(graphics.info());
    std::optional<std::filesystem::path> open_workspace;
    std::optional<scenario::ScenarioDefinition> loaded_workspace;
    std::array<char,1025> scenario_input{};
    const char* executable_path=SDL_GetBasePath();
    const auto example = std::filesystem::exists("examples/warehouse-baseline.json") ? std::filesystem::path{"examples/warehouse-baseline.json"} :
        (executable_path?std::filesystem::path{executable_path}:std::filesystem::current_path())/"examples/warehouse-baseline.json";
    const auto example_name = example.u8string();
    std::copy_n(reinterpret_cast<const char*>(example_name.data()),std::min(example_name.size(),scenario_input.size()-1),scenario_input.data());
    std::string launcher_error;
    float launcher_scale=0;
    bool launcher_verified=false;
    glm::vec2 launcher_button{0};
    std::cout << "Scene: " << (scenario_path ? scenario_path->string() : "cube demo") << " | Objects: " << frame.objects.size()
              << " | Unique GPU meshes: " << renderer::builtin_mesh_count << " | Static scene built once\n";
    if (layout)
    {
        std::array<std::uint32_t,7> counts{};
        for (const auto& object : layout->objects) ++counts[static_cast<std::size_t>(object.kind)];
        std::cout << "Capacity representatives | Bays: " << counts[1] << " | Racks: " << counts[2] << " | Pallets: " << counts[3]
                  << " | Forklifts: " << counts[4] << " | Workers: " << counts[5] << " | Floor markings: " << counts[6] << '\n';
    }
    double render_wall_ms = 0; std::uint64_t measured_frames = 0;
    std::vector<double> steady_times;
    auto last_progress = timer::now();
    std::uint64_t last_presented = 0;
    bool resized1 = false, resized2 = false, minimized = false, restored = false, resized3 = false;
    std::optional<timer::time_point> minimize_started;
    std::uint64_t skipped_before_minimize = 0, generations_before_minimize = 0;
    while (window.is_open())
    {
        window.poll_events([&](const SDL_Event& event){ if(options.ui) graphics.process_ui_event(event); }); if (!window.is_open()) break;
        if(options.ui)
        {
            const auto pixels=window.pixel_size();
            const float density=pixels.width && window.width()?static_cast<float>(pixels.width)/window.width():1;
            const float scale=std::clamp(SDL_GetWindowDisplayScale(window.native_handle())/density,1.0f,2.0f);
            if(scale!=launcher_scale) { style_dashboard(scale); launcher_scale=scale; }
            if(graphics.begin_ui_frame())
            {
                if(!launcher_verified)
                {
                    ImGui::SetNextWindowPos({20*scale,20*scale},ImGuiCond_FirstUseEver);
                    ImGui::SetNextWindowSize({std::min(460*scale,static_cast<float>(window.width())-40),0});
                    ImGui::Begin("SIMFORGE | Open a warehouse",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings);
                    ImGui::TextWrapped("Open a warehouse scenario to edit configurations, compare results and export reports. The static scene demo remains available behind this panel.");
                    ImGui::TextUnformatted("Local scenario JSON path"); ImGui::SetNextItemWidth(-1);
                    ImGui::InputText("##Scenario",scenario_input.data(),scenario_input.size());
                    if(ImGui::Button("Open warehouse workspace",{-1,36*scale}))
                    {
                        try
                        {
                            auto path=std::filesystem::u8path(scenario_input.data());
                            // Prepare CPU validation/layout before leaving the working demo.
                            auto definition=scenario::load_scenario(path);
                            (void)visualization::build_warehouse_layout(definition);
                            loaded_workspace=std::move(definition);
                            open_workspace=std::move(path);
                        }
                        catch(const std::exception& e) { launcher_error=e.what(); }
                    }
                    const auto low=ImGui::GetItemRectMin(),high=ImGui::GetItemRectMax(); launcher_button={(low.x+high.x)*0.5f,(low.y+high.y)*0.5f};
                    if(!launcher_error.empty()) ImGui::TextWrapped("%s",launcher_error.c_str());
                    ImGui::End();
                }
                graphics.end_ui_frame();
                if(open_workspace)
                {
                    if(!smoke_frames) break;
                    viewer::ViewerController prepared{*loaded_workspace};
                    if(prepared.observation().tick!=0 || prepared.name().empty()) throw std::runtime_error("Launcher did not prepare the loaded warehouse");
                    launcher_verified=true; open_workspace.reset();
                    std::cout<<"Launcher smoke: real ImGui Open button loaded/prepared bundled scenario through injected SDL input.\n";
                }
            }
        }
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
                diagnostic_failure_probe(); const auto image = graphics.capture_frame();
                if (scenario_path)
                {
                    verify_warehouse(image); submit_and_wait(graphics,frame);
                    if (image.rgba != graphics.capture_frame().rgba)
                        throw std::runtime_error("Static warehouse image changed between identical frames");
                    std::cout << "Static warehouse repeated capture identical; no observed flicker.\n";
                }
                else verify_scene(image);
                if (capture_output) write_capture(image,*capture_output);
                verify_depth(graphics); submit_and_wait(graphics,frame);
                window.request_close(); window.poll_events(); break;
            }
            if (timer::now() - last_progress > std::chrono::seconds{30}) throw std::runtime_error("Graphics smoke made no presentation progress for 30 seconds");
        }
        const auto render_started = timer::now();
        const bool presented = graphics.render_frame(frame);
        const auto duration = std::chrono::duration<double,std::milli>{timer::now()-render_started}.count();
        if (presented)
        {
            if(smoke_frames && !scenario_path && !launcher_verified)
            {
                const auto count=graphics.info().presented_frames;
                const auto window_id=SDL_GetWindowID(window.native_handle());
                // ImGui's first auto-sized window frame measures contents before becoming visible.
                if(count==2)
                {
                    const auto image=graphics.capture_frame();
                    if(graphics.info().ui_vertices<100 || graphics.info().ui_draw_calls==0) throw std::runtime_error("Launcher UI absent in Vulkan output");
                    if(capture_output) write_capture(image,capture_output->parent_path()/(capture_output->stem().string()+"-launcher"+capture_output->extension().string()));
                    SDL_Event motion{}; motion.type=SDL_EVENT_MOUSE_MOTION; motion.motion.windowID=window_id; motion.motion.x=launcher_button.x; motion.motion.y=launcher_button.y; SDL_PushEvent(&motion);
                    SDL_Event press{}; press.type=SDL_EVENT_MOUSE_BUTTON_DOWN; press.button.windowID=window_id; press.button.button=SDL_BUTTON_LEFT; press.button.down=true; press.button.x=launcher_button.x; press.button.y=launcher_button.y; SDL_PushEvent(&press);
                }
                if(count==3)
                {
                    SDL_Event release{}; release.type=SDL_EVENT_MOUSE_BUTTON_UP; release.button.windowID=window_id; release.button.button=SDL_BUTTON_LEFT; release.button.x=launcher_button.x; release.button.y=launcher_button.y; SDL_PushEvent(&release);
                }
                if(count>7) throw std::runtime_error("Injected launcher Open failed: "+launcher_error);
            }
            render_wall_ms += duration; ++measured_frames;
            if (graphics.info().presented_frames > 100 && steady_times.size() < 10000) steady_times.push_back(duration);
        }
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
    if (!steady_times.empty())
    {
        double mean = 0; for (auto value : steady_times) mean += value;
        mean /= static_cast<double>(steady_times.size());
        double variance = 0; for (auto value : steady_times) variance += (value-mean)*(value-mean);
        std::sort(steady_times.begin(),steady_times.end());
        std::cout << "Steady CPU render/present (after 100 frames) | Samples: " << steady_times.size()
                  << " | Mean: " << mean << " ms | Stddev: " << std::sqrt(variance/static_cast<double>(steady_times.size()))
                  << " | p50: " << steady_times[steady_times.size()/2] << " | p95: " << steady_times[(steady_times.size()-1)*95/100]
                  << " | Min/max: " << steady_times.front() << '/' << steady_times.back() << " ms; FIFO waits included, no GPU timestamps\n";
    }
    if (smoke_frames)
    {
        if (!restored || !resized3 || info.presented_frames < *smoke_frames || info.swapchain_generations < 4 || !info.validation_enabled || !info.synchronization_validation)
            throw std::runtime_error("Graphics smoke did not exercise all required phases");
        if (info.validation_errors != 0 || info.validation_warnings != 0) throw std::runtime_error("Vulkan validation messages were reported");
        if (info.pipeline_generations != 1 || info.draw_calls != frame.objects.size() || info.submitted_vertices != expected_vertices || info.submitted_indices != expected_indices ||
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
        if (info.validation_errors != 0 || info.validation_warnings != 0) throw std::runtime_error("Validation messages during lifecycle smoke");
        std::cout << "Graphics smoke passed: 3D geometry, image/color checks, depth occlusion, repeated resize, minimize/restore, close, reinitialize and native-window lease; validation errors: 0\n";
    }
    if(open_workspace) { window.destroy(); return run_viewer(std::nullopt,std::nullopt,*open_workspace,speed,true,&*loaded_workspace); }
    return 0;
}
