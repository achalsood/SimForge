#pragma once
#include <simforge/viewer/viewer.hpp>
#include <string_view>
#include <simforge/visualization/design_palette.hpp>

namespace simforge::viewer
{
namespace design
{
using namespace simforge::visualization::design;
inline constexpr float body = 14, label = 12, heading = 18, metric = 28;
inline constexpr float gap = 10, padding = 18, control_height = 36, corner = 7;
}
enum class ApplicationMode { Simulation, Editor, Analysis };
struct Rect
{
    glm::vec2 origin{0}, size{0};
    [[nodiscard]] bool contains(glm::vec2 p) const noexcept;
};
struct PanelPreferences
{
    bool metrics{true}, inspector{true}, metrics_explicit{false}, focus{false};
    bool workspace{false};
    ApplicationMode mode{ApplicationMode::Simulation};
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
