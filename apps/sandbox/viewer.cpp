#include "viewer.hpp"
#include "dashboard.hpp"
#include "workspace_panel.hpp"
#include "project_panel.hpp"
#include <simforge/workspace/experiment_execution.hpp>
#include "capture.hpp"
#include <simforge/platform/window.hpp>
#include <simforge/renderer/renderer.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <thread>
#include <stdexcept>

namespace
{
using namespace simforge;
using Clock = std::chrono::steady_clock;
// Opt-in graphics diagnostics hold actual work at a checkpoint, with no production delay.
struct ExperimentGate
{
    std::mutex mutex;
    std::condition_variable cv;
    bool reached{false}, released{false};
    std::string target_phase{"Evaluating candidate"};
    void checkpoint(std::string_view phase)
    {
        if (phase != target_phase) return;
        std::unique_lock lock{mutex}; if (reached) return;
        reached = true; cv.notify_all();
        if (!cv.wait_for(lock,std::chrono::seconds{10},[&]{ return released; })) throw std::runtime_error("Graphics experiment gate timed out");
    }
    void wait()
    {
        std::unique_lock lock{mutex};
        if (!cv.wait_for(lock,std::chrono::seconds{10},[&]{return reached;})) throw std::runtime_error("Graphics worker did not reach candidate");
    }
    void release() { std::lock_guard lock{mutex}; released=true; cv.notify_all(); }
    ~ExperimentGate() { release(); }
};
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::filesystem::path capture_path(const std::filesystem::path& base, const char* suffix)
{ return base.parent_path()/(base.stem().string()+"-"+suffix+base.extension().string()); }
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
    const std::filesystem::path& scenario_path, double speed, bool show_workspace, const simforge::scenario::ScenarioDefinition* prepared_definition, const std::optional<std::filesystem::path>& project_path)
{
    using namespace simforge;
    const auto definition = prepared_definition ? *prepared_definition : project_path ?
        scenario::ScenarioDefinition{1,"Warehouse",{60,28800,0},scenario::WarehouseConfig{2,4,2,300,600}} : scenario::load_scenario(scenario_path);
    workspace::ScenarioWorkspace workspace{definition,speed};
    workspace::Project project{workspace};
    if(project_path) project.open(*project_path);
    auto& controller = workspace.view(); // Existing live smoke exercises the preserved baseline.
    if (capture_output)
    {
        if (!smoke_frames) throw std::invalid_argument("Viewer capture requires graphics smoke");
        if (std::filesystem::exists(*capture_output)) throw std::invalid_argument("Viewer capture output already exists");
        for (const char* suffix : {"overview","queue","selected","paused","queue-buildup","final","collapsed","focus","inspector-collapsed","size-640","size-800","size-1024","size-1280","size-1600","size-1920","dpi-100","dpi-125","dpi-150","dpi-200","workspace-editor","workspace-invalid","workspace-candidates","workspace-results","workspace-running","workspace-cancelled","workspace-candidate","workspace-inspector","workspace-probes","workspace-checkpoint","workspace-compact","workspace-dpi200","custom-initial","custom-processing","custom-candidate-final","custom-file-final"})
            if (std::filesystem::exists(capture_path(*capture_output,suffix))) throw std::invalid_argument("Viewer capture output already exists");
        for (const auto* extension : {"-comparison.json","-comparison.csv"})
            if (std::filesystem::exists(capture_output->parent_path()/(capture_output->stem().string()+extension)))
                throw std::invalid_argument("Comparison capture output already exists");
        for(const char* stage:{"simulation","editor","analysis","candidate","shipment"}) for(int size:{1280,1600,1920})
            if(std::filesystem::exists(capture_path(*capture_output,(std::string{"design-"}+stage+"-"+std::to_string(size)).c_str())))
                throw std::invalid_argument("Design capture output already exists");
    }
    platform::Window window; window.create("SimForge | Interactive warehouse",1280,720,platform::WindowMode::Vulkan);
    if (!SDL_SetWindowMinimumSize(window.native_handle(),640,480)) throw std::runtime_error("Set viewer minimum window size");
    renderer::Renderer graphics; renderer::RendererOptions options; options.ui = true;
    options.validation = options.require_validation = smoke_frames.has_value(); options.capture_frames = smoke_frames.has_value();
    graphics.initialize(window,options); FailureCapture failure_capture{graphics}; style_dashboard();
    const auto initial_info = graphics.info();
    std::cout << "Interactive warehouse | GPU: " << initial_info.gpu_name << " | API: " << initial_info.api_version << " | Loader: " << initial_info.loader_version
        << " | Format: " << initial_info.surface_format << " | Present: " << initial_info.present_mode << " | ImGui: " << IMGUI_VERSION << '\n';
    const auto id = SDL_GetWindowID(window.native_handle());
    DashboardFrame dashboard; DashboardState ui_state; std::vector<ViewerCommand> pending; Measurements measurements;
    WorkspaceUI workspace_ui; ProjectUI project_ui;
    if(!smoke_frames) {
        if(const auto* pref=SDL_GetPrefPath("SimForge","SimForge")) {project_ui.recovery_path=std::filesystem::path(pref)/"session.recovery";
            project_ui.show_recovery=project_ui.recovery_deferred=workspace::Project::recovery_available(project_ui.recovery_path);}
    }
    ui_state.panels.workspace = show_workspace;
    if(show_workspace) ui_state.panels.mode=viewer::ApplicationMode::Editor;
    workspace_ui.path = scenario_path.string();
    if(project_path) { const auto& p=project.preferences();
        ui_state.panels={p.metrics,p.inspector,p.metrics_explicit,p.focus,p.workspace,static_cast<viewer::ApplicationMode>(p.application_mode)};
        workspace_ui.mode=static_cast<int>(p.comparison_mode);workspace_ui.objective=static_cast<int>(p.objective);
        workspace_ui.current_checkpoint=p.current_checkpoint;workspace_ui.checkpoint_seconds=p.checkpoint_seconds;workspace_ui.editing=p.selected_candidate;
        workspace_ui.notes=p.notes;workspace_ui.result_name=p.result_name;workspace_ui.current_tab=p.editor_tab;
        if(p.application_mode==1) workspace_ui.requested_tab=p.editor_tab;
        if(workspace.report()) {workspace_ui.result_order=workspace.report()->ranking;for(const auto& c:workspace.report()->candidates) if(!c.rank) workspace_ui.result_order.push_back(c.candidate.name);}
    }
    bool discard_next_elapsed = false;
    std::optional<float> diagnostic_scale;
    const auto frame = [&](std::chrono::nanoseconds elapsed)
    {
        const auto frame_started = Clock::now();
        std::vector<viewer::InputEvent> events;
        window.poll_events([&](const SDL_Event& e){ graphics.process_ui_event(e); if (const auto converted = input(e,id)) events.push_back(*converted); },[&] {
            if(smoke_frames || project_ui.close_requested) return true;
            if(project.dirty() || workspace.execution().progress().active()) {project_ui.confirmation=ProjectAction::Close;return false;}
            return true;
        });
        if (!window.is_open()) return false;
        if (workspace.poll_experiment()) {
            workspace_ui.result_order=workspace.report()->ranking;
            for (const auto& r:workspace.report()->candidates) if (!r.rank) workspace_ui.result_order.push_back(r.candidate.name);
            workspace_ui.result_name=workspace.report()->ranking.empty()?"Baseline":workspace.report()->ranking.front();
        }
        capture_project_preferences(project,ui_state,workspace_ui);
        process_project(project,workspace,project_ui,ui_state,workspace_ui);
        if(project_ui.close_requested) {window.request_close();return false;}
        const bool workspace_commands = !workspace_ui.pending.empty() || workspace_ui.dialog_used || project_ui.changed_workspace || project_ui.dialog_used;
        project_ui.dialog_used=false;
        workspace_ui.dialog_used=false;
        for (const auto& c : workspace_ui.pending)
        {
            try
            {
                apply_workspace(workspace,c); workspace_ui.error.clear();
                if (c.action == WorkspaceAction::Edit)
                {
                    workspace_ui.editing = c.id; workspace_ui.requested_tab = 0;
                    const auto found = std::find_if(workspace.candidates().begin(),workspace.candidates().end(),[&](const auto& candidate){return candidate.id == c.id;});
                    if (found != workspace.candidates().end()) workspace_ui.notes = found->notes;
                }
                if (c.action == WorkspaceAction::Open || c.action == WorkspaceAction::Revert) workspace_ui.editing.reset();
                if (c.action == WorkspaceAction::Compare && workspace.report())
                {
                    workspace_ui.result_order=workspace.report()->ranking;
                    for(const auto& r:workspace.report()->candidates) if(!r.rank) workspace_ui.result_order.push_back(r.candidate.name);
                    if(workspace_ui.result_name.empty()) workspace_ui.result_name=workspace.report()->ranking.empty()?"Baseline":workspace.report()->ranking.front();
                    ui_state.panels.mode=viewer::ApplicationMode::Analysis; ui_state.panels.workspace=true; ui_state.panel_changed=ui_state.workspace_changed=true;
                }
                if(c.action==WorkspaceAction::View || c.action==WorkspaceAction::Run || c.action==WorkspaceAction::Baseline ||
                    c.action==WorkspaceAction::SelectRun || c.action==WorkspaceAction::RemoveRun || c.action==WorkspaceAction::Live || c.action==WorkspaceAction::LoadRun ||
                    c.action==WorkspaceAction::SimulateCandidate || c.action==WorkspaceAction::SimulateResult)
                { workspace_ui.requested_tab=-1; ui_state.panels.mode=viewer::ApplicationMode::Simulation; ui_state.panels.workspace=false; ui_state.panel_changed=ui_state.workspace_changed=true;
                  ui_state.seeking=false; ui_state.last_selection.reset(); ui_state.feedback.clear(); ui_state.feedback_tick.reset(); }
            }
            catch (const std::exception& e) { workspace_ui.error = e.what(); }
        }
        workspace_ui.pending.clear();
        if(workspace_ui.requested_tab>=0)
        { ui_state.panels.mode=workspace_ui.requested_tab==2?viewer::ApplicationMode::Analysis:viewer::ApplicationMode::Editor; ui_state.panels.workspace=true; }
        workspace_ui.analysis_page=ui_state.panels.mode==viewer::ApplicationMode::Analysis;
        auto& controller = workspace.view();
        if (workspace_commands) pending.clear(); // Old-view controls must not cross a scene switch.
        if (ui_state.feedback_tick && !ui_state.feedback_error && (*ui_state.feedback_tick != controller.observation().tick || ui_state.feedback_paused != controller.runtime().clock().paused()))
        { ui_state.feedback.clear(); ui_state.feedback_tick.reset(); }
        bool discontinuity = workspace_commands || discard_next_elapsed;
        discard_next_elapsed = workspace_commands; // Synchronous experiments never become baseline wall input.
        for (const auto& c : pending)
        {
            try
            {
                apply(controller,c); ui_state.feedback_error = false;
                if (c.action == ViewerAction::Reset) ui_state.feedback = "Reset complete. Initial state restored and paused.";
                if (c.action == ViewerAction::Seek) ui_state.feedback = "Seek complete: "+viewer::format_time(c.tick,controller.observation().tick_rate)+" (paused)";
                ui_state.feedback_tick = controller.observation().tick; ui_state.feedback_paused = controller.runtime().clock().paused();
                discontinuity |= c.action == ViewerAction::Seek || c.action == ViewerAction::Reset || c.action == ViewerAction::Toggle;
            }
            catch (const std::exception& error) { ui_state.feedback = error.what(); ui_state.feedback_error = true; }
        }
        pending.clear();
        const auto began = Clock::now(); controller.advance(discontinuity ? std::chrono::nanoseconds{0} : elapsed);
        measurements.dynamic_us += std::chrono::duration<double,std::micro>{Clock::now()-began}.count();
        const auto pixels = window.pixel_size();
        const float framebuffer = pixels.width && window.width() ? static_cast<float>(pixels.width)/window.width() : 1;
        const float scale = diagnostic_scale.value_or(std::clamp(SDL_GetWindowDisplayScale(window.native_handle())/framebuffer,1.0f,2.0f));
        if (scale != ui_state.scale) { ui_state.scale = scale; style_dashboard(scale); }
        if (!graphics.begin_ui_frame())
        {
            for (const auto& e : events) if (e.kind == viewer::InputKind::FocusLost) controller.input(e,{});
            return false;
        }
        const auto input_started = Clock::now();
        const auto& io = ImGui::GetIO();
        const auto input_layout = viewer::dashboard_layout({io.DisplaySize.x,io.DisplaySize.y},{io.DisplayFramebufferScale.x,io.DisplayFramebufferScale.y},
            ui_state.scale,ui_state.panels,controller.selection().has_value(),ui_state.feedback_error);
        controller.set_viewport(input_layout.viewport,ui_state.panel_changed && !ui_state.workspace_changed);
        ui_state.panel_changed = false; ui_state.workspace_changed = false;
        for (const auto& e : events) controller.input(e,{io.WantCaptureMouse || input_layout.workspace_overlay,io.WantCaptureKeyboard || io.WantTextInput});
        measurements.input_us += std::chrono::duration<double,std::micro>{Clock::now()-input_started}.count();
        const auto ui_started = Clock::now();
        if (controller.selection() != ui_state.last_selection)
        { if (controller.selection()) { ui_state.panels.inspector = true; ui_state.select_inspector_tab = true; } ui_state.last_selection = controller.selection(); }
        controller.set_viewport(dashboard_viewport(controller,ui_state),ui_state.panel_changed); ui_state.panel_changed = false;
        ui_state.results_available=workspace.report().has_value();
        ui_state.viewing_baseline=workspace.viewing_baseline();
        ui_state.run_identity=workspace.viewed_identity(); ui_state.active_run=workspace.active_run_id();
        ui_state.runs={{0,"Original Baseline: "+workspace.baseline().name}};
        for (const auto& run : workspace.runs()) ui_state.runs.emplace_back(run.id,run.controller->name()+" / Run #"+std::to_string(run.id));
        ui_state.project_name=project.info().name;const auto project_location=project.info().path.u8string();ui_state.project_location=std::string(project_location.begin(),project_location.end());ui_state.project_dirty=project.dirty();
        dashboard = draw_dashboard(controller,ui_state); pending = dashboard.commands;
        if(ui_state.select_run) { workspace_ui.pending.push_back({WorkspaceAction::SelectRun,{},{},*ui_state.select_run}); ui_state.select_run.reset(); }
        if(ui_state.remove_run) { workspace_ui.pending.push_back({WorkspaceAction::RemoveRun,{},{},*ui_state.remove_run}); ui_state.remove_run.reset(); }
        if(ui_state.return_live) { workspace_ui.pending.push_back({WorkspaceAction::Live}); ui_state.return_live=false; }
        if(ui_state.context_action)
        {
            ui_state.context_action=false;
            if(ui_state.panels.mode==viewer::ApplicationMode::Editor) workspace_ui.pending.push_back({WorkspaceAction::Add,{},workspace_ui.notes});
            else if(ui_state.panels.mode==viewer::ApplicationMode::Analysis) workspace_ui.pending.push_back({WorkspaceAction::View,workspace_ui.result_name=="Baseline"?"":workspace_ui.result_name});
            else if(!workspace.viewing_baseline()) workspace_ui.pending.push_back({WorkspaceAction::Baseline});
            else { viewer::InputEvent e{viewer::InputKind::Key}; e.key=viewer::Key::Frame; controller.input(e,{}); }
        }
        if (ui_state.panels.workspace) draw_workspace(workspace,workspace_ui,dashboard);
        capture_project_preferences(project,ui_state,workspace_ui);
        draw_project(project,workspace,project_ui,ui_state,dashboard);
        if(!smoke_frames && !project_ui.recovery_deferred && !project_ui.recovery_path.empty()) {
            try { (void)project.recover_if_due(project_ui.recovery_path,Clock::now()); }
            catch(const std::exception& ex) {project_ui.error=std::string{"Recovery save failed: "}+ex.what();}
        }
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
        // Keep the published diagnostic view unobstructed by pointer-hover help.
        motion(id,workspace.view().viewport().origin+workspace.view().viewport().size-glm::vec2{5,5}); submit();
        const auto image = graphics.capture_frame();
        if(dashboard.layout.workspace_overlay) require(graphics.info().ui_vertices>100,"Compact editor page did not render UI");
        else verify_geometry(image,workspace.view().viewport());
        if (capture_output) write_capture(image,capture_path(*capture_output,suffix));
        return image;
    };
    const auto click = [&](glm::vec2 p)
    {
        button(id,p,true); submit(); button(id,p,false); submit(); submit();
    };
    const auto design_captures=[&](const char* stage)
    {
        if(!capture_output) return;
        for(const auto size:{std::pair{1280,720},std::pair{1600,900},std::pair{1920,1080}})
        {
            window.resize(size.first,size.second); submit(); submit();
            const auto suffix=std::string{"design-"}+stage+"-"+std::to_string(size.first); capture(suffix.c_str());
        }
        window.resize(1280,720); submit(); submit();
    };
    if(smoke_frames && project_path) {
        const auto saved=workspace.view().runtime().capture_snapshot();const auto saved_clock=workspace.view().runtime().clock().capture_snapshot();
        const auto saved_camera=workspace.view().orbit_camera().pose();const auto active=workspace.active_run_id();
        submit();submit();diagnostic_failure_probe();capture("project-restored");
        require(workspace.active_run_id()==active && workspace.view().runtime().capture_snapshot()==saved,"Fresh process changed restored run/time");
        require(workspace.view().orbit_camera().pose()==saved_camera,"Fresh process changed restored camera");
        require(workspace.view().speed()==saved_clock.time_scale && workspace.view().runtime().clock().paused()==saved_clock.paused,"Fresh process lost playback state");
        require(workspace.runs().size()==3 && workspace.candidates().size()==1 && workspace.report().has_value(),"Fresh process lost workspace analyses or runs");
        require(workspace.view().scene().resources().size()>0 && workspace.view().observation().capacity.forklifts==3,"Restored geometry mismatch");
        auto uninterrupted=warehouse::WarehouseSimulation::create_from(saved);uninterrupted.run();
        workspace.view().seek(workspace.view().observation().horizon);submit();
        require(workspace.view().runtime().capture_snapshot()==uninterrupted.capture_snapshot(),"Fresh process deterministic continuation diverged");
        capture("project-continued");std::cout<<"Project fresh process: exact run/time/camera/report restoration and deterministic continuation verified.\n";
    }
    if (smoke_frames && !project_path)
    {
        submit(); submit(); diagnostic_failure_probe(); const auto overview = capture("overview");
        require(graphics.info().ui_vertices > 1000 && graphics.info().ui_draw_calls >= 3,"Dashboard emitted no meaningful UI draw data");
        // Readback must contain readable light text in the metrics panel, not just its background.
        std::size_t text_pixels = 0;
        for (std::uint32_t y = 70; y < 550; ++y) for (std::uint32_t x = 10; x < 190; ++x)
        { const auto i = (static_cast<std::size_t>(y)*overview.width+x)*4; if (overview.rgba[i] > 150 && overview.rgba[i+1] > 150 && overview.rgba[i+2] > 150) ++text_pixels; }
        require(text_pixels > 1000,"Dashboard text was not present in GPU readback");
        const auto starting_snapshot = controller.runtime().capture_snapshot();
        const auto starting_pose = controller.orbit_camera().camera().position;
        const auto expanded_width = controller.viewport().size.x;
        click(dashboard.metrics_toggle);
        require(controller.viewport().size.x > expanded_width,"Metrics collapse did not expand viewport");
        require(controller.orbit_camera().camera().position == starting_pose,"Panel collapse changed camera pose"); capture("collapsed");
        click(dashboard.metrics_toggle); require(controller.viewport().size.x == expanded_width,"Metrics expansion left stale viewport bounds");
        click(dashboard.focus_toggle); require(controller.viewport().size.x == ImGui::GetIO().DisplaySize.x,"Focus mode did not reclaim full width"); capture("focus");
        click(dashboard.focus_toggle); require(controller.runtime().capture_snapshot() == starting_snapshot,"Panel controls changed simulation state");
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
        design_captures("shipment");
        require(selected.rgba != before_orbit.rgba,"Selection/inspector did not change readback");
        const auto selected_key = controller.selection();
        const auto selected_object = std::find_if(controller.scene().selectables().begin(),controller.scene().selectables().end(),
            [&](const auto& object){return object.key == *selected_key;});
        const auto index = selected_object->render_index;
        // Large-layout overview cargo may be subpixel or occluded. Verify the same
        // real mesh/tint in an isolated close view, retaining identical UI/camera.
        auto probe = controller.present(); probe.objects = {probe.objects.at(index)}; probe.framing_bounds = selected_object->bounds;
        require(graphics.render_frame(probe),"Could not present selected tint probe"); const auto tinted = graphics.capture_frame();
        controller.select(std::nullopt); probe.objects.front() = controller.present().objects.at(index);
        require(graphics.render_frame(probe),"Could not present unselected tint probe");
        require(graphics.capture_frame().rgba != tinted.rgba,"Selected-object tint did not change isolated real-mesh GPU pixels");
        // All cube vertices are white: distinct neutral faces must come from
        // the normal-based shader, rather than baked material shades or UI.
        auto light_probe=probe; light_probe.objects={renderer::RenderObject{renderer::MeshId::Cube,{},glm::vec3{0.7f}}};
        light_probe.framing_bounds=renderer::SceneBounds{{-0.5f,-0.5f,-0.5f},{0.5f,0.5f,0.5f}};
        require(graphics.render_frame(light_probe),"Could not present lighting probe");
        const auto lit=graphics.capture_frame(); const auto area=controller.viewport().pixels(); std::array<std::size_t,256> shades{};
        for(std::uint32_t y=area.y;y<area.y+area.height;++y) for(std::uint32_t x=area.x;x<area.x+area.width;++x)
        { const auto i=(static_cast<std::size_t>(y)*lit.width+x)*4; const auto r=lit.rgba[i]; if(r>100 && r==lit.rgba[i+1] && r==lit.rgba[i+2]) ++shades[r]; }
        require(std::count_if(shades.begin(),shades.end(),[](auto count){return count>64;})>=3,"Directional lighting did not produce three visible neutral cube faces");
        controller.select(selected_key); submit();
        const auto inspector_pose = controller.orbit_camera().camera().position;
        click(dashboard.inspector_toggle); require(controller.selection() == selected_key && dashboard.layout.inspector.size.x == 0,"Inspector collapse lost selection or left a stale region");
        require(controller.orbit_camera().camera().position == inspector_pose,"Inspector collapse changed camera pose"); capture("inspector-collapsed");
        click(dashboard.inspector_toggle);
        const auto selected_info = controller.inspect();
        require(selected_info->shipment->id == controller.selection()->id,"Inspector fabricated shipment identity");
        require(!selected_info->shipment->completion_tick,"Inspector invented active completion timestamp");
        const auto frozen = controller.observation().tick; frame(std::chrono::seconds{1});
        require(controller.observation().tick == frozen,"Paused wall time advanced runtime"); capture("paused");
        controller.seek(std::min<std::uint64_t>(controller.observation().horizon,7200ULL*definition.simulation.tick_rate)); submit(); capture("queue-buildup");
        design_captures("simulation");
        controller.seek(0); submit(); require(controller.observation().tick == 0 && controller.observation().metrics.arrived == 1,"Backward seek zero did not replay arrival zero");
        click(dashboard.reset); require(controller.runtime().capture_snapshot() == warehouse::WarehouseSimulation{definition}.capture_snapshot(),"Reset did not restore pristine runtime");
        require(!controller.selection(),"Reset retained selection");
        click(dashboard.play); require(!controller.runtime().clock().paused(),"Resume button failed");
        controller.set_speed(900); frame(std::chrono::milliseconds{10}); require(controller.observation().tick > 0,"Resume/speed failed");
        for (const auto size : {platform::PixelSize{1024,768},platform::PixelSize{1600,900},platform::PixelSize{1920,1080},platform::PixelSize{800,520},platform::PixelSize{640,480},platform::PixelSize{1280,720}})
        {
            window.resize(size.width,size.height); submit(); require(controller.viewport().pixels().width > 0,"Resize invalidated viewport");
            const bool compact_capture = size.width == 640 && dashboard.layout.metrics.size.x > 0;
            if (compact_capture) click(dashboard.metrics_toggle);
            const auto suffix = "size-"+std::to_string(size.width); capture(suffix.c_str());
            if (compact_capture) click(dashboard.metrics_toggle);
        }
        window.minimize(); window.poll_events([&](const SDL_Event& e){graphics.process_ui_event(e);});
        require(!graphics.begin_ui_frame(),"Minimized UI frame was not skipped"); window.restore(); submit();
        controller.seek(controller.observation().horizon); submit(); capture("final");
        window.resize(1920,1080); submit(); controller.select(viewer::VisualKey{viewer::VisualCategory::Worker,1}); submit();
        const auto wide_inspector = controller.viewport().size.x; const auto wide_pose = controller.orbit_camera().camera().position;
        click(dashboard.inspector_toggle); require(controller.viewport().size.x > wide_inspector,"Wide inspector collapse did not expand viewport");
        require(controller.selection().has_value() && controller.orbit_camera().camera().position == wide_pose,"Wide inspector toggle corrupted selection/camera");
        controller.select(std::nullopt); window.resize(1280,720); submit();
        const auto native_scale = ui_state.scale;
        for (float scale : {1.0f,1.25f,1.5f,2.0f})
        {
            diagnostic_scale = scale; submit(); submit();
            const auto suffix = "dpi-"+std::to_string(static_cast<int>(scale*100)); capture(suffix.c_str());
            require(dashboard.layout.control_height >= viewer::design::control_height*scale,"UI scale reduced control hit targets");
        }
        diagnostic_scale = native_scale; submit(); diagnostic_scale.reset();
        warehouse::WarehouseSimulation reference{definition}; reference.run();
        require(controller.runtime().capture_snapshot() == reference.capture_snapshot(),"Seek horizon disagrees with headless runtime");
        require(graphics.info().pipeline_generations == 1 && graphics.info().ui_pipeline_generations == 1,"Ordinary interaction recreated a graphics pipeline");
        const auto preserved = controller.runtime().capture_snapshot();
        click(dashboard.workspace_toggle); submit();
        require(ui_state.panels.workspace && workspace.draft().values[1] == std::to_string(std::get<scenario::WarehouseConfig>(definition.domain).loading_bays),"Workspace editor initial configuration mismatch");
        capture("workspace-editor");
        design_captures("editor");
        const auto enter_text = [&](glm::vec2 target, const char* value)
        {
            click(target);
            const auto key = [&](SDL_Keycode code, SDL_Scancode scan, bool down, SDL_Keymod mods)
            { SDL_Event e{}; e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP; e.key.windowID = id; e.key.key = code; e.key.scancode = scan; e.key.down = down; e.key.mod = mods; push(e); submit(); };
            key(SDLK_LCTRL,SDL_SCANCODE_LCTRL,true,SDL_KMOD_LCTRL); key(SDLK_A,SDL_SCANCODE_A,true,SDL_KMOD_LCTRL);
            key(SDLK_A,SDL_SCANCODE_A,false,SDL_KMOD_LCTRL); key(SDLK_LCTRL,SDL_SCANCODE_LCTRL,false,SDL_KMOD_NONE);
            SDL_Event text{}; text.type = SDL_EVENT_TEXT_INPUT; text.text.windowID = id; text.text.text = value; push(text); submit();
        };
        workspace_ui.scroll_target = 3; submit(); submit(); enter_text(workspace_ui.forklift_field,"-1");
        workspace_ui.scroll_target = 10; submit(); submit(); click(workspace_ui.validate);
        require(!workspace.errors().empty() && workspace.baseline_viewer().runtime().capture_snapshot() == preserved,"Invalid draft changed baseline or lacked validation errors");
        workspace_ui.scroll_target = 3; submit(); submit(); capture("workspace-invalid");
        enter_text(workspace_ui.forklift_field,std::to_string(std::get<scenario::WarehouseConfig>(definition.domain).forklifts+1).c_str());
        workspace_ui.scroll_target = 0; submit(); submit(); enter_text(workspace_ui.name_field,"Add forklift");
        workspace_ui.scroll_target = 10; submit(); submit(); click(workspace_ui.add);
        require(workspace.candidates().size() == 1,"Injected candidate creation failed");
        workspace_ui.requested_tab = 1; workspace_ui.scroll_target = 11; submit(); submit(); submit();
        const auto wait_experiment = [&] {
            const auto deadline=Clock::now()+std::chrono::seconds{10};
            while (workspace.execution().progress().active()) { require(Clock::now()<deadline,"Async graphical experiment timed out"); submit(); }
            submit(); require(workspace.execution().progress().state==workspace::ExecutionState::Completed,"Async graphical experiment failed");
        };
        capture("workspace-candidates"); click(workspace_ui.compare); submit(); wait_experiment();
        if (!workspace.report()) throw std::runtime_error("Workspace comparison did not produce a report at "+std::to_string(workspace_ui.compare.x)+","+std::to_string(workspace_ui.compare.y)+": "+workspace_ui.error);
        workspace_ui.requested_tab = 2; workspace_ui.scroll_target = 20; submit(); submit(); capture("workspace-results");
        {
            const auto previous_report=workspace.json();
            ExperimentGate gate;
            workspace.execution().start(workspace.capture_request(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted),
                [&](auto phase,auto,auto,auto){gate.checkpoint(phase);});
            gate.wait(); submit();
            require(workspace.execution().progress().active() && workspace.execution().progress().phase=="Evaluating candidate","Active experiment progress missing");
            capture("workspace-running");
            click(dashboard.simulation_mode); submit();
            const auto camera_before=workspace.view().orbit_camera().camera().view_projection();
            const auto target=workspace.view().viewport().origin+workspace.view().viewport().size*0.5f;
            button(id,target,true); submit(); motion(id,target+glm::vec2{40,15}); submit(); button(id,target+glm::vec2{40,15},false); submit();
            require(workspace.view().orbit_camera().camera().view_projection()!=camera_before,"Camera froze during background work");
            click(dashboard.workspace_toggle); submit(); require(ui_state.panels.mode==viewer::ApplicationMode::Editor,"Editor mode unavailable during work");
            click(dashboard.analysis_mode); submit(); require(ui_state.panels.mode==viewer::ApplicationMode::Analysis,"Analysis mode unavailable during work");
            require(workspace.json()==previous_report,"Running experiment replaced previous report");
            click(workspace_ui.cancel_experiment);
            require(workspace.execution().progress().cancellation_requested,"Graphical cancel control did not request stop");
            gate.release();
            const auto deadline=Clock::now()+std::chrono::seconds{10};
            while(workspace.execution().progress().active()) {require(Clock::now()<deadline,"Graphical cancellation timed out"); submit();}
            submit(); require(workspace.execution().progress().state==workspace::ExecutionState::Cancelled && workspace.json()==previous_report,"Cancellation lost the previous report");
            capture("workspace-cancelled");
            require(workspace.baseline_viewer().runtime().capture_snapshot()==preserved,"Async experiment mutated viewer state");
            workspace.start_experiment(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted); wait_experiment();
            auto substantial=workspace.capture_request(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted);
            substantial.baseline.simulation={60,100000,42}; substantial.baseline.domain=scenario::WarehouseConfig{4,8,2,1,4};
            const auto prototype=substantial.candidates.front(); substantial.candidates.clear();
            for (std::uint64_t i=1;i<=50;++i) {
                auto c=prototype; c.id=i; c.definition=substantial.baseline; c.definition.name="Load candidate "+std::to_string(i);
                substantial.candidates.push_back(std::move(c));
            }
            ExperimentGate publication_gate; publication_gate.target_phase="Publishing";
            workspace.execution().start(std::move(substantial),[&](auto phase,auto,auto,auto){publication_gate.checkpoint(phase);});
            click(dashboard.simulation_mode); submit();
            double active_frame_ms=0,maximum_ms=0; int active_frames=0;
            for(int i=0;i<8;++i) {
                require(workspace.execution().progress().active(),"Substantial experiment finished before frame responsiveness checks");
                const auto start=Clock::now(); submit(); const auto elapsed=std::chrono::duration<double,std::milli>{Clock::now()-start}.count();
                active_frame_ms+=elapsed; maximum_ms=std::max(maximum_ms,elapsed); ++active_frames;
            }
            click(dashboard.analysis_mode); submit(); click(workspace_ui.cancel_experiment);
            require(workspace.execution().progress().cancellation_requested,"Substantial cancel control did not request stop");
            publication_gate.release();
            const auto cancel_started=Clock::now();
            const auto cancel_deadline=Clock::now()+std::chrono::seconds{10};
            while(workspace.execution().progress().active()) {require(Clock::now()<cancel_deadline,"Substantial cancellation timed out"); submit();}
            submit(); require(workspace.execution().progress().state==workspace::ExecutionState::Cancelled,"Substantial run was not cancelled");
            std::cout<<"Actual 100K-arrival/50-candidate experiment active frames: "<<active_frames<<" | mean/max ms: "<<active_frame_ms/active_frames<<'/'<<maximum_ms
                <<" | cancel observed ms (includes rendering): "<<std::chrono::duration<double,std::milli>{Clock::now()-cancel_started}.count()<<'\n';
            std::cout<<"Async graphics: progress, camera, mode switching, cancel/report retention and retry verified.\n";
        }
        design_captures("analysis");
        workspace_ui.result_name = "Add forklift"; workspace_ui.scroll_target = 12; submit(); submit(); click(workspace_ui.candidate_view); submit();
        require(workspace.view().result_preview() && workspace.view().observation().metrics == workspace.report()->candidates.front().metrics,"Candidate preview did not display its actual report state");
        require(!workspace.view().selection(),"Candidate view retained stale selection");
        workspace_ui.scroll_target = 14; submit(); submit(); capture("workspace-candidate");
        design_captures("candidate");
        const auto selected_camera = workspace.view().orbit_camera().camera().position;
        auto p2 = workspace.view().viewport().origin+workspace.view().viewport().size*0.5f;
        button(id,p2,true); submit(); motion(id,p2+glm::vec2{30,10}); submit(); button(id,p2+glm::vec2{30,10},false); submit();
        require(workspace.view().orbit_camera().camera().position != selected_camera,"Candidate camera controls failed");
        bool preview_picked = false;
        for (const auto& o : workspace.view().scene().selectables())
        {
            auto clip = workspace.view().orbit_camera().camera().view_projection()*glm::vec4{(o.bounds.minimum+o.bounds.maximum)*0.5f,1};
            if (clip.w <= 0) continue; clip /= clip.w;
            const auto v = workspace.view().viewport(); const auto pixels = v.pixels();
            const glm::vec2 point{(pixels.x+(clip.x+1)*pixels.width*0.5f)/v.framebuffer_scale.x,(pixels.y+(clip.y+1)*pixels.height*0.5f)/v.framebuffer_scale.y};
            if (!v.contains(point)) continue; click(point);
            if (workspace.view().inspect()) { preview_picked = true; break; }
        }
        require(preview_picked,"Candidate result picking/inspection failed");
        click(dashboard.simulation_mode); submit(); capture("workspace-inspector"); click(dashboard.analysis_mode); submit();
        click(dashboard.simulation_mode); submit(); click(dashboard.context_action); submit();
        require(workspace.view().runtime().capture_snapshot() == preserved,"Comparison/view switching advanced baseline");
        workspace_ui.mode=1; workspace_ui.current_checkpoint=false;
        workspace_ui.checkpoint_seconds=std::to_string(std::min<std::uint64_t>(definition.simulation.duration_seconds,7200));
        workspace_ui.requested_tab=0; workspace_ui.scroll_target=16; submit(); submit(); submit();
        click(workspace_ui.editor_compare); submit(); wait_experiment();
        require(ui_state.panels.mode==viewer::ApplicationMode::Analysis && workspace.report()->mode==workspace::ComparisonMode::Checkpoint,"Editor compare action ignored the selected comparison mode");
        workspace_ui.scroll_target = 20; submit(); submit(); capture("workspace-checkpoint");
        workspace_ui.scroll_target = 12; submit(); submit(); click(workspace_ui.probe_toggle);
        workspace_ui.scroll_target = 13; submit(); submit(); capture("workspace-probes");
        require(workspace.report()->executions.front().request.checkpoint.current_tick == std::min<std::uint64_t>(definition.simulation.duration_seconds,7200)*definition.simulation.tick_rate,"Checkpoint workflow used the wrong time");
        window.resize(640,480); submit(); capture("workspace-compact");
        diagnostic_scale = 2.0f; submit(); submit(); require(dashboard.layout.workspace_overlay,"Small high-DPI editor did not use its expanded page"); capture("workspace-dpi200");
        diagnostic_scale.reset(); window.resize(1280,720); submit();
        if(capture_output)
        {
            window.resize(1280,720); submit();
            const auto json = capture_output->parent_path()/(capture_output->stem().string()+"-comparison.json");
            const auto csv = capture_output->parent_path()/(capture_output->stem().string()+"-comparison.csv");
            workspace_ui.scroll_target=15; submit(); submit();
            enter_text(workspace_ui.export_field,json.string().c_str()); click(workspace_ui.export_json);
            require(std::filesystem::exists(json) && workspace_ui.error.empty(),"Graphical JSON export failed");
            click(workspace_ui.export_json); require(!workspace_ui.error.empty() && workspace.report().has_value(),"Existing-file export did not preserve the report and show failure");
            workspace_ui.scroll_target=15; submit(); submit();
            enter_text(workspace_ui.export_field,csv.string().c_str()); click(workspace_ui.export_csv);
            require(std::filesystem::exists(csv) && workspace_ui.error.empty(),"Graphical CSV export failed");
            workspace::ScenarioWorkspace::verify_export(workspace.json());
        }
        click(dashboard.simulation_mode); submit();
        require(workspace.baseline_viewer().runtime().capture_snapshot() == preserved && graphics.info().pipeline_generations == 1 && graphics.info().ui_pipeline_generations == 1,"Workspace modified baseline or rebuilt pipelines");
        const auto custom_buffers_before=graphics.info().buffer_allocations;
        // Commit 20: use real ImGui/SDL launch and controls on an independent custom runtime.
        workspace.revert(true); workspace.edit(workspace::Field::Name,"Custom three-forklift warehouse"); workspace.edit(workspace::Field::Forklifts,"3");
        click(dashboard.workspace_toggle); workspace_ui.requested_tab=0; workspace_ui.scroll_target=17; submit(); submit();
        click(workspace_ui.run_simulation); submit();
        require(workspace.active_run_id()!=0 && ui_state.panels.mode==viewer::ApplicationMode::Simulation && !ui_state.panels.workspace,"Run Simulation did not activate Simulation mode");
        auto& custom=workspace.view(); const auto custom_id=workspace.active_run_id();
        require(custom.observation().tick==0 && custom.runtime().clock().paused() && custom.observation().capacity.forklifts==3,"Custom start policy/configuration incorrect");
        capture("custom-initial"); click(dashboard.play); frame(std::chrono::seconds{2}); click(dashboard.play);
        require(custom.observation().tick>0 && custom.observation().metrics.arrived>0 && custom.runtime().clock().paused(),"Custom playback failed to evolve");
        const auto custom_tick=custom.observation().tick; click(dashboard.step); require(custom.observation().tick==custom_tick+1,"Custom single step failed");
        enter_text(dashboard.speed,"2");
        SDL_Event enter{}; enter.type=SDL_EVENT_KEY_DOWN; enter.key.windowID=id; enter.key.key=SDLK_RETURN; enter.key.scancode=SDL_SCANCODE_RETURN; enter.key.down=true; push(enter); submit();
        enter.type=SDL_EVENT_KEY_UP; enter.key.down=false; push(enter); submit(); require(custom.speed()==2,"Custom speed UI did not address the active run");
        const auto custom_definition=scenario::parse_scenario(custom.runtime().capture_snapshot().scenario_identity);
        const auto seek_custom_slider=[&](float fraction) {
            const auto a=dashboard.timeline_min,b=dashboard.timeline_max; click({a.x+(b.x-a.x)*fraction,(a.y+b.y)*0.5f});
            warehouse::WarehouseSimulation expected{custom_definition}; expected.advance_to(custom.observation().tick);
            require(custom.runtime().capture_snapshot()==expected.capture_snapshot(),"Custom timeline UI did not reconstruct actual state");
        };
        seek_custom_slider(0.20f); const auto custom_forward=custom.observation().tick; seek_custom_slider(0.02f);
        require(custom.observation().tick<custom_forward,"Custom backward timeline UI failed");
        custom.seek(std::min<std::uint64_t>(custom.observation().horizon,600ULL*custom.observation().tick_rate)); submit();
        require(!custom.scene().shipments().empty(),"Custom operational cargo missing");
        bool custom_picked=false;
        for(const auto& o:custom.scene().selectables()) {
            if(o.key.category!=viewer::VisualCategory::Shipment) continue;
            auto clip=custom.orbit_camera().camera().view_projection()*glm::vec4{(o.bounds.minimum+o.bounds.maximum)*0.5f,1};
            if(clip.w<=0) continue; clip/=clip.w;
            const auto v=custom.viewport(); const auto pixels=v.pixels();
            const glm::vec2 point{(pixels.x+(clip.x+1)*pixels.width*0.5f)/v.framebuffer_scale.x,(pixels.y+(clip.y+1)*pixels.height*0.5f)/v.framebuffer_scale.y};
            if(!v.contains(point)) continue; click(point);
            if(custom.inspect() && custom.inspect()->shipment) {custom_picked=true; break;}
        }
        require(custom_picked,"Custom shipment picking failed"); capture("custom-processing");
        const auto saved_custom=custom.runtime().capture_snapshot();
        const auto choose_run=[&](std::uint64_t run) {
            click(dashboard.run_selector); submit();
            const auto found=std::find_if(dashboard.run_choices.begin(),dashboard.run_choices.end(),[&](const auto& choice){return choice.first==run;});
            require(found!=dashboard.run_choices.end(),"Run selector did not expose retained run");
            const auto point=found->second; click(point); submit();
        };
        choose_run(0);
        require(workspace.view().runtime().capture_snapshot()==preserved && !workspace.view().selection(),"Custom launch modified baseline");
        choose_run(custom_id);
        require(custom.runtime().capture_snapshot()==saved_custom && custom.speed()==2 && !custom.selection(),"Custom switching lost state");
        {
            ExperimentGate gate; workspace.execution().start(workspace.capture_request(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted),
                [&](auto phase,auto,auto,auto){gate.checkpoint(phase);}); gate.wait(); submit();
            const auto before=custom.orbit_camera().camera().position;
            const auto p=custom.viewport().origin+custom.viewport().size*0.5f;
            button(id,p,true); submit(); motion(id,p+glm::vec2{30,10}); submit(); button(id,p+glm::vec2{30,10},false); submit();
            require(custom.orbit_camera().camera().position!=before,"Custom viewport froze during experiment");
            gate.release(); wait_experiment(); require(custom.runtime().capture_snapshot()==saved_custom,"Background work mutated custom run");
        }
        click(dashboard.analysis_mode); workspace_ui.result_name="Add forklift"; workspace_ui.scroll_target=18; submit(); submit();
        click(workspace_ui.simulate_result); submit();
        require(workspace.active_run_id()!=custom_id && workspace.view().observation().tick==0 && !workspace.view().result_preview(),"Simulate This Candidate failed");
        workspace.view().seek(workspace.view().observation().horizon); submit();
        require(workspace.view().observation().metrics==workspace.report()->candidates.front().metrics,"Playable candidate differs from report"); capture("custom-candidate-final");
        workspace.view_result("Add forklift"); submit(); require(workspace.view().result_preview(),"Custom candidate preview missing");
        workspace_ui.pending.push_back({WorkspaceAction::SelectRun,{},{},custom_id}); submit();
        require(custom.runtime().capture_snapshot()==saved_custom,"Preview destroyed custom run");
        click(dashboard.reset); require(custom.observation().tick==0,"Custom restart failed");
        const auto file=std::filesystem::temp_directory_path()/("simforge-playable-"+std::to_string(Clock::now().time_since_epoch().count())+".json");
        struct FileCleanup { std::filesystem::path path; ~FileCleanup(){std::error_code e; std::filesystem::remove(path,e);} } cleanup{file};
        workspace.save_draft(file); workspace_ui.pending.push_back({WorkspaceAction::LoadRun,file.string()}); submit();
        auto loaded=scenario::load_scenario(file); warehouse::WarehouseSimulation headless_custom{loaded}; headless_custom.run();
        workspace.view().seek(workspace.view().observation().horizon); submit();
        require(workspace.view().runtime().capture_snapshot()==headless_custom.capture_snapshot(),"Saved-file custom playback differs from headless"); capture("custom-file-final");
        std::cout<<"Custom GPU buffers before/after: "<<custom_buffers_before<<'/'<<graphics.info().buffer_allocations<<" | 3D/UI draws: "<<graphics.info().draw_calls<<'/'<<graphics.info().ui_draw_calls<<'\n';
        window.resize(1024,768); submit(); SDL_MinimizeWindow(window.native_handle()); frame(std::chrono::nanoseconds{0}); SDL_RestoreWindow(window.native_handle()); submit();
        workspace.return_to_baseline(); submit();
        require(workspace.view().runtime().capture_snapshot()==preserved && graphics.info().pipeline_generations==1 && graphics.info().ui_pipeline_generations==1,"Custom playback rebuilt pipelines or changed baseline");
        if(const auto* project_test=std::getenv("SIMFORGE_PROJECT_TEST_PATH")) {
            project_ui.test_path=std::filesystem::u8path(project_test);project.rename("Warehouse study");
            // Save an existing independent run at an intermediate time through the File menu.
            workspace.select_run(custom_id);workspace.view().seek(600ULL*workspace.view().observation().tick_rate);submit();
            const auto saved=workspace.view().runtime().capture_snapshot();const auto saved_camera=workspace.view().orbit_camera().pose();
            const auto saved_report=workspace.json();
            click(dashboard.file_menu);submit();click(project_ui.save_button);submit();
            require(std::filesystem::exists(project_ui.test_path) && project_ui.error.empty() && !project.dirty(),"UI project save failed");
            capture("project-saved");click(dashboard.file_menu);submit();capture("project-menu");
            // Dismiss the File popup with Escape before continuing.
            SDL_Event dismiss{};dismiss.type=SDL_EVENT_KEY_DOWN;dismiss.key.windowID=id;dismiss.key.key=SDLK_ESCAPE;dismiss.key.scancode=SDL_SCANCODE_ESCAPE;dismiss.key.down=true;push(dismiss);submit();
            dismiss.type=SDL_EVENT_KEY_UP;dismiss.key.down=false;push(dismiss);submit();
            const auto original_path=project_ui.test_path;
            workspace.edit(workspace::Field::Workers,"7");
            click(dashboard.file_menu);submit();click(project_ui.new_button);submit();
            require(project_ui.confirmation.has_value(),"New did not prompt for unsaved changes");
            click(project_ui.cancel);submit();require(workspace.view().runtime().capture_snapshot()==saved && workspace.draft().values[2]=="7","Cancel lost unsaved workspace");
            click(dashboard.file_menu);submit();click(project_ui.new_button);submit();click(project_ui.discard);submit();
            require(workspace.runs().empty() && !workspace.report(),"New project did not clear retained work");
            click(dashboard.file_menu);submit();click(project_ui.open_button);submit();
            project_ui.test_path=original_path.parent_path()/"missing"/"cannot-save.simforge";
            click(project_ui.confirm_save);submit();require(project_ui.confirmation.has_value() && !project_ui.error.empty() && workspace.runs().empty(),"Failed prompt save proceeded with Open");
            project_ui.test_path=original_path;click(project_ui.discard);submit();
            require(project_ui.error.empty() && workspace.active_run_id()==custom_id && workspace.view().runtime().capture_snapshot()==saved,"UI project open did not restore run");
            require(workspace.json()==saved_report && workspace.view().orbit_camera().pose()==saved_camera,"UI project open lost report/camera");
            capture("project-restored");
            const auto malformed=original_path.parent_path()/"invalid.simforge";
            {std::ofstream out(malformed);out<<"malformed project";}
            project_ui.test_path=malformed;workspace.edit(workspace::Field::Workers,"8");
            click(dashboard.file_menu);submit();click(project_ui.open_button);submit();click(project_ui.discard);submit();
            require(!project_ui.error.empty() && workspace.view().runtime().capture_snapshot()==saved && workspace.draft().values[2]=="8","Malformed UI open damaged current work");
            project_ui.test_path=original_path;project_ui.error.clear();
            std::cout<<"Project UI: save/open/new, unsaved Save/Discard/Cancel controls and malformed-file isolation verified.\n";
        }
        std::cout<<"Custom graphics: draft/candidate/file launches, real shipments/metrics, picking, playback/seek/reset, retained switching, async isolation and resize verified.\n";
        std::cout << "Workspace smoke: injected draft edit/invalid feedback/candidate creation/comparison, actual candidate result camera, preserved baseline, checkpoint and export replay verified.\n";
        std::cout << "Viewer smoke: dashboard GPU text, injected SDL camera/picking/buttons, real inspector, pause/step/reset/seek, sizes, minimize/restore, exact headless snapshot verified.\n";
    }
    auto previous = Clock::now(); std::uint64_t count = 0;
    const auto warmed_texture_creations = graphics.info().ui_texture_creations;
    while (window.is_open() && (!smoke_frames || count < *smoke_frames))
    {
        const auto now = Clock::now(); const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now-previous); previous = now;
        if (frame(elapsed)) ++count; else std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if(smoke_frames) require(count==*smoke_frames,"Graphics walkthrough cancelled before all requested frames completed");
    if (smoke_frames && capture_output) write_capture(graphics.capture_frame(),*capture_output);
    if (smoke_frames) require(graphics.info().ui_texture_creations == warmed_texture_creations,"Steady frames repeatedly recreated the font atlas");
    if(!smoke_frames && !project_ui.recovery_deferred && !project_ui.recovery_path.empty()) {
        try {if(project.dirty() && !project_ui.close_requested) (void)project.recover_if_due(project_ui.recovery_path,Clock::now()+std::chrono::seconds(61));
            else project.discard_own_recovery(project_ui.recovery_path);}
        catch(const std::exception& ex) {std::cerr<<"Recovery save failed: "<<ex.what()<<'\n';}
    }
    workspace.execution().shutdown();
    graphics.shutdown(); const auto info = graphics.info();
    std::cout << "Viewer frames: " << info.presented_frames << " | 3D draws: " << info.draw_calls << " | UI draws: " << info.ui_draw_calls << " | UI vertices: " << info.ui_vertices
              << " | Scene/UI pipeline generations: " << info.pipeline_generations << '/' << info.ui_pipeline_generations
              << " | Validation errors/warnings: " << info.validation_errors << '/' << info.validation_warnings << '\n';
    std::cout << "UI scale: " << ui_state.scale << " | Font texture creations/updates: " << info.ui_texture_creations << '/' << info.ui_texture_updates
              << " | Layout generations: " << workspace.view().scene().layout_generations() << " | Static/dynamic objects: "
              << workspace.view().scene().frame().objects.size()-workspace.view().scene().resources().size()-workspace.view().scene().shipments().size() << '/'
              << workspace.view().scene().resources().size()+workspace.view().scene().shipments().size() << '\n';
    if (measurements.frames) std::cout << "CPU means (includes present synchronization): UI build " << measurements.ui_us/measurements.frames
        << " us | dynamic " << measurements.dynamic_us/measurements.frames << " us | input " << measurements.input_us/measurements.frames
        << " us | record/submit/present " << measurements.present_ms/measurements.frames << " ms\n";
    if (workspace_ui.results_frames) std::cout << "Workspace result UI mean: " << workspace_ui.results_ui_us/workspace_ui.results_frames << " us\n";
    if (measurements.frames) std::cout << "CPU total frame mean: " << measurements.total_ms/measurements.frames << " ms (poll, commands, runtime, UI, GPU fence waits and present; no GPU timer)\n";
    require(!info.validation_errors && !info.validation_warnings,"Viewer Vulkan validation was not clean");
    if (smoke_frames && window.is_open())
    {
        workspace.view().reset(); graphics.initialize(window,options); style_dashboard(); submit();
        ExperimentGate closing_gate; workspace::ExperimentExecution closing_worker;
        closing_worker.start(workspace.capture_request(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted),
            [&](auto phase,auto,auto,auto){closing_gate.checkpoint(phase);});
        closing_gate.wait(); require(closing_worker.progress().active(),"Shutdown experiment was not active");
        window.request_close(); window.poll_events(); require(!window.is_open(),"Viewer close event failed");
        closing_worker.cancel(); closing_gate.release(); closing_worker.shutdown();
        require(closing_worker.progress().state==workspace::ExecutionState::Cancelled,"Active worker did not stop on native shutdown");
        graphics.shutdown();
        require(!graphics.info().validation_errors && !graphics.info().validation_warnings,"UI reinitialization validation failed");
        window.request_close(); window.poll_events(); require(!window.is_open(),"Viewer close event failed");
        std::cout << "Viewer clean UI/GPU shutdown and reinitialization verified.\n";
    }
    window.destroy(); return 0;
}
