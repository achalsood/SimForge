#pragma once
#include <simforge/viewer/viewer.hpp>
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
};
void style_dashboard();
struct DashboardState { std::uint64_t seek_tick{0}; bool seeking{false}; };
[[nodiscard]] simforge::viewer::Viewport dashboard_viewport(const simforge::viewer::ViewerController& viewer);
[[nodiscard]] DashboardFrame draw_dashboard(const simforge::viewer::ViewerController& viewer, DashboardState& state);
