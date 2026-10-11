#pragma once
#include "workspace_panel.hpp"
simforge::analysis::ReplicationPlan statistical_plan(const WorkspaceUI& ui);
void draw_statistical_setup(simforge::workspace::ScenarioWorkspace& workspace,WorkspaceUI& ui,float scale);
void draw_statistical_results(simforge::workspace::ScenarioWorkspace& workspace,WorkspaceUI& ui,float scale);
