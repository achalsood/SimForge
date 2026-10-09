#pragma once
#include "workspace_panel.hpp"
#include <simforge/workspace/project.hpp>
enum class ProjectAction { New, Open, Save, SaveAs, Recover, DiscardRecovery, Close };
struct ProjectUI
{
    std::optional<ProjectAction> pending, confirmation;
    std::filesystem::path test_path, recovery_path;
    bool show_recovery{false}, changed_workspace{false}, dialog_used{false};
    bool close_requested{false};
    bool recovery_deferred{false};
    std::string notice, error;
    glm::vec2 new_button{}, open_button{}, save_button{}, save_as_button{}, discard{}, cancel{}, confirm_save{};
};
void process_project(simforge::workspace::Project& project,simforge::workspace::ScenarioWorkspace& workspace,
    ProjectUI& ui,DashboardState& dashboard,WorkspaceUI& editor);
void draw_project(simforge::workspace::Project& project,simforge::workspace::ScenarioWorkspace& workspace,
    ProjectUI& ui,DashboardState& dashboard,const DashboardFrame& frame);
void capture_project_preferences(simforge::workspace::Project& project,const DashboardState& dashboard,const WorkspaceUI& editor);
