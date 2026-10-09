#include "viewer.hpp"
#include "dashboard.hpp"
#include <simforge/platform/window.hpp>
#include <simforge/renderer/renderer.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <stdexcept>

namespace
{
using namespace simforge;
using Clock = std::chrono::steady_clock;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void save(const renderer::CapturedFrame& frame, const std::filesystem::path& path)
{
    if (std::filesystem::exists(path)) throw std::runtime_error("Viewer capture output already exists");
    std::ofstream file{path,std::ios::binary}; if (!file) throw std::runtime_error("Cannot open viewer capture");
    file << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
    for (std::size_t i = 0; i < frame.rgba.size(); i += 4) file.write(reinterpret_cast<const char*>(frame.rgba.data()+i),3);
    if (!file) throw std::runtime_error("Cannot save viewer capture");
}
std::filesystem::path capture_path(const std::filesystem::path& base, const char* suffix)
{ return base.parent_path()/(base.stem().string()+"-"+suffix+".ppm"); }
void apply(viewer::ViewerController& controller, const ViewerCommand& command)
{
    switch(command.action)
    {
    case ViewerAction::Toggle: controller.toggle_playback(); break;
    case ViewerAction::Step: controller.step(); break;
    case ViewerAction::Speed: controller.set_speed(command.speed); break;
    case ViewerAction::Reset: controller.reset(); break;
    case ViewerAction::Seek: controller.seek(command.tick); break;
    case ViewerAction::ClearSelection: controller.select(std::nullopt); break;
    }
}
std::optional<viewer::InputEvent> input(const SDL_Event& e, std::uint32_t window)
{
    using namespace viewer;
    const auto button = [](Uint8 b){ return b == SDL_BUTTON_LEFT ? MouseButton::Left : b == SDL_BUTTON_MIDDLE ? MouseButton::Middle : MouseButton::Other; };
    if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST && e.window.windowID == window) return InputEvent{InputKind::FocusLost};
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        if (e.button.windowID != window) return std::nullopt;
        InputEvent result{e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? InputKind::Down : InputKind::Up};
        result.position = {e.button.x,e.button.y}; result.button = button(e.button.button); return result;
    }
    if (e.type == SDL_EVENT_MOUSE_MOTION && e.motion.windowID == window)
    { InputEvent result{InputKind::Move}; result.position = {e.motion.x,e.motion.y}; return result; }
    if (e.type == SDL_EVENT_MOUSE_WHEEL && e.wheel.windowID == window)
    { InputEvent result{InputKind::Wheel}; result.position = {e.wheel.mouse_x,e.wheel.mouse_y}; result.wheel = e.wheel.y*(e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f); return result; }
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.windowID == window)
    {
        InputEvent result{InputKind::Key}; result.repeat = e.key.repeat;
        switch(e.key.key)
        {
        case SDLK_SPACE: result.key = Key::Space; break; case SDLK_F: result.key = Key::Frame; break;
        case SDLK_R: result.key = Key::ResetCamera; break; case SDLK_ESCAPE: result.key = Key::Escape; break;
        case SDLK_RIGHT: result.key = Key::Step; break; default: break;
        }
        return result;
    }
    return std::nullopt;
}
void push(const SDL_Event& source)
{ auto event = source; if (!SDL_PushEvent(&event)) throw std::runtime_error("Push viewer diagnostic SDL event"); }
void motion(std::uint32_t id, glm::vec2 p)
{ SDL_Event e{}; e.type = SDL_EVENT_MOUSE_MOTION; e.motion.windowID = id; e.motion.x = p.x; e.motion.y = p.y; push(e); }
void button(std::uint32_t id, glm::vec2 p, bool down, Uint8 which = SDL_BUTTON_LEFT)
{
    motion(id,p); SDL_Event e{}; e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.windowID = id; e.button.button = which; e.button.down = down; e.button.x = p.x; e.button.y = p.y; push(e);
}
struct Measurements
{
    double ui_us{0}, dynamic_us{0}, input_us{0}, present_ms{0}, total_ms{0}; std::uint64_t frames{0};
};
void verify_geometry(const renderer::CapturedFrame& image, const viewer::Viewport& viewport)
{
    const auto p = viewport.pixels(); std::size_t geometry = 0;
    for (std::uint32_t y = p.y+40; y < std::min(p.y+p.height,image.height); ++y)
        for (std::uint32_t x = p.x; x < std::min(p.x+p.width,image.width); ++x)
        {
            const auto i = (static_cast<std::size_t>(y)*image.width+x)*4;
            if (std::abs(static_cast<int>(image.rgba[i])-44)+std::abs(static_cast<int>(image.rgba[i+1])-60)+std::abs(static_cast<int>(image.rgba[i+2])-77) > 50) ++geometry;
        }
    require(geometry > 1000,"GPU readback has no warehouse geometry in the 3D viewport");
}
}
int run_viewer(std::optional<std::uint64_t> smoke_frames, const std::optional<std::filesystem::path>& capture_output,
    const std::filesystem::path& scenario_path, double speed)
{
    using namespace simforge;
    const auto definition = scenario::load_scenario(scenario_path);
    viewer::ViewerController controller{definition,speed};
    if (capture_output)
    {
        if (!smoke_frames) throw std::invalid_argument("Viewer capture requires graphics smoke");
        if (std::filesystem::exists(*capture_output)) throw std::invalid_argument("Viewer capture output already exists");
        for (const char* suffix : {"overview","queue","selected","paused","queue-buildup","final","size-640","size-800","size-1280","size-1600","size-1920"})
            if (std::filesystem::exists(capture_path(*capture_output,suffix))) throw std::invalid_argument("Viewer capture output already exists");
    }
    platform::Window window; window.create("SimForge | Interactive warehouse",1280,720,platform::WindowMode::Vulkan);
    if (!SDL_SetWindowMinimumSize(window.native_handle(),640,480)) throw std::runtime_error("Set viewer minimum window size");
    renderer::Renderer graphics; renderer::RendererOptions options; options.ui = true;
    options.validation = options.require_validation = smoke_frames.has_value(); options.capture_frames = smoke_frames.has_value();
    graphics.initialize(window,options); style_dashboard();
    const auto initial_info = graphics.info();
    std::cout << "Interactive warehouse | GPU: " << initial_info.gpu_name << " | API: " << initial_info.api_version << " | Loader: " << initial_info.loader_version
        << " | Format: " << initial_info.surface_format << " | Present: " << initial_info.present_mode << " | ImGui: " << IMGUI_VERSION << '\n';
    const auto id = SDL_GetWindowID(window.native_handle());
    DashboardFrame dashboard; DashboardState ui_state; std::vector<ViewerCommand> pending; Measurements measurements;
    const auto frame = [&](std::chrono::nanoseconds elapsed)
    {
        const auto frame_started = Clock::now();
        std::vector<viewer::InputEvent> events;
        window.poll_events([&](const SDL_Event& e){ graphics.process_ui_event(e); if (const auto converted = input(e,id)) events.push_back(*converted); });
        if (!window.is_open()) return false;
        bool discontinuity = false;
        for (const auto& c : pending) { apply(controller,c); discontinuity |= c.action == ViewerAction::Seek || c.action == ViewerAction::Reset || c.action == ViewerAction::Toggle; }
        pending.clear();
        const auto began = Clock::now(); controller.advance(discontinuity ? std::chrono::nanoseconds{0} : elapsed);
        measurements.dynamic_us += std::chrono::duration<double,std::micro>{Clock::now()-began}.count();
        if (!graphics.begin_ui_frame())
        {
            for (const auto& e : events) if (e.kind == viewer::InputKind::FocusLost) controller.input(e,{});
            return false;
        }
        const auto input_started = Clock::now();
        const auto& io = ImGui::GetIO();
        for (const auto& e : events) controller.input(e,{io.WantCaptureMouse,io.WantCaptureKeyboard || io.WantTextInput});
        measurements.input_us += std::chrono::duration<double,std::micro>{Clock::now()-input_started}.count();
        const auto ui_started = Clock::now();
        controller.set_viewport(dashboard_viewport(controller)); dashboard = draw_dashboard(controller,ui_state); pending = dashboard.commands;
        measurements.ui_us += std::chrono::duration<double,std::micro>{Clock::now()-ui_started}.count();
        graphics.end_ui_frame(); const auto present_started = Clock::now();
        const bool presented = graphics.render_frame(controller.present());
        measurements.present_ms += std::chrono::duration<double,std::milli>{Clock::now()-present_started}.count();
        measurements.total_ms += std::chrono::duration<double,std::milli>{Clock::now()-frame_started}.count(); ++measurements.frames;
        return presented;
    };
    const auto submit = [&]
    {
        for (int attempt = 0; attempt < 100; ++attempt) { if (frame(std::chrono::nanoseconds{0})) return; std::this_thread::sleep_for(std::chrono::milliseconds{10}); }
        throw std::runtime_error("Viewer could not present a diagnostic frame");
    };
    const auto capture = [&](const char* suffix)
    {
        const auto image = graphics.capture_frame();
        verify_geometry(image,controller.viewport());
        if (capture_output) save(image,capture_path(*capture_output,suffix));
        return image;
    };
    const auto click = [&](glm::vec2 p)
    {
        button(id,p,true); submit(); button(id,p,false); submit(); submit();
    };
    if (smoke_frames)
    {
        submit(); submit(); const auto overview = capture("overview");
        require(graphics.info().ui_vertices > 1000 && graphics.info().ui_draw_calls >= 3,"Dashboard emitted no meaningful UI draw data");
        // Readback must contain readable light text in the metrics panel, not just its background.
        std::size_t text_pixels = 0;
        for (std::uint32_t y = 70; y < 550; ++y) for (std::uint32_t x = 10; x < 190; ++x)
        { const auto i = (static_cast<std::size_t>(y)*overview.width+x)*4; if (overview.rgba[i] > 150 && overview.rgba[i+1] > 150 && overview.rgba[i+2] > 150) ++text_pixels; }
        require(text_pixels > 1000,"Dashboard text was not present in GPU readback");
        const auto camera = controller.orbit_camera().camera().view_projection();
        click(dashboard.play); require(controller.runtime().clock().paused(),"Injected SDL Pause button failed");
        require(controller.orbit_camera().camera().view_projection() == camera,"UI button changed camera");
        const auto tick = controller.observation().tick; click(dashboard.step);
        require(controller.observation().tick == tick+1,"Step button did not advance exactly one tick");
        click(dashboard.speed);
        SDL_Event space{}; space.type = SDL_EVENT_KEY_DOWN; space.key.windowID = id; space.key.key = SDLK_SPACE; space.key.scancode = SDL_SCANCODE_SPACE; space.key.down = true;
        push(space); submit(); space.type = SDL_EVENT_KEY_UP; space.key.down = false; push(space); submit();
        require(controller.runtime().clock().paused(),"Keyboard text focus failed to capture Space");
        const auto seek_slider = [&](float fraction)
        {
            const auto start = dashboard.timeline_min, end = dashboard.timeline_max;
            click({start.x+(end.x-start.x)*fraction,(start.y+end.y)*0.5f});
            warehouse::WarehouseSimulation reference{definition}; reference.advance_to(controller.observation().tick);
            require(reference.capture_snapshot() == controller.runtime().capture_snapshot(),"Timeline UI target disagrees with authoritative events");
        };
        seek_slider(0.20f); const auto forward = controller.observation().tick;
        require(forward > tick+1,"Forward timeline click failed"); seek_slider(0.02f);
        require(controller.observation().tick < forward,"Backward timeline click failed");
        controller.seek(std::min<std::uint64_t>(controller.observation().horizon,1200ULL*definition.simulation.tick_rate)); submit();
        const auto before_orbit = capture("queue");
        auto p = dashboard.viewport.origin+dashboard.viewport.size*0.5f;
        button(id,p,true); submit(); motion(id,p+glm::vec2{70,20}); submit(); button(id,p+glm::vec2{70,20},false); submit();
        SDL_Event wheel{}; wheel.type = SDL_EVENT_MOUSE_WHEEL; wheel.wheel.windowID = id;
        wheel.wheel.mouse_x = p.x; wheel.wheel.mouse_y = p.y; wheel.wheel.y = 2; push(wheel); submit();
        require(controller.orbit_camera().camera().view_projection() != camera,"Injected SDL camera input had no effect");
        require(graphics.capture_frame().rgba != before_orbit.rgba,"Camera navigation did not change GPU pixels");
        const auto pan_target = controller.orbit_camera().target();
        button(id,p,true,SDL_BUTTON_MIDDLE); submit(); motion(id,p+glm::vec2{24,16}); submit(); button(id,p+glm::vec2{24,16},false,SDL_BUTTON_MIDDLE); submit();
        require(controller.orbit_camera().target() != pan_target,"Injected SDL middle-button pan failed");
        button(id,p,true); submit(); const auto before_focus_loss = controller.orbit_camera().camera().view_projection();
        SDL_Event focus{}; focus.type = SDL_EVENT_WINDOW_FOCUS_LOST; focus.window.windowID = id; push(focus); submit();
        motion(id,p+glm::vec2{50,10}); submit(); button(id,p+glm::vec2{50,10},false); submit();
        require(controller.orbit_camera().camera().view_projection() == before_focus_loss,"Focus loss retained a camera drag");
        focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED; push(focus); submit();
        controller.input({viewer::InputKind::Key,{}, {},viewer::MouseButton::Other,viewer::Key::Frame},{}); submit();
        bool selected_shipment = false;
        // Probe projected centers and use the real nearest-hit picker, avoiding assumptions about occlusion.
        for (const auto& o : controller.scene().selectables()) if (o.key.category == viewer::VisualCategory::Shipment)
        {
            auto clip = controller.orbit_camera().camera().view_projection()*glm::vec4{(o.bounds.minimum+o.bounds.maximum)*0.5f,1};
            if (clip.w <= 0) continue; clip /= clip.w;
            const auto point = dashboard.viewport.origin+(glm::vec2{clip.x,clip.y}+1.0f)*0.5f*dashboard.viewport.size;
            const auto ray = viewer::pointer_ray(controller.orbit_camera().camera(),dashboard.viewport,point);
            if (ray && viewer::pick(*ray,controller.scene().selectables()) == o.key)
            {
                const auto key = o.key; // Submitting animates/rebuilds selectable values.
                click(point); selected_shipment = controller.selection() == key; break;
            }
        }
        require(selected_shipment && controller.inspect()->shipment.has_value(),"Shipment click/inspector failed");
        submit(); const auto selected = capture("selected");
        require(selected.rgba != before_orbit.rgba,"Selection/inspector did not change readback");
        const auto selected_key = controller.selection(); controller.select(std::nullopt);
        require(graphics.render_frame(controller.present()),"Could not present selection tint probe");
        require(graphics.capture_frame().rgba != selected.rgba,"Selected-object tint did not change GPU pixels with the same camera/UI");
        controller.select(selected_key); submit();
        const auto selected_info = controller.inspect();
        require(selected_info->shipment->id == controller.selection()->id,"Inspector fabricated shipment identity");
        require(!selected_info->shipment->completion_tick,"Inspector invented active completion timestamp");
        const auto frozen = controller.observation().tick; frame(std::chrono::seconds{1});
        require(controller.observation().tick == frozen,"Paused wall time advanced runtime"); capture("paused");
        controller.seek(std::min<std::uint64_t>(controller.observation().horizon,7200ULL*definition.simulation.tick_rate)); submit(); capture("queue-buildup");
        controller.seek(0); submit(); require(controller.observation().tick == 0 && controller.observation().metrics.arrived == 1,"Backward seek zero did not replay arrival zero");
        click(dashboard.reset); require(controller.runtime().capture_snapshot() == warehouse::WarehouseSimulation{definition}.capture_snapshot(),"Reset did not restore pristine runtime");
        require(!controller.selection(),"Reset retained selection");
        click(dashboard.play); require(!controller.runtime().clock().paused(),"Resume button failed");
        controller.set_speed(900); frame(std::chrono::milliseconds{10}); require(controller.observation().tick > 0,"Resume/speed failed");
        for (const auto size : {platform::PixelSize{1600,900},platform::PixelSize{1920,1080},platform::PixelSize{800,520},platform::PixelSize{640,480},platform::PixelSize{1280,720}})
        {
            window.resize(size.width,size.height); submit(); require(controller.viewport().pixels().width > 0,"Resize invalidated viewport");
            const auto suffix = "size-"+std::to_string(size.width); capture(suffix.c_str());
        }
        window.minimize(); window.poll_events([&](const SDL_Event& e){graphics.process_ui_event(e);});
        require(!graphics.begin_ui_frame(),"Minimized UI frame was not skipped"); window.restore(); submit();
        controller.seek(controller.observation().horizon); submit(); capture("final");
        warehouse::WarehouseSimulation reference{definition}; reference.run();
        require(controller.runtime().capture_snapshot() == reference.capture_snapshot(),"Seek horizon disagrees with headless runtime");
        require(graphics.info().pipeline_generations == 1 && graphics.info().ui_pipeline_generations == 1,"Ordinary interaction recreated a graphics pipeline");
        std::cout << "Viewer smoke: dashboard GPU text, injected SDL camera/picking/buttons, real inspector, pause/step/reset/seek, sizes, minimize/restore, exact headless snapshot verified.\n";
    }
    auto previous = Clock::now(); std::uint64_t count = 0;
    while (window.is_open() && (!smoke_frames || count < *smoke_frames))
    {
        const auto now = Clock::now(); const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now-previous); previous = now;
        if (frame(elapsed)) ++count; else std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if (smoke_frames && capture_output) save(graphics.capture_frame(),*capture_output);
    graphics.shutdown(); const auto info = graphics.info();
    std::cout << "Viewer frames: " << info.presented_frames << " | 3D draws: " << info.draw_calls << " | UI draws: " << info.ui_draw_calls << " | UI vertices: " << info.ui_vertices
              << " | Scene/UI pipeline generations: " << info.pipeline_generations << '/' << info.ui_pipeline_generations
              << " | Validation errors/warnings: " << info.validation_errors << '/' << info.validation_warnings << '\n';
    if (measurements.frames) std::cout << "CPU means (includes present synchronization): UI build " << measurements.ui_us/measurements.frames
        << " us | dynamic " << measurements.dynamic_us/measurements.frames << " us | input " << measurements.input_us/measurements.frames
        << " us | record/submit/present " << measurements.present_ms/measurements.frames << " ms\n";
    if (measurements.frames) std::cout << "CPU total frame mean: " << measurements.total_ms/measurements.frames << " ms (poll, commands, runtime, UI, GPU fence waits and present; no GPU timer)\n";
    require(!info.validation_errors && !info.validation_warnings,"Viewer Vulkan validation was not clean");
    if (smoke_frames)
    {
        controller.reset(); graphics.initialize(window,options); style_dashboard(); submit(); graphics.shutdown();
        require(!graphics.info().validation_errors && !graphics.info().validation_warnings,"UI reinitialization validation failed");
        window.request_close(); window.poll_events(); require(!window.is_open(),"Viewer close event failed");
        std::cout << "Viewer clean UI/GPU shutdown and reinitialization verified.\n";
    }
    window.destroy(); return 0;
}
