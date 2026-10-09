#include <simforge/viewer/viewer.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace simforge::viewer
{
namespace
{
void valid(glm::vec2 v)
{ if (!std::isfinite(v.x) || !std::isfinite(v.y)) throw std::invalid_argument("Non-finite pointer/camera input"); }
void positive(float value)
{ if (!std::isfinite(value) || value <= 0) throw std::invalid_argument("Expected a finite positive camera extent"); }
glm::vec3 outward(float yaw, float pitch)
{ return {std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)}; }
}
void Viewport::validate() const
{
    valid(origin); valid(size); valid(framebuffer_scale);
    if (origin.x < 0 || origin.y < 0 || size.x <= 0 || size.y <= 0 || framebuffer_scale.x <= 0 || framebuffer_scale.y <= 0)
        throw std::invalid_argument("Invalid logical viewport");
    const auto end = (origin+size)*framebuffer_scale;
    if (!std::isfinite(end.x) || !std::isfinite(end.y) || end.x > 1000000 || end.y > 1000000)
        throw std::invalid_argument("Viewport exceeds supported surface dimensions");
}
bool Viewport::contains(glm::vec2 p) const noexcept
{ return p.x >= origin.x && p.y >= origin.y && p.x < origin.x+size.x && p.y < origin.y+size.y; }
renderer::RenderViewport Viewport::pixels() const
{
    validate();
    const auto first = glm::round(origin*framebuffer_scale), last = glm::round((origin+size)*framebuffer_scale);
    if (last.x <= first.x || last.y <= first.y) throw std::invalid_argument("Viewport is smaller than a drawable pixel");
    return {static_cast<std::uint32_t>(first.x),static_cast<std::uint32_t>(first.y),
        static_cast<std::uint32_t>(last.x-first.x),static_cast<std::uint32_t>(last.y-first.y)};
}
OrbitCamera::OrbitCamera(renderer::SceneBounds bounds, float aspect) : initial_{bounds}, bounds_{bounds}, aspect_{aspect}
{ fit(bounds); }
void OrbitCamera::fit(renderer::SceneBounds bounds)
{
    const auto c = renderer::frame_bounds(bounds,aspect_,1.04f);
    const double radius = glm::length(glm::dvec3{bounds.maximum}-glm::dvec3{bounds.minimum})*0.5;
    const auto offset = glm::dvec3{c.position}-glm::dvec3{c.target}; const double distance = glm::length(offset);
    if (radius <= 0 || radius*20 > std::numeric_limits<float>::max() || !std::isfinite(distance) || distance <= 0)
        throw std::invalid_argument("Scene exceeds supported orbit camera range");
    bounds_ = bounds; target_ = c.target; radius_ = static_cast<float>(radius); distance_ = static_cast<float>(distance);
    yaw_ = static_cast<float>(std::atan2(offset.x,offset.z)); pitch_ = static_cast<float>(std::asin(offset.y/distance)); adjusted_ = false;
}
void OrbitCamera::frame(renderer::SceneBounds bounds) { fit(bounds); }
void OrbitCamera::reset() { fit(initial_); }
void OrbitCamera::restore_pose(const CameraPose& pose)
{
    if (!std::isfinite(pose.yaw) || !std::isfinite(pose.pitch) || !std::isfinite(pose.distance) ||
        !renderer::finite(pose.target) || std::abs(pose.yaw)>7 || pose.pitch < -1.57f || pose.pitch > 1.57f ||
        pose.distance <= 0 || pose.distance > radius_*20+6)
        throw std::invalid_argument("Invalid saved camera pose");
    const auto center=(bounds_.minimum+bounds_.maximum)*0.5f;
    if (glm::any(glm::greaterThan(glm::abs(pose.target-center),glm::vec3{radius_*3+1})))
        throw std::invalid_argument("Saved camera target exceeds scene bounds");
    auto replacement=*this;
    replacement.yaw_=pose.yaw; replacement.pitch_=pose.pitch; replacement.distance_=pose.distance;
    replacement.target_=pose.target; replacement.adjusted_=true; (void)replacement.camera(); *this=replacement;
}
void OrbitCamera::set_aspect(float aspect, bool preserve_pose)
{
    positive(aspect); if (aspect == aspect_) return;
    auto replacement = *this; replacement.aspect_ = aspect;
    if (!adjusted_ && !preserve_pose) replacement.fit(bounds_); else (void)replacement.camera().view_projection();
    *this = replacement;
}
void OrbitCamera::orbit(glm::vec2 delta)
{
    valid(delta);
    yaw_ = std::remainder(yaw_+std::clamp(delta.x,-10000.0f,10000.0f)*0.006f,6.283185307f);
    pitch_ = std::clamp(pitch_+std::clamp(delta.y,-10000.0f,10000.0f)*0.006f,0.17f,1.35f); adjusted_ = true;
}
void OrbitCamera::pan(glm::vec2 delta, float height)
{
    valid(delta); positive(height);
    const auto direction = outward(yaw_,pitch_);
    const auto right = glm::normalize(glm::cross(glm::vec3{0,1,0},direction));
    const auto up = glm::cross(direction,right);
    delta = glm::clamp(delta,glm::vec2{-10000},glm::vec2{10000});
    const auto change = (-right*delta.x+up*delta.y)*(distance_*0.82842712f/std::max(height,1.0f));
    const auto center = (bounds_.minimum+bounds_.maximum)*0.5f;
    target_ = glm::clamp(target_+change,center-glm::vec3{radius_*3},center+glm::vec3{radius_*3}); adjusted_ = true;
}
void OrbitCamera::zoom(float wheel)
{
    if (!std::isfinite(wheel)) throw std::invalid_argument("Non-finite zoom input");
    distance_ = std::clamp(distance_*std::exp(-std::clamp(wheel,-30.0f,30.0f)*0.1f),std::max(3.0f,radius_*0.12f),std::max(6.0f,radius_*20));
    adjusted_ = true;
}
renderer::Camera OrbitCamera::camera() const
{
    renderer::Camera c; c.target = target_; c.position = target_+outward(yaw_,pitch_)*distance_;
    // Pan cannot place the eye beneath the floor.
    if (c.position.y < 0.3f) { c.target.y += 0.3f-c.position.y; c.position.y = 0.3f; }
    c.aspect_ratio = aspect_; c.far_plane = distance_+radius_*8+20; c.validate(); return c;
}
std::optional<Ray> pointer_ray(const renderer::Camera& camera, const Viewport& viewport, glm::vec2 pointer)
{
    viewport.validate(); valid(pointer); if (!viewport.contains(pointer)) return std::nullopt;
    const auto pixel = viewport.pixels();
    const auto relative = (pointer*viewport.framebuffer_scale-glm::vec2{pixel.x,pixel.y})/glm::vec2{pixel.width,pixel.height};
    const auto ndc = relative*2.0f-1.0f; // Projection already flips Y for Vulkan.
    const auto inverse = glm::inverse(camera.view_projection());
    auto near = inverse*glm::vec4{ndc.x,ndc.y,0,1}; auto far = inverse*glm::vec4{ndc.x,ndc.y,1,1};
    if (!std::isfinite(near.w) || !std::isfinite(far.w) || std::abs(near.w) < 1e-8f || std::abs(far.w) < 1e-8f)
        throw std::invalid_argument("Singular picking projection");
    near /= near.w; far /= far.w;
    const glm::vec3 start{near}, offset{far-near}; const float length = glm::length(offset);
    if (!renderer::finite(start) || !renderer::finite(offset) || !std::isfinite(length) || length <= 0)
        throw std::invalid_argument("Invalid picking ray");
    return Ray{start,offset/length,length};
}
std::optional<double> intersect(const Ray& ray, const renderer::SceneBounds& bounds)
{
    if (!renderer::finite(ray.origin) || !renderer::finite(ray.direction) || !std::isfinite(ray.length) || ray.length <= 0 ||
        glm::length(ray.direction) < 0.999f || glm::length(ray.direction) > 1.001f ||
        !renderer::finite(bounds.minimum) || !renderer::finite(bounds.maximum) ||
        glm::any(glm::greaterThan(bounds.minimum,bounds.maximum))) throw std::invalid_argument("Invalid ray or picking bounds");
    double low = 0, high = ray.length;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::abs(ray.direction[axis]) < 1e-10)
        { if (ray.origin[axis] < bounds.minimum[axis] || ray.origin[axis] > bounds.maximum[axis]) return std::nullopt; }
        else
        {
            double a = (static_cast<double>(bounds.minimum[axis])-ray.origin[axis])/ray.direction[axis];
            double b = (static_cast<double>(bounds.maximum[axis])-ray.origin[axis])/ray.direction[axis];
            if (a > b) std::swap(a,b); low = std::max(low,a); high = std::min(high,b);
            if (high < low) return std::nullopt;
        }
    }
    return low;
}
std::optional<VisualKey> pick(const Ray& ray, std::span<const visualization::Selectable> objects)
{
    std::optional<VisualKey> result; double closest = std::numeric_limits<double>::infinity();
    for (const auto& o : objects) if (const auto d = intersect(ray,o.bounds))
    {
        if (*d < closest || (*d == closest && (!result || o.key < *result))) { closest = *d; result = o.key; }
    }
    return result;
}
std::string format_time(std::uint64_t tick, std::uint32_t rate)
{
    if (!rate) throw std::invalid_argument("Time display requires a positive tick rate");
    const auto seconds = tick/rate; std::ostringstream out;
    out << std::setfill('0') << std::setw(2) << seconds/3600 << ':' << std::setw(2) << seconds/60%60 << ':' << std::setw(2) << seconds%60;
    return out.str();
}
std::string format_number(std::optional<double> value, double scale, const char* suffix)
{
    if (!value) return "N/A";
    if (!std::isfinite(*value) || !std::isfinite(scale) || !std::isfinite(*value*scale)) throw std::invalid_argument("Non-finite metric display");
    std::ostringstream out; out << std::fixed << std::setprecision(2) << *value*scale << suffix; return out.str();
}
ViewerController::ViewerController(const scenario::ScenarioDefinition& definition, double speed)
{ set_speed(speed); replace_scenario(definition); }
ViewerController::ViewerController(const scenario::ScenarioDefinition& definition, const warehouse::WarehouseSnapshot& result)
    : ViewerController(definition)
{
    if (result.current_tick != result.horizon_ticks) throw std::invalid_argument("Result preview requires a completed horizon");
    const auto& c = std::get<scenario::WarehouseConfig>(definition.domain);
    if (result.capacity != warehouse::ResourceCounts{c.loading_bays,c.workers,c.forklifts})
        throw std::invalid_argument("Result geometry must match snapshot capacity");
    // A checkpoint branch retains the original scenario identity even when its
    // final capacity/layout and display name differ. Restore against that identity.
    const auto original = scenario::parse_scenario(result.scenario_identity);
    auto compatible = definition; compatible.name = original.name;
    auto& visual_config = std::get<scenario::WarehouseConfig>(compatible.domain);
    const auto& original_config = std::get<scenario::WarehouseConfig>(original.domain);
    visual_config.loading_bays = original_config.loading_bays; visual_config.workers = original_config.workers; visual_config.forklifts = original_config.forklifts;
    if (scenario::serialize_scenario(compatible) != scenario::serialize_scenario(original))
        throw std::invalid_argument("Result preview parameters must match its recorded scenario history");
    auto runtime = std::make_unique<warehouse::WarehouseSimulation>(original);
    runtime->restore_snapshot(result); runtime->pause(); runtime_ = std::move(runtime); result_preview_ = true; refresh(true);
}
void ViewerController::replace_scenario(const scenario::ScenarioDefinition& definition)
{
    auto runtime = std::make_unique<warehouse::WarehouseSimulation>(definition);
    auto scene = std::make_unique<visualization::LiveWarehouseScene>(definition);
    const auto pixels = viewport_.pixels();
    auto camera = std::make_unique<OrbitCamera>(scene->layout().bounds,static_cast<float>(pixels.width)/pixels.height);
    auto initial = runtime->capture_snapshot(); runtime->set_time_scale(speed_);
    definition_ = definition; runtime_ = std::move(runtime); scene_ = std::move(scene); camera_ = std::move(camera); initial_ = std::move(initial);
    result_preview_ = false; command_history_.clear(); next_command_=0; selection_.reset(); cancel_drag(); refresh(true);
}
void ViewerController::restore_session(const warehouse::WarehouseSnapshot& state, const simulation::ClockSnapshot& clock,
    const CameraPose& camera, std::optional<VisualKey> selection, std::vector<warehouse::CapacityChange> history)
{
    simulation::validate_snapshot(clock);
    if (clock.ticks != state.current_tick || clock.ticks_per_second != state.tick_rate)
        throw std::invalid_argument("Saved simulation clock is inconsistent");
    auto prepared=std::make_unique<warehouse::WarehouseSimulation>(definition_);
    prepared->restore_snapshot(state); prepared->restore_clock(clock);
    if(history.empty()) history=state.capacity_changes;
    if(history.size()>warehouse::max_capacity_changes || history.size()<state.capacity_changes.size() ||
        !std::equal(state.capacity_changes.begin(),state.capacity_changes.end(),history.begin()))
        throw std::invalid_argument("Saved command history conflicts with operational snapshot");
    std::uint64_t previous=0;
    for(const auto& command:history) {
        (void)warehouse::resource_name(command.resource);
        if(command.tick<previous || command.tick>state.horizon_ticks || command.capacity>100000 ||
            command.started_before>scenario::max_warehouse_shipments) throw std::invalid_argument("Invalid saved command timeline");
        previous=command.tick;
    }
    if(history.size()>state.capacity_changes.size() && history[state.capacity_changes.size()].tick<state.current_tick)
        throw std::invalid_argument("Saved command timeline is behind its clock");
    if(history.size()>state.capacity_changes.size()) {
        // A backward seek can retain future recorded commands. Validate that tail
        // in isolation; the playable runtime still restores the exact saved snapshot.
        auto check=warehouse::WarehouseSimulation::create_from(state);
        for(std::size_t i=state.capacity_changes.size();i<history.size();++i) {
            const auto& command=history[i];check.advance_to(command.tick);
            if(check.metrics().started!=command.started_before) throw std::invalid_argument("Recorded command reservation history mismatch");
            check.set_resource_capacity(command.resource,command.capacity);
        }
        if(check.capture_snapshot().capacity_changes!=history) throw std::invalid_argument("Recorded command timeline mismatch");
    }
    auto pose=*camera_; pose.restore_pose(camera);
    set_speed(clock.time_scale); runtime_=std::move(prepared); *camera_=pose;
    command_history_=std::move(history); next_command_=state.capacity_changes.size();
    selection_.reset(); cancel_drag(); refresh(true); select(selection);
}
void ViewerController::advance_timeline(std::uint64_t tick)
{
    while(next_command_<command_history_.size() && command_history_[next_command_].tick<=tick) {
        const auto& command=command_history_[next_command_];
        runtime_->advance_to(command.tick);
        if(runtime_->metrics().started!=command.started_before) throw std::invalid_argument("Recorded command history diverged");
        runtime_->set_resource_capacity(command.resource,command.capacity); ++next_command_;
    }
    runtime_->advance_to(tick);
}
void ViewerController::toggle_playback()
{
    if (result_preview_) return;
    if (runtime_->clock().paused()) { if (!observation_.at_horizon) runtime_->resume(); }
    else runtime_->pause();
}
void ViewerController::step()
{
    if (result_preview_) return;
    if (!runtime_->clock().paused() || observation_.at_horizon) return;
    advance_timeline(runtime_->clock().tick_count()+1); refresh();
}
void ViewerController::set_speed(double speed)
{
    if (!std::isfinite(speed) || speed < 0.01 || speed > 3600) throw std::invalid_argument("Playback speed must be in [0.01,3600]");
    if (runtime_) runtime_->set_time_scale(speed); speed_ = speed;
}
void ViewerController::reset()
{
    if (result_preview_) throw std::logic_error("Final result preview has no playback timeline; return to baseline");
    runtime_->restore_snapshot(initial_); next_command_=0; runtime_->set_time_scale(speed_); runtime_->pause(); selection_.reset(); cancel_drag(); refresh(true);
}
void ViewerController::seek(std::uint64_t tick)
{
    if (result_preview_) throw std::logic_error("Final result preview has no playback timeline; return to baseline");
    if (tick > runtime_->horizon_ticks()) throw std::invalid_argument("Seek target exceeds scenario horizon");
    if (tick < observation_.tick) {runtime_->restore_snapshot(initial_); next_command_=0;}
    advance_timeline(tick);
    // A seek establishes a tick boundary; discard old real-time fractional phase.
    if (runtime_->clock().capture_snapshot().remainder != 0) runtime_->restore_snapshot(runtime_->capture_snapshot());
    runtime_->set_time_scale(speed_); runtime_->pause(); selection_.reset(); cancel_drag(); refresh(true);
}
void ViewerController::advance(std::chrono::nanoseconds elapsed)
{
    if (elapsed.count() < 0) throw std::invalid_argument("Negative viewer frame duration");
    const auto tick = observation_.tick;
    if (elapsed.count() > 0 && !runtime_->clock().paused())
    {
        if(command_history_.empty()) runtime_->advance_elapsed(elapsed);
        else {
            simulation::SimulationClock scheduled(runtime_->clock().ticks_per_second());
            scheduled.restore_snapshot(runtime_->clock().capture_snapshot()); (void)scheduled.advance(elapsed);
            const auto target=std::min(scheduled.tick_count(),runtime_->horizon_ticks());
            advance_timeline(target);
            auto state=scheduled.capture_snapshot(); state.ticks=target;
            if(target==runtime_->horizon_ticks()) state.remainder=0;
            runtime_->restore_clock(state);
        }
        // A sub-tick frame can settle arrival zero without changing the tick.
        if (runtime_->clock().tick_count() != tick || runtime_->metrics() != observation_.metrics) refresh();
    }
    scene_->animate(elapsed);
}
void ViewerController::refresh(bool reset)
{
    observation_ = runtime_->capture_observation(scene_->waiting_limit(),scene_->processing_limit()); scene_->set_state(observation_,reset);
    select(selection_);
}
void ViewerController::set_viewport(Viewport viewport, bool preserve_camera)
{
    const auto pixels = viewport.pixels(); camera_->set_aspect(static_cast<float>(pixels.width)/pixels.height,preserve_camera); viewport_ = viewport;
}
void ViewerController::cancel_drag() noexcept { drag_.reset(); dragged_ = false; }
void ViewerController::input(const InputEvent& e, InputCapture capture)
{
    if (e.kind == InputKind::FocusLost) { cancel_drag(); return; }
    if (e.kind == InputKind::Key)
    {
        if (capture.keyboard || e.repeat) return;
        switch (e.key)
        {
        case Key::Space: toggle_playback(); break;
        case Key::Frame: camera_->frame(scene_->layout().bounds); break;
        case Key::ResetCamera: camera_->reset(); break;
        case Key::Escape: selection_.reset(); break;
        case Key::Step: step(); break;
        default: break;
        }
        return;
    }
    valid(e.position);
    if (e.kind == InputKind::Up)
    {
        if (drag_ == e.button && e.button == MouseButton::Left && !dragged_ && !capture.mouse && viewport_.contains(e.position))
        {
            const auto ray = pointer_ray(camera_->camera(),viewport_,e.position);
            selection_ = ray ? pick(*ray,scene_->selectables()) : std::nullopt;
        }
        cancel_drag(); return;
    }
    if (capture.mouse || !viewport_.contains(e.position)) { cancel_drag(); return; }
    if (e.kind == InputKind::Down && (e.button == MouseButton::Left || e.button == MouseButton::Middle))
    { drag_ = e.button; press_ = previous_ = e.position; dragged_ = e.button == MouseButton::Middle; }
    else if (e.kind == InputKind::Move && drag_)
    {
        if (!dragged_ && glm::length(e.position-press_) >= 5) dragged_ = true;
        if (dragged_)
        { if (*drag_ == MouseButton::Left) camera_->orbit(e.position-previous_); else camera_->pan(e.position-previous_,viewport_.size.y); }
        previous_ = e.position;
    }
    else if (e.kind == InputKind::Wheel) camera_->zoom(e.wheel);
}
void ViewerController::select(std::optional<VisualKey> key)
{
    selection_.reset(); if (!key) return;
    for (const auto& o : scene_->selectables()) if (o.key == *key) { selection_ = key; break; }
}
std::optional<Inspector> ViewerController::inspect() const
{
    if (!selection_) return std::nullopt;
    const auto key = *selection_;
    const auto found = std::find_if(scene_->selectables().begin(),scene_->selectables().end(),[&](const auto& o){ return o.key == key; });
    if (found == scene_->selectables().end()) return std::nullopt;
    Inspector info{key,(found->bounds.minimum+found->bounds.maximum)*0.5f};
    if (key.category == VisualCategory::Shipment)
    {
        const auto find = [&](const auto& records)
        { for (const auto& r : records) if (r.id == key.id) info.shipment = r; };
        find(observation_.waiting); find(observation_.processing);
        if (info.shipment)
        {
            const auto& r = *info.shipment;
            info.waiting_ticks = r.start_tick.value_or(observation_.tick)-r.arrival_tick;
            if (r.start_tick) info.processing_ticks = r.completion_tick.value_or(observation_.tick)-*r.start_tick;
        }
    }
    else if (key.category != VisualCategory::Rack)
    {
        for (const auto& r : scene_->resources())
        {
            const auto kind = r.kind == visualization::WarehouseVisualKind::LoadingBay ? VisualCategory::LoadingBay :
                r.kind == visualization::WarehouseVisualKind::Worker ? VisualCategory::Worker : VisualCategory::Forklift;
            if (kind == key.category && r.id == key.id) info.busy = r.busy;
        }
        const auto& m = observation_.metrics;
        if (key.category == VisualCategory::LoadingBay) { info.capacity = observation_.capacity.loading_bays; info.usage = observation_.usage.loading_bays; info.utilization = m.loading_bay_utilization; }
        else if (key.category == VisualCategory::Worker) { info.capacity = observation_.capacity.workers; info.usage = observation_.usage.workers; info.utilization = m.worker_utilization; }
        else { info.capacity = observation_.capacity.forklifts; info.usage = observation_.usage.forklifts; info.utilization = m.forklift_utilization; }
    }
    return info;
}
const renderer::RenderFrameData& ViewerController::present()
{ scene_->present(camera_->camera(),viewport_.pixels(),selection_); return scene_->frame(); }
}
