#pragma once
#include <simforge/viewer/viewer.hpp>
#include <simforge/viewer/presentation.hpp>
#include <vector>
enum class ViewerAction { Toggle, Step, Speed, Reset, Seek, ClearSelection };
struct ViewerCommand { ViewerAction action; double speed{0}; std::uint64_t tick{0}; };
struct DashboardFrame
{
    simforge::viewer::Viewport viewport;
    std::vector<ViewerCommand> commands;
    // Last rendered widget centers, used by opt-in SDL input smoke automation.
    glm::vec2 play{}, step{}, reset{}, speed{};
    glm::vec2 timeline_min{}, timeline_max{};
    glm::vec2 metrics_toggle{}, inspector_toggle{}, focus_toggle{};
    glm::vec2 workspace_toggle{};
    simforge::viewer::DashboardLayout layout;
};
void style_dashboard(float scale = 1);
struct DashboardState
{
    std::uint64_t seek_tick{0}; bool seeking{false};
    simforge::viewer::PanelPreferences panels;
    float scale{1};
    bool panel_changed{false}, select_inspector_tab{false}, feedback_error{false};
    bool workspace_changed{false};
    std::optional<simforge::viewer::VisualKey> last_selection;
    std::string feedback;
    bool custom_requested{false};
    double custom_speed{300};
    bool performance_user_choice{false};
    std::optional<std::uint64_t> feedback_tick;
    bool feedback_paused{false};
};
[[nodiscard]] simforge::viewer::Viewport dashboard_viewport(const simforge::viewer::ViewerController& viewer, const DashboardState& state);
[[nodiscard]] DashboardFrame draw_dashboard(const simforge::viewer::ViewerController& viewer, DashboardState& state);
