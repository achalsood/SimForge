#include <simforge/viewer/presentation.hpp>
#include <simforge/visualization/visual_palette.hpp>
#include <glm/geometric.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <source_location>

using namespace simforge;
using namespace simforge::viewer;
namespace
{
void check(bool value, std::source_location here = std::source_location::current())
{ if (!value) throw std::runtime_error("Presentation check failed at line "+std::to_string(here.line())); }
template<class F> void rejects(F f) { bool rejected = false; try { f(); } catch (const std::exception&) { rejected = true; } check(rejected); }
constexpr std::array sizes{glm::vec2{640,480},glm::vec2{1024,768},glm::vec2{1280,720},glm::vec2{1600,900},glm::vec2{1920,1080}};
scenario::ScenarioDefinition definition()
{ scenario::ScenarioDefinition d; d.name = "Presentation tests"; d.simulation = {60,60,42}; d.domain = scenario::WarehouseConfig{3,4,1,1,4}; return d; }
bool within(Rect r, glm::vec2 size)
{ return r.origin.x >= 0 && r.origin.y >= 0 && r.size.x >= 0 && r.size.y >= 0 && r.origin.x+r.size.x <= size.x && r.origin.y+r.size.y <= size.y; }
void layout()
{
    for (const auto size : sizes) for (float scale : {1.0f,1.25f,1.5f,2.0f}) for (bool selected : {false,true})
    {
        const auto l = dashboard_layout(size,{1,1},scale,{},selected);
        check(within(l.header,size) && within(l.playback,size) && within(l.metrics,size) && within(l.inspector,size));
        check(within({l.viewport.origin,l.viewport.size},size)); check(l.viewport.size.x > 0 && l.viewport.size.y > 0);
        check(l.header.size.y == l.viewport.origin.y && l.viewport.origin.y+l.viewport.size.y == l.playback.origin.y);
    }
}
void panels()
{
    for(const auto size : sizes)
    {
        PanelPreferences p; p.metrics_explicit = true;
        const auto expanded = dashboard_layout(size,{1,1},1,p,false); p.metrics = false;
        const auto collapsed = dashboard_layout(size,{1,1},1,p,false); check(collapsed.viewport.size.x > expanded.viewport.size.x);
        p.metrics = true; auto inspector = dashboard_layout({1920,1080},{1,1},1,p,true);
        p.inspector = false; auto hidden = dashboard_layout({1920,1080},{1,1},1,p,true); check(hidden.viewport.size.x > inspector.viewport.size.x);
        p.focus = true; const auto focused = dashboard_layout(size,{1,1},1,p,true);
        check(focused.viewport.size.x == size.x && focused.metrics.size.x == 0 && focused.inspector.size.x == 0);
    }
    PanelPreferences p; p.metrics = false;
    auto shared = dashboard_layout({1280,720},{1,1},1,p,true); check(shared.shared_inspector && !shared.metrics_visible);
}
void viewport()
{
    const auto l = dashboard_layout({1280,720},{1,1},1,{},false);
    check(!l.viewport.contains({20,20}) && !l.viewport.contains({20,200}) && !l.viewport.contains({500,700}));
    check(l.viewport.contains(l.viewport.origin+glm::vec2{5,5}));
    rejects([]{(void)dashboard_layout({0,480},{1,1},1,{},false);});
    rejects([]{(void)dashboard_layout({639,480},{1,1},1,{},false);});
    rejects([]{(void)dashboard_layout({640,480},{1,1},0,{},false);});
    rejects([]{(void)dashboard_layout({640,480},{1,1},std::numeric_limits<float>::quiet_NaN(),{},false);});
    const auto error = dashboard_layout({640,480},{2,2},2,{},false,true);
    check(error.viewport.size.y > 0 && error.playback.size.y > dashboard_layout({640,480},{2,2},2,{},false).playback.size.y);
}
void dpi()
{
    for (float scale : {1.0f,1.25f,1.5f,2.0f}) for (auto framebuffer : {glm::vec2{1,1},glm::vec2{1.25f,1.25f},glm::vec2{1.5f,1.5f},glm::vec2{2,2},glm::vec2{2,1.5f}})
    {
        const auto l = dashboard_layout({1280,720},framebuffer,scale,{},false); const auto p = l.viewport.pixels();
        ViewerController v{definition()}; v.set_viewport(l.viewport);
        check(std::abs(v.orbit_camera().camera().aspect_ratio-static_cast<float>(p.width)/p.height) < 1e-5);
        check(p.width > 0 && p.height > 0 && p.x+p.width <= 1280*framebuffer.x && p.y+p.height <= 720*framebuffer.y);
    }
}
void controls()
{
    for (auto size : sizes) for(float scale : {1.0f,1.25f,1.5f,2.0f})
    {
        const auto l = dashboard_layout(size,{1,1},scale,{},false);
        for(auto r : {l.play,l.step,l.reset,l.speed,l.timeline})
        { check(within(r,size)); check(r.size.x >= 36*scale); check(l.playback.contains(r.origin)); }
        check(l.play.size.y >= 36*scale && l.step.size.y >= 36*scale && l.reset.size.y >= 36*scale);
        check(l.play.origin.x+l.play.size.x < l.step.origin.x && l.step.origin.x+l.step.size.x < l.reset.origin.x);
        check(l.reset.origin.x+l.reset.size.x < l.speed.origin.x && l.speed.origin.y+l.speed.size.y < l.timeline.origin.y);
    }
}
void formatting()
{
    check(format_time(3661*60,60) == "01:01:01"); check(display_percent({}) == "N/A" && display_percent(0.12345) == "12.3%");
    check(display_decimal(12345.123) == "12345.1" && display_decimal({}) == "N/A");
    check(display_duration(30) == "30.0 s" && display_duration(90) == "1.5 min" && display_duration(7200) == "2.0 h");
    check(resource_usage(128,256) == "128 / 256 active" && display_speed(300) == "300x" && display_speed(0.011) == "0.011x");
    rejects([]{(void)display_percent(1.01);}); rejects([]{(void)display_duration(-1);}); rejects([]{(void)resource_usage(2,1);});
    rejects([]{(void)display_decimal(std::numeric_limits<double>::infinity());});
}
void status()
{
    check(status_label(playback_status(false,false)) == "Running" && status_label(playback_status(false,true)) == "Paused");
    check(status_label(playback_status(true,true)) == "Completed" && status_label(playback_status(true,false)) == "Completed");
    check(status_label(playback_status(false,true,true)) == "Seek preview");
    for (const auto state : {PresentationStatus::Resetting,PresentationStatus::Loading,PresentationStatus::Error}) check(!status_label(state).empty());
    auto d = definition(); ViewerController v{d}; const auto before = v.runtime().capture_snapshot(); d.simulation.tick_rate = 0;
    rejects([&]{v.replace_scenario(d);}); check(v.runtime().capture_snapshot() == before);
    v.seek(30); check(v.runtime().clock().paused()); v.reset(); check(v.observation().tick == 0 && v.runtime().clock().paused());
}
void framing()
{
    for (const auto bounds : {renderer::SceneBounds{{-10,0,-8},{10,6,8}},renderer::SceneBounds{{0,0,0},{300,8,180}}})
        for (auto size : sizes)
        {
            const auto l = dashboard_layout(size,{1,1},1,{},false); OrbitCamera c{bounds,l.viewport.size.x/l.viewport.size.y};
            const auto old = renderer::frame_bounds(bounds,c.camera().aspect_ratio);
            check(c.distance() < glm::length(old.position-old.target));
            for(int i = 0; i < 8; ++i)
            {
                auto p = c.camera().view_projection()*glm::vec4{(i&1)?bounds.maximum.x:bounds.minimum.x,(i&2)?bounds.maximum.y:bounds.minimum.y,(i&4)?bounds.maximum.z:bounds.minimum.z,1}; p /= p.w;
                check(std::abs(p.x) < 1 && std::abs(p.y) < 1 && p.z > 0 && p.z < 1);
            }
        }
}
void manual()
{
    ViewerController v{definition()}; v.set_viewport(dashboard_layout({1280,720},{1,1},1,{},false).viewport);
    const auto auto_pose = v.orbit_camera().camera().position;
    v.set_viewport(dashboard_layout({1920,1080},{1,1},1,{},false).viewport,true); check(v.orbit_camera().camera().position == auto_pose);
    InputEvent orbit{InputKind::Down}; orbit.button = MouseButton::Left; orbit.position = v.viewport().origin+glm::vec2{50,50}; v.input(orbit,{});
    orbit.kind = InputKind::Move; orbit.position += glm::vec2{40,20}; v.input(orbit,{});
    const auto pose = v.orbit_camera().camera().position;
    v.set_viewport(dashboard_layout({1024,768},{1,1},1,{},false).viewport); check(v.orbit_camera().camera().position == pose);
    v.input({InputKind::FocusLost},{}); InputEvent frame{InputKind::Key}; frame.key = Key::Frame; v.input(frame,{});
    check(v.orbit_camera().camera().position != pose); frame.key = Key::ResetCamera; v.input(frame,{}); check(renderer::finite(v.orbit_camera().camera().view_projection()));
}
void collapse()
{
    ViewerController v{definition()}; v.seek(60); v.select(VisualKey{VisualCategory::Shipment,0}); const auto saved = v.runtime().capture_snapshot();
    auto layout = dashboard_layout({1920,1080},{1,1},1,{},true); v.set_viewport(layout.viewport);
    const auto pose = v.orbit_camera().camera().position; PanelPreferences p; p.inspector = false; p.metrics = false;
    v.set_viewport(dashboard_layout({1920,1080},{1,1},1,p,true).viewport,true);
    check(v.orbit_camera().camera().position == pose && v.selection() && v.runtime().capture_snapshot() == saved);
    InputEvent press{InputKind::Down}; press.button = MouseButton::Left; press.position = v.viewport().origin+glm::vec2{40}; v.input(press,{true,false});
    press.kind = InputKind::Move; press.position += glm::vec2{40}; v.input(press,{true,false}); check(v.orbit_camera().camera().position == pose);
}
void picking()
{
    ViewerController v{definition()}; v.seek(60);
    for(auto size : sizes) for (float scale : {1.0f,1.5f,2.0f})
    {
        v.set_viewport(dashboard_layout(size,{1.5f,1.5f},scale,{},false).viewport);
        const auto pixels = v.viewport().pixels(); bool hit = false;
        for(const auto& object : v.scene().selectables())
        {
            auto clip = v.orbit_camera().camera().view_projection()*glm::vec4{(object.bounds.minimum+object.bounds.maximum)*0.5f,1}; clip /= clip.w;
            const glm::vec2 pointer{(pixels.x+(clip.x+1)*pixels.width*0.5f)/1.5f,(pixels.y+(clip.y+1)*pixels.height*0.5f)/1.5f};
            auto ray = pointer_ray(v.orbit_camera().camera(),v.viewport(),pointer); if (!ray) continue;
            const auto key = pick(*ray,v.scene().selectables()); if (!key) continue;
            InputEvent click{InputKind::Down}; click.button = MouseButton::Left; click.position = pointer; v.input(click,{}); click.kind = InputKind::Up; v.input(click,{});
            check(v.selection() == key); hit = true; break;
        }
        check(hit);
    }
}
void colors()
{
    ViewerController v{definition()}; v.seek(120); std::optional<warehouse::ShipmentId> previous;
    for (const auto& shipment : v.scene().shipments())
    {
        check(!previous || shipment.id > *previous); previous = shipment.id;
        check(shipment.target.neutral_material);
        if (shipment.status == warehouse::ShipmentStatus::Waiting) check(shipment.target.color.r > shipment.target.color.b*3);
        else check(shipment.target.color.b > shipment.target.color.r*3);
    }
    for (const auto& resource : v.scene().resources()) check(resource.render.neutral_material);
    check(v.scene().layout_generations() == 1); v.seek(3600); for (const auto& shipment : v.scene().shipments()) check(shipment.status != warehouse::ShipmentStatus::Completed);
}
void regression()
{
    auto d = scenario::load_scenario(std::filesystem::path{SIMFORGE_SOURCE_DIR}/"examples/warehouse-baseline.json");
    ViewerController v{d};
    for(const auto size : sizes) { v.set_viewport(dashboard_layout(size,{1,1},1,{},false).viewport); (void)v.present(); v.seek(v.observation().horizon/2); }
    v.seek(v.observation().horizon); warehouse::WarehouseSimulation reference{d}; reference.run(); check(v.runtime().capture_snapshot() == reference.capture_snapshot());
    const auto& m = v.observation().metrics; check(m.arrived == 96 && m.started == 64 && m.completed == 63 && m.waiting == 32 && m.in_progress == 1);
}
}
int main(int argc, char** argv)
{
    try
    {
        check(argc == 2); const std::string_view c = argv[1];
        if(c == "layout") layout(); else if(c == "panels") panels(); else if(c == "viewport") viewport(); else if(c == "dpi") dpi();
        else if(c == "controls") controls(); else if(c == "formatting") formatting(); else if(c == "status") status();
        else if(c == "framing") framing(); else if(c == "manual") manual(); else if(c == "collapse") collapse();
        else if(c == "picking") picking(); else if(c == "colors") colors(); else if(c == "regression") regression(); else check(false);
        return 0;
    }
    catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
