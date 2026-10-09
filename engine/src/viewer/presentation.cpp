#include <simforge/viewer/presentation.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace simforge::viewer
{
bool Rect::contains(glm::vec2 p) const noexcept
{ return p.x >= origin.x && p.y >= origin.y && p.x < origin.x+size.x && p.y < origin.y+size.y; }
DashboardLayout dashboard_layout(glm::vec2 surface, glm::vec2 framebuffer_scale, float scale, PanelPreferences panels, bool selected, bool feedback_error)
{
    Viewport{{0,0},surface,framebuffer_scale}.validate();
    if (surface.x < 640 || surface.y < 480 || !std::isfinite(scale) || scale < 1 || scale > 2)
        throw std::invalid_argument("Dashboard requires 640x480 or larger and UI scale in [1,2]");
    DashboardLayout layout; layout.scale = scale; layout.compact = surface.x/scale < 850 || surface.y < 650;
    layout.gap = design::gap*scale; layout.padding = design::padding*scale; layout.control_height = design::control_height*scale;
    const float header = (layout.compact ? 48 : 56)*scale;
    const float footer = ((layout.compact ? 132 : 96)+(feedback_error ? 22 : 0))*scale;
    const float middle = surface.y-header-footer;
    const bool metrics = !panels.focus && panels.metrics && (!layout.compact || panels.metrics_explicit);
    layout.metrics_visible = metrics;
    const bool inspector = !panels.focus && panels.inspector && selected;
    layout.shared_inspector = inspector && (surface.x < 1600 || surface.x/scale < 1200);
    const float left = panels.workspace && !panels.focus ? std::min(440*scale,surface.x*0.48f) :
        metrics || layout.shared_inspector ? std::min(276*scale,surface.x*(layout.compact ? 0.40f : 0.25f)) : 0;
    const float right = inspector && !layout.shared_inspector ? 312*scale : 0;
    layout.header = {{0,0},{surface.x,header}};
    layout.playback = {{0,surface.y-footer},{surface.x,footer}};
    layout.metrics = {{0,header},{left,middle}};
    layout.workspace = layout.metrics;
    // At the smallest window/highest scale, an editor page needs more height
    // than the scene sidebar can offer. The header toggle remains accessible.
    layout.workspace_overlay = panels.workspace && !panels.focus && middle/scale < 100;
    if(layout.workspace_overlay) layout.workspace = {{0,header},{surface.x,surface.y-header}};
    layout.inspector = {{layout.shared_inspector ? 0 : surface.x-right,header},{layout.shared_inspector ? left : right,middle}};
    layout.viewport = {{left,header},{surface.x-left-right,middle},framebuffer_scale};
    float x = layout.padding;
    const float top = layout.playback.origin.y+10*scale;
    const auto control = [&](float width)
    { Rect r{{x,top},{width*scale,layout.control_height}}; x += r.size.x+layout.gap; return r; };
    layout.play = control(layout.compact ? 56.0f : 104.0f); layout.step = control(layout.compact ? 48.0f : 84.0f);
    layout.reset = control(layout.compact ? 48.0f : 88.0f); layout.speed = control(96);
    const float label_width = layout.compact ? 0 : 108*scale;
    layout.timeline = {{layout.padding+label_width,layout.playback.origin.y+(layout.compact ? 90 : 54)*scale},
        {surface.x-2*layout.padding-label_width,26*scale}};
    (void)layout.viewport.pixels(); return layout;
}
std::string_view status_label(PresentationStatus status)
{
    switch (status)
    {
    case PresentationStatus::Running: return "Running";
    case PresentationStatus::Paused: return "Paused";
    case PresentationStatus::Completed: return "Completed";
    case PresentationStatus::Seeking: return "Seek preview";
    case PresentationStatus::Resetting: return "Resetting";
    case PresentationStatus::Loading: return "Loading";
    case PresentationStatus::Error: return "Error";
    }
    throw std::invalid_argument("Unknown presentation status");
}
PresentationStatus playback_status(bool horizon, bool paused, bool preview) noexcept
{ return preview ? PresentationStatus::Seeking : horizon ? PresentationStatus::Completed : paused ? PresentationStatus::Paused : PresentationStatus::Running; }
namespace
{
std::string decimal(std::optional<double> value, double factor, const char* suffix)
{
    if (!value) return "N/A";
    if (!std::isfinite(*value) || !std::isfinite(*value*factor)) throw std::invalid_argument("Invalid presentation metric");
    std::ostringstream out; out << std::fixed << std::setprecision(1) << *value*factor << suffix; return out.str();
}
}
std::string display_percent(std::optional<double> value)
{
    if (value && (*value < 0 || *value > 1)) throw std::invalid_argument("Utilization outside 0..100 percent");
    return decimal(value,100,"%");
}
std::string display_decimal(std::optional<double> value) { return decimal(value,1,""); }
std::string display_duration(std::optional<double> seconds)
{
    if (!seconds) return "N/A";
    if (*seconds < 0) throw std::invalid_argument("Negative duration");
    if (*seconds >= 3600) return decimal(seconds,1.0/3600," h");
    if (*seconds >= 60) return decimal(seconds,1.0/60," min");
    return decimal(seconds,1," s");
}
std::string resource_usage(std::uint32_t usage, std::uint32_t capacity)
{
    if (usage > capacity) throw std::invalid_argument("Resource usage exceeds capacity");
    return std::to_string(usage)+" / "+std::to_string(capacity)+" active";
}
std::string display_speed(double speed)
{
    if (!std::isfinite(speed) || speed < 0.01 || speed > 3600) throw std::invalid_argument("Invalid playback speed display");
    std::ostringstream text; text << std::setprecision(6) << speed << 'x'; return text.str();
}
}
