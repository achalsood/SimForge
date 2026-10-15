#pragma once
#include <simforge/workspace/scenario_workspace.hpp>
#include <glm/vec2.hpp>
struct DiagnosticsUI {
    int tab{0},partition{1};
    int scroll_target{0};
    bool reimport{false},exploratory{true};
    bool navigate_source{false};
    std::string bin_seconds{"3600"},replications{"100"},seed{"42"},threshold{"10"},level{"0.95"};
    std::string prediction_start,prediction_seconds{"3600"},csv_path,error,source_id,viewed_id;
    std::string export_path{"build/demand-diagnostics.json"},title,notes;
    std::array<std::uint64_t,32> histogram{};
    double histogram_min{0},histogram_max{1};
    glm::vec2 run{},cancel{},save{},export_json{},export_csv{},rename{},source_navigation{},source_selector{};
    std::array<glm::vec2,5> tabs{};
    simforge::workspace::CalibrationExecution::Observer observer; // Opt-in owned-worker diagnostic gate.
    double render_us{0};std::size_t rendered_frames{0};
};
void draw_diagnostics(simforge::workspace::ScenarioWorkspace&,DiagnosticsUI&);
