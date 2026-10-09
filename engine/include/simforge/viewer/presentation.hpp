#pragma once
#include <simforge/viewer/viewer.hpp>
#include <string_view>

namespace simforge::viewer
{
namespace design
{
inline constexpr float body = 16, label = 14, heading = 18, metric = 24;
inline constexpr float gap = 10, padding = 16, control_height = 36, corner = 5;
inline constexpr glm::vec3 text{0.90f,0.94f,0.98f}, secondary{0.56f,0.65f,0.73f};
inline constexpr glm::vec3 navy{0.008f,0.014f,0.025f}, panel{0.014f,0.023f,0.036f}, border{0.055f,0.085f,0.12f};
inline constexpr glm::vec3 waiting{1.0f,0.57f,0.075f}, processing{0.07f,0.58f,1.0f}, completed{0.10f,0.72f,0.46f};
inline constexpr glm::vec3 idle{0.34f,0.40f,0.47f}, selected{1.0f,0.76f,0.28f}, error{1.0f,0.20f,0.16f};
}
struct Rect
{
    glm::vec2 origin{0}, size{0};
    [[nodiscard]] bool contains(glm::vec2 p) const noexcept;
};
struct PanelPreferences
{
    bool metrics{true}, inspector{true}, metrics_explicit{false}, focus{false};
    bool workspace{false};
};
struct DashboardLayout
{
    Rect header, metrics, inspector, playback;
    Rect workspace;
    Rect play, step, reset, speed, timeline;
    Viewport viewport;
    bool compact{false}, shared_inspector{false};
    bool metrics_visible{false};
    bool workspace_overlay{false};
    float scale{1}, control_height{40}, padding{16}, gap{10};
};
// Pure logical layout. Compact defaults to a scene-first view; an explicit panel
// toggle overrides that default. Framebuffer scale is independent of UI scale.
[[nodiscard]] DashboardLayout dashboard_layout(glm::vec2 surface, glm::vec2 framebuffer_scale,
    float ui_scale, PanelPreferences panels, bool selected, bool feedback_error = false);
enum class PresentationStatus { Running, Paused, Completed, Seeking, Resetting, Loading, Error };
[[nodiscard]] std::string_view status_label(PresentationStatus status);
[[nodiscard]] PresentationStatus playback_status(bool horizon, bool paused, bool seek_preview = false) noexcept;
[[nodiscard]] std::string display_percent(std::optional<double> value);
[[nodiscard]] std::string display_decimal(std::optional<double> value);
[[nodiscard]] std::string display_duration(std::optional<double> seconds);
[[nodiscard]] std::string resource_usage(std::uint32_t usage, std::uint32_t capacity);
[[nodiscard]] std::string display_speed(double speed);
}
