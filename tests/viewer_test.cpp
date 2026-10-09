#include <simforge/viewer/viewer.hpp>
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <source_location>
#include <string_view>

using namespace simforge;
using namespace simforge::viewer;
using namespace std::chrono_literals;
namespace
{
void check(bool condition, std::source_location loc = std::source_location::current())
{ if (!condition) throw std::runtime_error("Viewer check failed at line "+std::to_string(loc.line())); }
template<class F> void rejects(F f) { bool caught = false; try { f(); } catch (const std::exception&) { caught = true; } check(caught); }
void near(double a, double b, double epsilon = 1e-4) { check(std::abs(a-b) < epsilon); }
scenario::ScenarioDefinition definition(std::uint32_t horizon = 12, std::uint32_t rate = 60)
{
    scenario::ScenarioDefinition d; d.name = "Viewer tests"; d.simulation = {rate,horizon,42};
    d.domain = scenario::WarehouseConfig{3,3,1,1,4}; return d;
}
const renderer::SceneBounds bounds{{-10,0,-8},{10,6,8}};
const Viewport viewport{{240,60},{800,500},{1,1}};
InputEvent key(Key value, bool repeat = false)
{ InputEvent e{InputKind::Key}; e.key = value; e.repeat = repeat; return e; }
InputEvent pointer(InputKind kind, glm::vec2 point, MouseButton button = MouseButton::Left)
{ InputEvent e{kind}; e.position = point; e.button = button; return e; }
void camera_orbit()
{
    OrbitCamera c{bounds}; const auto yaw = c.yaw(), pitch = c.pitch(); const auto target = c.target();
    c.orbit({25,10}); near(c.yaw(),yaw+0.15f); near(c.pitch(),pitch+0.06f); check(c.target() == target);
    c.orbit({0,10000}); near(c.pitch(),1.35); c.orbit({0,-10000}); near(c.pitch(),0.17);
    for(int i = 0; i < 1000; ++i) c.orbit({10000,-10000}); check(renderer::finite(c.camera().view_projection()));
}
void camera_pan()
{
    OrbitCamera a{bounds}, b{bounds}; const auto start = a.target(); a.pan({10,0},500);
    near(glm::length(a.target()-start),a.distance()*0.82842712f*10/500);
    b.zoom(4); const auto target = b.target(); b.pan({10,0},500); check(glm::length(b.target()-target) < glm::length(a.target()-start));
    a.pan({0,10000},std::numeric_limits<float>::min()); check(renderer::finite(a.camera().view_projection())); check(a.camera().position.y >= 0.3f);
}
void camera_zoom()
{
    OrbitCamera c{bounds}; const auto d = c.distance(); c.zoom(1); near(c.distance(),d*std::exp(-0.1),1e-3);
    for(int i = 0; i < 100; ++i) c.zoom(10000); check(c.distance() >= 3); const auto minimum = c.distance(); c.zoom(10000); check(c.distance() == minimum);
    for(int i = 0; i < 100; ++i) c.zoom(-10000); const auto maximum = c.distance(); c.zoom(-10000); check(c.distance() == maximum);
    check(renderer::finite(c.camera().view_projection()));
}
void camera_frame()
{
    OrbitCamera c{bounds,0.8f}; const auto initial = c.camera().view_projection(); c.orbit({50,40}); c.pan({40,15},500); c.zoom(3); c.reset(); check(c.camera().view_projection() == initial);
    c.set_aspect(2); check(c.camera().aspect_ratio == 2); check(c.camera().view_projection() != initial);
    c.frame({{-100,0,-80},{100,10,80}}); const auto matrix = c.camera().view_projection();
    for (int i = 0; i < 8; ++i)
    {
        auto p = matrix*glm::vec4{(i&1)?100.0f:-100.0f,(i&2)?10.0f:0.0f,(i&4)?80.0f:-80.0f,1}; p /= p.w;
        check(std::abs(p.x) <= 1 && std::abs(p.y) <= 1 && p.z >= 0 && p.z <= 1);
    }
    c.orbit({10,10}); const auto pose = c.camera().position; c.set_aspect(1); check(c.camera().position == pose);
}
void camera_invalid()
{
    OrbitCamera c{bounds}; const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    rejects([&]{c.orbit({nan,0});}); rejects([&]{c.pan({inf,0},500);}); rejects([&]{c.pan({1,1},0);});
    rejects([&]{c.zoom(inf);}); rejects([&]{c.set_aspect(0);}); rejects([&]{c.frame({{0,0,0},{0,1,1}});});
    const auto before = c.camera().view_projection(); rejects([&]{c.set_aspect(std::numeric_limits<float>::min());}); check(c.camera().view_projection() == before);
    c.orbit({std::numeric_limits<float>::max(),-std::numeric_limits<float>::max()}); check(renderer::finite(c.camera().view_projection()));
}
void viewport_test()
{
    auto v = viewport; check(v.contains({240,60}) && !v.contains({1040,60}) && !v.contains({239,100}));
    v.framebuffer_scale = {2,1.5f}; const auto p = v.pixels(); check(p.x == 480 && p.y == 90 && p.width == 1600 && p.height == 750);
    ViewerController controller{definition()}; controller.set_viewport(v); near(controller.orbit_camera().camera().aspect_ratio,1600.0/750);
    rejects([&]{v.size.x = 0; (void)v.pixels();}); rejects([&]{v = viewport; v.origin.x = -1; v.validate();});
    rejects([&]{v = viewport; v.framebuffer_scale.y = 0; v.validate();});
    rejects([&]{v = viewport; v.size.y = std::numeric_limits<float>::infinity(); v.validate();});
    rejects([&]{v = viewport; v.size = {0.01f,0.01f}; (void)v.pixels();});
}
void ray_test()
{
    renderer::Camera c; c.position = {0,0,10}; c.target = {0,0,0}; c.aspect_ratio = 1.6f;
    auto ray = pointer_ray(c,viewport,{640,310}); check(ray.has_value()); near(ray->direction.x,0); near(ray->direction.y,0); near(ray->direction.z,-1);
    auto dpi = viewport; dpi.framebuffer_scale = {2,2}; const auto other = pointer_ray(c,dpi,{640,310}); check(other->direction == ray->direction);
    check(!pointer_ray(c,viewport,{239,310}));
    const auto top = pointer_ray(c,viewport,{640,100}); check(top->direction.y > 0);
    const auto left = pointer_ray(c,viewport,{300,310}); check(left->direction.x < 0);
    rejects([&]{(void)pointer_ray(c,viewport,{std::numeric_limits<float>::quiet_NaN(),0});});
    // Project a world point then unproject its logical pointer with viewport offsets and DPI.
    c = OrbitCamera{bounds,1.6f}.camera(); const glm::vec3 world{2,1,3}; auto p = c.view_projection()*glm::vec4{world,1}; p /= p.w;
    const auto pointer = viewport.origin+(glm::vec2{p.x,p.y}+1.0f)*0.5f*viewport.size;
    const auto roundtrip = pointer_ray(c,dpi,pointer); check(glm::length(glm::cross(world-roundtrip->origin,roundtrip->direction)) < 0.002f);
}
void intersections()
{
    const Ray ray{{0,0,10},{0,0,-1},100}; const renderer::SceneBounds box{{-1,-1,-1},{1,1,1}};
    near(*intersect(ray,box),9); check(!intersect({{2,0,10},{0,0,-1},100},box));
    near(*intersect({{0,0,0},{0,0,1},100},box),0); check(!intersect({{0,0,10},{0,0,-1},8},box));
    rejects([&]{(void)intersect({{0,0,0},{0,0,0},100},box);}); rejects([&]{(void)intersect(ray,{{2,0,0},{1,1,1}});});
}
void picking()
{
    const Ray ray{{0,0,10},{0,0,-1},100};
    std::vector<visualization::Selectable> objects{
        {{VisualCategory::Shipment,9},{{-1,-1,-1},{1,1,1}},0},{{VisualCategory::Worker,1},{{-1,-1,3},{1,1,5}},1}};
    check(pick(ray,objects) == VisualKey{VisualCategory::Worker,1});
    objects.push_back({{VisualCategory::Shipment,4},objects[1].bounds,2}); check(pick(ray,objects) == VisualKey{VisualCategory::Shipment,4});
    std::reverse(objects.begin(),objects.end()); check(pick(ray,objects) == VisualKey{VisualCategory::Shipment,4});
    check(!pick({{100,0,10},{0,0,-1},100},objects)); check(!pick(ray,{}));
}
void playback()
{
    ViewerController v{definition(),1}; check(!v.runtime().clock().paused()); v.step(); check(v.observation().tick == 0);
    v.advance(1s); check(v.observation().tick == 60); v.toggle_playback(); v.advance(1s); check(v.observation().tick == 60);
    v.step(); check(v.observation().tick == 61 && v.runtime().clock().paused()); v.toggle_playback(); v.advance(1s); check(v.observation().tick == 121);
    v.seek(v.observation().horizon); v.step(); v.toggle_playback(); check(v.runtime().clock().paused() && v.observation().at_horizon);
}
void speed()
{
    ViewerController v{definition(120),1};
    for (const auto value : {0.01,1.0,10.0,60.0,300.0,900.0,3600.0}) { v.set_speed(value); check(v.speed() == value && v.runtime().clock().time_scale() == value); }
    for (const auto value : {0.0,-1.0,0.001,3601.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) rejects([&]{v.set_speed(value);});
    v.set_speed(2.5); v.advance(1s); check(v.observation().tick == 150); rejects([&]{v.advance(-1ns);});
}
void reset_test()
{
    const auto d = definition(); ViewerController v{d}; const auto original = v.runtime().capture_snapshot();
    v.seek(400); v.select(VisualKey{VisualCategory::Worker,1}); v.reset(); check(!v.selection());
    check(v.runtime().capture_snapshot() == original && v.runtime().clock().paused()); check(v.scene().shipments().empty());
    v.advance(1s); check(v.runtime().capture_snapshot() == original);
    v.step(); check(v.observation().tick == 1 && v.observation().metrics.arrived == 1);
}
void seeking()
{
    const auto d = definition(); ViewerController v{d};
    for (const auto target : {120ULL,600ULL,240ULL,0ULL,720ULL,1ULL,720ULL})
    {
        v.seek(target); warehouse::WarehouseSimulation reference{d}; reference.advance_to(target);
        check(v.runtime().capture_snapshot() == reference.capture_snapshot()); check(v.runtime().clock().paused());
        check(v.observation().metrics == reference.metrics());
        for (const auto& shipment : v.scene().shipments()) check(shipment.age == visualization::shipment_settle_duration);
    }
    const auto prior = v.runtime().capture_snapshot(); rejects([&]{v.seek(721);}); check(v.runtime().capture_snapshot() == prior);
    v.reset(); v.toggle_playback(); v.set_speed(1); v.advance(1ms); check(v.runtime().clock().capture_snapshot().remainder != 0);
    v.seek(2); check(v.runtime().clock().capture_snapshot().remainder == 0);
}
void selection()
{
    ViewerController v{definition()}; v.seek(60); v.select(VisualKey{VisualCategory::Shipment,0}); check(v.selection().has_value());
    v.seek(120); check(!v.selection()); v.select(VisualKey{VisualCategory::Shipment,0});
    v.toggle_playback(); v.set_speed(1); v.advance(1s); check(v.selection() == VisualKey{VisualCategory::Shipment,0});
    v.advance(1s); check(!v.selection()); v.select(VisualKey{VisualCategory::Shipment,1000}); check(!v.selection());
    v.select(VisualKey{VisualCategory::Worker,1}); const auto& highlighted = v.present();
    const auto index = std::find_if(v.scene().selectables().begin(),v.scene().selectables().end(),[](const auto& o){return o.key == VisualKey{VisualCategory::Worker,1};})->render_index;
    check(highlighted.objects[index].color == glm::vec3{1,0.52f,0.12f});
    v.select(std::nullopt); check(v.present().objects[index].color != glm::vec3{1,0.52f,0.12f});
}
void captures()
{
    ViewerController v{definition()}; v.set_viewport(viewport); const auto before = v.orbit_camera().camera().view_projection();
    const auto center = viewport.origin+viewport.size*0.5f;
    v.input(pointer(InputKind::Down,center),{true,false}); v.input(pointer(InputKind::Move,center+glm::vec2{40,10}),{true,false});
    v.input(pointer(InputKind::Up,center),{true,false}); check(before == v.orbit_camera().camera().view_projection() && !v.selection());
    InputEvent wheel{InputKind::Wheel}; wheel.position = center; wheel.wheel = 4; v.input(wheel,{true,false}); check(before == v.orbit_camera().camera().view_projection());
    v.seek(60);
    for (const auto& o : v.scene().selectables())
    {
        auto projected = v.orbit_camera().camera().view_projection()*glm::vec4{(o.bounds.minimum+o.bounds.maximum)*0.5f,1}; projected /= projected.w;
        const auto p = viewport.origin+(glm::vec2{projected.x,projected.y}+1.0f)*0.5f*viewport.size;
        const auto ray = pointer_ray(v.orbit_camera().camera(),viewport,p); if (!ray || !pick(*ray,v.scene().selectables())) continue;
        v.input(pointer(InputKind::Down,p),{true,false}); v.input(pointer(InputKind::Up,p),{true,false}); check(!v.selection()); break;
    }
    v.reset(); v.toggle_playback();
    v.input(key(Key::Space),{false,true}); check(!v.runtime().clock().paused()); v.input(key(Key::Space,true),{}); check(!v.runtime().clock().paused());
    v.input(key(Key::Space),{}); check(v.runtime().clock().paused()); v.input(key(Key::Step),{false,true}); check(v.observation().tick == 0);
    v.input(key(Key::Step),{}); check(v.observation().tick == 1);
}
void dragging()
{
    ViewerController v{definition()}; v.set_viewport(viewport); const auto center = viewport.origin+viewport.size*0.5f;
    const auto initial = v.orbit_camera().camera().view_projection();
    v.input(pointer(InputKind::Down,center),{}); v.input(pointer(InputKind::Move,center+glm::vec2{2,0}),{}); check(v.orbit_camera().camera().view_projection() == initial);
    v.input(pointer(InputKind::Move,center+glm::vec2{20,10}),{}); check(v.orbit_camera().camera().view_projection() != initial);
    v.input({InputKind::FocusLost},{}); const auto lost = v.orbit_camera().camera().view_projection();
    v.input(pointer(InputKind::Move,center+glm::vec2{100,100}),{}); check(v.orbit_camera().camera().view_projection() == lost);
    v.input(pointer(InputKind::Down,center),{}); v.input(pointer(InputKind::Move,{100,100}),{});
    v.input(pointer(InputKind::Move,center+glm::vec2{100,100}),{}); check(v.orbit_camera().camera().view_projection() == lost);
    v.input(key(Key::ResetCamera),{}); check(v.orbit_camera().camera().view_projection() == initial);
}
void click_test()
{
    ViewerController v{definition()}; v.set_viewport(viewport); v.seek(120);
    for (const auto& o : v.scene().selectables())
    {
        auto p = v.orbit_camera().camera().view_projection()*glm::vec4{(o.bounds.minimum+o.bounds.maximum)*0.5f,1}; p /= p.w;
        const auto point = viewport.origin+(glm::vec2{p.x,p.y}+1.0f)*0.5f*viewport.size;
        const auto ray = pointer_ray(v.orbit_camera().camera(),viewport,point); if (!ray) continue;
        const auto expected = pick(*ray,v.scene().selectables()); if (!expected) continue;
        v.input(pointer(InputKind::Down,point),{}); v.input(pointer(InputKind::Up,point),{}); check(v.selection() == expected);
        v.input(key(Key::Escape),{false,true}); check(v.selection() == expected); v.input(key(Key::Escape),{}); check(!v.selection()); return;
    }
    check(false);
}
void replace()
{
    ViewerController v{definition()}; v.seek(400); v.select(VisualKey{VisualCategory::Worker,1}); v.set_speed(60);
    auto d = definition(30); d.name = "Replacement"; v.replace_scenario(d);
    check(v.name() == "Replacement" && !v.selection() && v.observation().tick == 0 && v.observation().horizon == 1800);
    check(v.speed() == 60 && v.runtime().clock().time_scale() == 60 && v.scene().shipments().empty());
    d.domain = scenario::MovementConfig{}; rejects([&]{v.replace_scenario(d);}); check(v.observation().horizon == 1800);
}
void inspector()
{
    ViewerController v{definition()}; check(!v.inspect()); v.seek(120); v.select(VisualKey{VisualCategory::Shipment,0});
    auto i = v.inspect(); check(i->shipment->id == 0 && i->shipment->start_tick == 0 && !i->shipment->completion_tick);
    check(i->waiting_ticks == 0 && i->processing_ticks == 120);
    v.select(VisualKey{VisualCategory::Shipment,1}); i = v.inspect(); check(i->shipment->arrival_tick == 60 && !i->shipment->start_tick && !i->processing_ticks && i->waiting_ticks == 60);
    for (const auto kind : {VisualCategory::LoadingBay,VisualCategory::Worker,VisualCategory::Forklift})
    { v.select(VisualKey{kind,1}); i = v.inspect(); check(i->busy && i->usage == 1 && !i->shipment && i->utilization); check(i->capacity == (kind == VisualCategory::Forklift ? 1 : 3)); }
    v.select(VisualKey{VisualCategory::Worker,3}); check(!v.inspect()->busy);
}
void metrics()
{
    ViewerController v{definition(600)}; check(format_time(0,60) == "00:00:00"); check(format_time(3661*60,60) == "01:01:01");
    check(format_time(100ULL*3600,1) == "100:00:00"); check(format_number({},100,"%") == "N/A"); check(format_number(0.125,100,"%") == "12.50%");
    check(!v.observation().metrics.throughput_per_hour && !v.observation().metrics.loading_bay_utilization);
    rejects([]{(void)format_time(0,0);}); rejects([]{(void)format_number(std::numeric_limits<double>::infinity());});
    v.seek(300*60); const auto s = v.scene().visibility(); check(s.hidden_waiting > 0 && s.visible_waiting+s.hidden_waiting == v.observation().metrics.waiting);
}
void determinism()
{
    const auto d = definition(60); warehouse::WarehouseSimulation reference{d}; reference.run();
    for(const auto frame_ns : {1000000LL,17000000LL,310000000LL})
    {
        ViewerController v{d,60}; v.set_viewport(viewport);
        for (int n = 0; !v.observation().at_horizon; ++n)
        {
            if(n%5 == 0) { v.input(key(Key::Space),{}); const auto tick = v.observation().tick; v.advance(100ms); check(v.observation().tick == tick); v.input(key(Key::Space),{}); }
            const auto p = viewport.origin+viewport.size*0.5f; InputEvent wheel{InputKind::Wheel}; wheel.position = p; wheel.wheel = n%2?1.0f:-1.0f; v.input(wheel,{});
            v.advance(std::chrono::nanoseconds{frame_ns}); (void)v.present();
        }
        check(v.runtime().capture_snapshot() == reference.capture_snapshot());
    }
    std::mt19937 random{42}; ViewerController v{d};
    for(int n = 0; n < 100; ++n) { const auto tick = random()%3601; v.seek(tick); warehouse::WarehouseSimulation at{d}; at.advance_to(tick); check(v.runtime().capture_snapshot() == at.capture_snapshot()); }
}
}
int main(int argc, char** argv)
{
    try
    {
        check(argc == 2); const std::string_view c{argv[1]};
        if(c == "orbit") camera_orbit(); else if(c == "pan") camera_pan(); else if(c == "zoom") camera_zoom(); else if(c == "frame") camera_frame();
        else if(c == "invalid") camera_invalid(); else if(c == "viewport") viewport_test(); else if(c == "ray") ray_test();
        else if(c == "intersection") intersections(); else if(c == "picking") picking(); else if(c == "playback") playback();
        else if(c == "speed") speed(); else if(c == "reset") reset_test(); else if(c == "seek") seeking(); else if(c == "selection") selection();
        else if(c == "capture") captures(); else if(c == "drag") dragging(); else if(c == "click") click_test(); else if(c == "replace") replace();
        else if(c == "inspector") inspector(); else if(c == "metrics") metrics(); else if(c == "determinism") determinism(); else check(false);
        return 0;
    }
    catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
