#pragma once
#include <simforge/workspace/scenario_workspace.hpp>
#include <glm/vec2.hpp>
struct CalibrationUI {
    bool open{false},continuous{false},accepted_periodic{false};
    int tab{0},model{0};
    int scroll_target{-1}; // Opt-in SDL navigation: top, reviewed conversion, playback
    std::string path{"examples/demand-calibration/constant.csv"},start{"2026-01-05T00:00:00Z"},end{"2026-01-08T00:00:00Z"},split{"2026-01-07T00:00:00Z"};
    std::string daily_bin{"3600"},diagnostic_bin{"3600"},error,export_path{"build/demand-report.json"};
    std::string scenario_name{"Calibrated periodic demand"},seed{"42"},utc_start{"2026-01-05T00:00:00Z"},scenario_path{"build/calibrated-demand.json"};
    std::optional<simforge::analysis::demand::Conversion> preview;
    glm::vec2 toggle{},import{},fit{},cancel{},save{},preview_button{},approve{},run{},chart{};
    std::array<glm::vec2,4> tabs{};
    double render_us{0};std::size_t rendered_frames{0};
};
void draw_calibration(simforge::workspace::ScenarioWorkspace&,CalibrationUI&,float);
