#include "graphics.hpp"
#include <simforge/platform/window.hpp>
#include <simforge/renderer/renderer.hpp>
#include <simforge/renderer/geometry.hpp>
#include <simforge/visualization/warehouse_layout.hpp>
#include <simforge/visualization/live_warehouse.hpp>
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
std::filesystem::path phase_path(const std::filesystem::path& base, const char* phase)
{ return base.parent_path()/(base.stem().string()+"-"+phase+".ppm"); }
void verify_live_ticks(simforge::warehouse::WarehouseSimulation& runtime, simforge::visualization::LiveWarehouseScene& scene,
    simforge::renderer::Renderer& graphics, const simforge::scenario::ScenarioDefinition& definition,
    const std::optional<std::filesystem::path>& output)
{
    const auto capture = [&](const char* phase)
    {
        const auto state = runtime.capture_observation(scene.waiting_limit(),scene.processing_limit());
        scene.set_state(state,true); submit_and_wait(graphics,scene.frame());
        auto pixels = graphics.capture_frame();
        std::cout << "Live GPU phase " << phase << " | " << simforge::visualization::format_live_metrics(state,scene.visibility()) << '\n';
        if (output) save_capture(pixels,phase_path(*output,phase));
        return pixels;
    };
    const auto initial = capture("initial");
    runtime.advance_to(0); const auto processing = capture("processing");
    if (initial.rgba == processing.rgba || scene.shipments().empty())
        throw std::runtime_error("Live GPU verification did not show the real tick-zero arrival");
    const auto& config = std::get<simforge::scenario::WarehouseConfig>(definition.domain);
    const auto rate = runtime.clock().ticks_per_second();
    runtime.advance_to(std::min(runtime.horizon_ticks(),2ULL*config.shipment_interval_seconds*rate));
    const auto queue = capture("queue"); const auto queue_tick = runtime.clock().tick_count();
    runtime.advance_to(std::max(runtime.clock().tick_count(),std::min(runtime.horizon_ticks(),static_cast<std::uint64_t>(config.processing_duration_seconds)*rate)));
    const auto completion = capture("completion");
    if (runtime.metrics().waiting != 0 && queue.rgba == processing.rgba)
        throw std::runtime_error("Live queue growth did not change GPU pixels");
    if (runtime.clock().tick_count() != queue_tick && runtime.metrics().completed != 0 && completion.rgba == queue.rgba)
        throw std::runtime_error("Live completion/identity transition did not change GPU pixels");
    std::cout << "Live GPU verification: actual arrival, queue and completion states captured; distinct operational images verified.\n";
}
}
int run_graphics(std::optional<std::uint64_t> smoke_frames, const std::optional<std::filesystem::path>& capture_output,
    const std::optional<std::filesystem::path>& scenario_path, bool live, double speed)
{
    using namespace simforge;
    using timer = std::chrono::steady_clock;
    // Load, validate and build before any SDL or Vulkan initialization.
    const std::optional<scenario::ScenarioDefinition> definition = scenario_path ? std::optional{scenario::load_scenario(*scenario_path)} : std::nullopt;
    const std::optional<visualization::WarehouseLayout> layout = definition && !live ?
        std::optional{visualization::build_warehouse_layout(*definition)} : std::nullopt;
    const auto static_frame = layout ? visualization::make_warehouse_frame(*layout) : renderer::make_demo_scene();
    std::optional<warehouse::WarehouseSimulation> runtime;
    std::optional<visualization::LiveWarehouseScene> live_scene;
    std::optional<warehouse::WarehouseObservation> observation;
    if (live)
    {
        if (!definition) throw std::invalid_argument("Live graphics requires a warehouse scenario");
        live_scene.emplace(*definition); runtime.emplace(*definition); runtime->set_time_scale(speed);
        observation = runtime->capture_observation(live_scene->waiting_limit(),live_scene->processing_limit());
        live_scene->set_state(*observation,true);
        if (capture_output) for (const char* phase : {"initial","processing","queue","completion"})
            if (std::filesystem::exists(phase_path(*capture_output,phase))) throw std::invalid_argument("Live phase capture output already exists");
    }
    const auto& frame = live_scene ? live_scene->frame() : static_frame;
    const std::array meshes{renderer::make_builtin_mesh(renderer::MeshId::Cube),renderer::make_builtin_mesh(renderer::MeshId::Floor),
        renderer::make_builtin_mesh(renderer::MeshId::Grid),renderer::make_builtin_mesh(renderer::MeshId::Dock),
        renderer::make_builtin_mesh(renderer::MeshId::Rack),renderer::make_builtin_mesh(renderer::MeshId::Pallet),
        renderer::make_builtin_mesh(renderer::MeshId::Forklift),renderer::make_builtin_mesh(renderer::MeshId::Worker)};
    const auto geometry_counts = [&]
    {
        std::array<std::uint32_t,2> counts{};
        for (const auto& object : frame.objects)
        {
            const auto& mesh = meshes.at(static_cast<std::size_t>(object.mesh)-1);
            counts[0] += static_cast<std::uint32_t>(mesh.vertices.size()); counts[1] += static_cast<std::uint32_t>(mesh.indices.size());
        }
        return counts;
    };
    platform::Window window;
    window.create("SimForge Vulkan Sandbox",1280,720,platform::WindowMode::Vulkan);
    renderer::Renderer graphics;
    renderer::RendererOptions options;
    if (smoke_frames) { options.validation = true; options.require_validation = true; options.capture_frames = true; }
    graphics.initialize(window,options); summary(graphics.info());
    if (live)
    {
        const auto metrics = visualization::format_live_metrics(*observation,live_scene->visibility());
        std::cout << "Live initial | " << metrics << " speed=" << speed << " | Space: pause/resume\n";
        window.set_title("SimForge Live | "+metrics+" | Space: pause/resume");
    }
    if (live && smoke_frames) verify_live_ticks(*runtime,*live_scene,graphics,*definition,capture_output);
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
    std::vector<double> live_steady_times;
    double extraction_us = 0, targets_us = 0, animation_us = 0; std::uint64_t live_updates = 0, animation_updates = 0;
    auto last_frame = timer::now(), last_metrics = last_frame;
    bool final_reported = false;
    bool pause_requested = false, resume_requested = false;
    std::optional<timer::time_point> pause_started;
    std::uint64_t paused_tick = 0;
    auto last_progress = timer::now();
    std::uint64_t last_presented = 0;
    bool resized1 = false, resized2 = false, minimized = false, restored = false, resized3 = false;
    std::optional<timer::time_point> minimize_started;
    std::uint64_t skipped_before_minimize = 0, generations_before_minimize = 0;
    while (window.is_open())
    {
        window.poll_events(); if (!window.is_open()) break;
        const auto now = timer::now(); const auto real_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now-last_frame); last_frame = now;
        if (live)
        {
            const bool pause_changed = window.consume_space_press();
            if (pause_changed) { if (runtime->clock().paused()) runtime->resume(); else runtime->pause(); }
            if (smoke_frames && pause_requested && runtime->clock().paused() && !pause_started)
            { pause_started = now; paused_tick = runtime->clock().tick_count(); }
            const auto pacing = smoke_frames ? std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>{
                static_cast<double>(runtime->horizon_ticks())/runtime->clock().ticks_per_second()/speed/static_cast<double>(*smoke_frames-100)}) : real_elapsed;
            runtime->advance_elapsed(pacing);
            if (pause_started && !resume_requested && runtime->clock().tick_count() != paused_tick)
                throw std::runtime_error("Live SDL pause advanced the authoritative tick");
            if (!observation || observation->tick != runtime->clock().tick_count() || observation->capacity != runtime->resource_capacity() || observation->metrics != runtime->metrics())
            {
                const auto began = timer::now(); observation = runtime->capture_observation(live_scene->waiting_limit(),live_scene->processing_limit());
                const auto captured = timer::now(); live_scene->set_state(*observation);
                const auto targeted = timer::now(); ++live_updates;
                extraction_us += std::chrono::duration<double,std::micro>{captured-began}.count();
                targets_us += std::chrono::duration<double,std::micro>{targeted-captured}.count();
            }
            const auto began = timer::now(); live_scene->animate(real_elapsed);
            animation_us += std::chrono::duration<double,std::micro>{timer::now()-began}.count(); ++animation_updates;
            if (pause_changed || now-last_metrics >= std::chrono::seconds{1} || (observation->at_horizon && !final_reported))
            {
                const auto metrics = visualization::format_live_metrics(*observation,live_scene->visibility());
                std::cout << "Live | " << metrics << " speed=" << speed << (runtime->clock().paused() ? " paused" : " running") << '\n';
                window.set_title(std::string{"SimForge Live"}+(runtime->clock().paused() ? " [PAUSED] | " : " | ")+metrics+" | Space: pause/resume");
                last_metrics = now; final_reported = observation->at_horizon;
            }
        }
        if (smoke_frames)
        {
            const auto count = graphics.info().presented_frames;
            if (live && count >= 12 && !pause_requested) { window.request_space_press(); pause_requested = true; }
            if (live && pause_started && !resume_requested && now-*pause_started >= std::chrono::milliseconds{250})
            {
                if (!runtime->clock().paused() || runtime->clock().tick_count() != paused_tick) throw std::runtime_error("Live pause routing failed");
                window.request_space_press(); resume_requested = true;
                std::cout << "Live SDL Space routing verified: authoritative tick " << paused_tick << " held for 250ms; resume requested\n";
            }
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
                if (live)
                {
                    if (!observation->at_horizon) throw std::runtime_error("Live smoke did not reach the scenario horizon");
                    if (!resume_requested || runtime->clock().paused()) throw std::runtime_error("Live smoke did not resume through SDL input");
                    warehouse::WarehouseSimulation reference{*definition}; reference.run();
                    if (auto mismatch = warehouse::first_difference(reference.capture_snapshot(),runtime->capture_snapshot())) throw std::runtime_error(*mismatch);
                    std::cout << "Live final operational snapshot equals headless execution | "
                              << visualization::format_live_metrics(*observation,live_scene->visibility()) << '\n';
                }
                const auto image = graphics.capture_frame();
                if (scenario_path)
                {
                    verify_warehouse(image); submit_and_wait(graphics,frame);
                    if (image.rgba != graphics.capture_frame().rgba)
                        throw std::runtime_error("Static warehouse image changed between identical frames");
                    std::cout << "Static warehouse repeated capture identical; no observed flicker.\n";
                }
                else verify_scene(image);
                if (capture_output) save_capture(image,*capture_output);
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
            render_wall_ms += duration; ++measured_frames;
            if (graphics.info().presented_frames > 100 && steady_times.size() < 10000) steady_times.push_back(duration);
            if (live && !observation->at_horizon && graphics.info().presented_frames > 10 && live_steady_times.size() < 10000) live_steady_times.push_back(duration);
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
    if (live)
    {
        std::cout << "Live CPU means | Logical updates: " << live_updates << " | Extraction: " << (live_updates ? extraction_us/static_cast<double>(live_updates) : 0)
                  << " us | Dynamic targets: " << (live_updates ? targets_us/static_cast<double>(live_updates) : 0)
                  << " us | Frame animation: " << (animation_updates ? animation_us/static_cast<double>(animation_updates) : 0)
                  << " us | Layout generations: " << live_scene->layout_generations() << " | Active visual shipments: " << live_scene->shipments().size() << '\n';
        if (!live_steady_times.empty())
        {
            double mean = 0; for (auto t : live_steady_times) mean += t; mean /= static_cast<double>(live_steady_times.size());
            double variance = 0; for (auto t : live_steady_times) variance += (t-mean)*(t-mean);
            std::cout << "Live in-progress CPU render/present | Samples: " << live_steady_times.size() << " | Mean: " << mean
                      << " ms | Stddev: " << std::sqrt(variance/static_cast<double>(live_steady_times.size())) << " ms; includes FIFO, not GPU execution\n";
        }
    }
    if (smoke_frames)
    {
        if (!restored || !resized3 || info.presented_frames < *smoke_frames || info.swapchain_generations < 4 || !info.validation_enabled || !info.synchronization_validation)
            throw std::runtime_error("Graphics smoke did not exercise all required phases");
        if (info.validation_errors != 0 || info.validation_warnings != 0) throw std::runtime_error("Vulkan validation messages were reported");
        const auto expected = geometry_counts();
        if (info.pipeline_generations != 1 || info.draw_calls != frame.objects.size() || info.submitted_vertices != expected[0] || info.submitted_indices != expected[1] ||
            info.depth_images_created != info.swapchain_generations || info.buffer_allocations != 2+info.swapchain_generations ||
            std::abs(info.aspect_ratio-static_cast<float>(info.width)/static_cast<float>(info.height)) > 1e-6f)
            throw std::runtime_error("Smoke resource reuse, geometry counts or camera aspect mismatch");
        // Reuse the renderer after full shutdown and intentionally release the
        // Window wrapper first. Its native lease must survive Vulkan cleanup.
        window.destroy(); graphics.shutdown();
        window.create("SimForge Vulkan Lifecycle Smoke",640,480,platform::WindowMode::Vulkan);
        graphics.initialize(window,options);
        if (live)
        {
            warehouse::WarehouseSimulation fresh{*definition};
            live_scene->set_state(fresh.capture_observation(live_scene->waiting_limit(),live_scene->processing_limit()),true);
            if (!live_scene->shipments().empty()) throw std::runtime_error("Live reinitialization retained stale shipments");
        }
        for (int i = 0; i < 3; ++i) { window.poll_events(); (void)graphics.render_frame(frame); }
        window.destroy();
        if (graphics.render_frame(frame)) throw std::runtime_error("Renderer presented after window close");
        graphics.shutdown(); info = graphics.info();
        if (info.validation_errors != 0 || info.validation_warnings != 0) throw std::runtime_error("Validation messages during lifecycle smoke");
        std::cout << "Graphics smoke passed: 3D geometry, image/color checks, depth occlusion, repeated resize, minimize/restore, close, reinitialize and native-window lease; validation errors: 0\n";
    }
    return 0;
}
