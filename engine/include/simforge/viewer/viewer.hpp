#pragma once
#include <simforge/visualization/live_warehouse.hpp>
#include <simforge/warehouse/warehouse_simulation.hpp>
#include <glm/vec2.hpp>
#include <memory>

namespace simforge::viewer
{
using visualization::VisualKey;
using visualization::VisualCategory;
// SDL/ImGui pointer coordinates are logical units; only GPU rectangles use pixels.
struct Viewport
{
    glm::vec2 origin{0}, size{1}, framebuffer_scale{1};
    void validate() const;
    [[nodiscard]] bool contains(glm::vec2 point) const noexcept;
    [[nodiscard]] renderer::RenderViewport pixels() const;
};
class OrbitCamera
{
public:
    explicit OrbitCamera(renderer::SceneBounds bounds, float aspect = 16.0f/9.0f);
    void frame(renderer::SceneBounds bounds);
    void reset();
    void set_aspect(float aspect);
    void orbit(glm::vec2 delta);
    void pan(glm::vec2 delta, float viewport_height);
    void zoom(float wheel);
    [[nodiscard]] renderer::Camera camera() const;
    [[nodiscard]] float yaw() const noexcept { return yaw_; }
    [[nodiscard]] float pitch() const noexcept { return pitch_; }
    [[nodiscard]] float distance() const noexcept { return distance_; }
    [[nodiscard]] glm::vec3 target() const noexcept { return target_; }
private:
    void fit(renderer::SceneBounds bounds);
    renderer::SceneBounds initial_, bounds_;
    glm::vec3 target_{};
    float yaw_{0}, pitch_{0}, distance_{1}, aspect_{1}, radius_{1};
    bool adjusted_{false};
};
struct Ray { glm::vec3 origin, direction; float length; };
[[nodiscard]] std::optional<Ray> pointer_ray(const renderer::Camera& camera, const Viewport& viewport, glm::vec2 pointer);
[[nodiscard]] std::optional<double> intersect(const Ray& ray, const renderer::SceneBounds& bounds);
[[nodiscard]] std::optional<VisualKey> pick(const Ray& ray, std::span<const visualization::Selectable> objects);
[[nodiscard]] std::string format_time(std::uint64_t tick, std::uint32_t rate);
[[nodiscard]] std::string format_number(std::optional<double> value, double scale = 1, const char* suffix = "");
struct Inspector
{
    VisualKey key;
    glm::vec3 position;
    std::optional<warehouse::ShipmentRecord> shipment;
    std::optional<std::uint64_t> waiting_ticks, processing_ticks;
    bool busy{false};
    std::uint32_t capacity{0}, usage{0};
    std::optional<double> utilization;
};
enum class InputKind { Down, Up, Move, Wheel, Key, FocusLost };
enum class MouseButton { Left, Middle, Other };
enum class Key { Space, Frame, ResetCamera, Escape, Step, Other };
struct InputEvent
{
    InputKind kind;
    glm::vec2 position{0}, delta{0};
    MouseButton button{MouseButton::Other};
    Key key{Key::Other};
    float wheel{0};
    bool repeat{false};
};
struct InputCapture { bool mouse{false}, keyboard{false}; };
class ViewerController
{
public:
    explicit ViewerController(const scenario::ScenarioDefinition& definition, double speed = 300);
    void replace_scenario(const scenario::ScenarioDefinition& definition);
    void toggle_playback();
    void step(); // Exactly one tick, only while paused.
    void set_speed(double speed);
    void reset(); // Untouched initial runtime, paused; does not settle arrival zero.
    void seek(std::uint64_t tick); // Plain scenario only, pauses and clears animation/selection.
    void advance(std::chrono::nanoseconds elapsed);
    void set_viewport(Viewport viewport);
    void input(const InputEvent& event, InputCapture capture);
    void select(std::optional<VisualKey> key);
    [[nodiscard]] std::optional<Inspector> inspect() const;
    [[nodiscard]] const renderer::RenderFrameData& present();
    [[nodiscard]] const warehouse::WarehouseSimulation& runtime() const noexcept { return *runtime_; }
    [[nodiscard]] const warehouse::WarehouseObservation& observation() const noexcept { return observation_; }
    [[nodiscard]] const visualization::LiveWarehouseScene& scene() const noexcept { return *scene_; }
    [[nodiscard]] const OrbitCamera& orbit_camera() const noexcept { return *camera_; }
    [[nodiscard]] const Viewport& viewport() const noexcept { return viewport_; }
    [[nodiscard]] std::optional<VisualKey> selection() const noexcept { return selection_; }
    [[nodiscard]] double speed() const noexcept { return speed_; }
    [[nodiscard]] const std::string& name() const noexcept { return definition_.name; }
private:
    void refresh(bool reset = false);
    void cancel_drag() noexcept;
    scenario::ScenarioDefinition definition_;
    std::unique_ptr<warehouse::WarehouseSimulation> runtime_;
    warehouse::WarehouseSnapshot initial_;
    std::unique_ptr<visualization::LiveWarehouseScene> scene_;
    std::unique_ptr<OrbitCamera> camera_;
    warehouse::WarehouseObservation observation_;
    Viewport viewport_;
    std::optional<VisualKey> selection_;
    double speed_{300};
    std::optional<MouseButton> drag_;
    glm::vec2 press_{0}, previous_{0};
    bool dragged_{false};
};
}
