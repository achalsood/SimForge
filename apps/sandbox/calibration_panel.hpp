#pragma once
#include <simforge/workspace/scenario_workspace.hpp>
#include <glm/vec2.hpp>
#include <future>
#include <atomic>
#include "diagnostics_panel.hpp"
struct RealizationProgress {
    std::atomic<int> phase{0};
    std::atomic<std::uint64_t> generated{0};
};
struct CalibrationUI {
    DiagnosticsUI diagnostics;
    bool open{false},continuous{false},accepted_periodic{false};
    int tab{0},model{0};
    int conversion_mode{0}; // 0 legacy periodic; 1 explicit Poisson realization.
    bool explicit_windows{false},exclude_unestimated{false};
    std::string request_path{"examples/stochastic-coverage/multi-shift.json"};
    std::vector<std::array<std::string,3>> windows;
    std::shared_future<simforge::analysis::demand::Conversion> preview_job;
    std::stop_source preview_stop;
    std::function<void(std::string_view)> preview_observer; // Opt-in diagnostic phase gate; copied into owned worker.
    std::shared_ptr<RealizationProgress> preview_progress;
    int scroll_target{-1}; // Opt-in SDL navigation: top, reviewed conversion, playback
    std::string path{"examples/demand-calibration/constant.csv"},start{"2026-01-05T00:00:00Z"},end{"2026-01-08T00:00:00Z"},split{"2026-01-07T00:00:00Z"};
    std::string daily_bin{"3600"},diagnostic_bin{"3600"},error,export_path{"build/demand-report.json"};
    std::string scenario_name{"Calibrated periodic demand"},seed{"42"},utc_start{"2026-01-05T00:00:00Z"},scenario_path{"build/calibrated-demand.json"};
    std::optional<simforge::analysis::demand::Conversion> preview;
    glm::vec2 toggle{},import{},fit{},cancel{},save{},preview_button{},approve{},run{},chart{};
    glm::vec2 load_request{},conversion_selector{},coverage_editor{};
    glm::vec2 preview_cancel{};
    std::array<glm::vec2,5> tabs{};
    double render_us{0};std::size_t rendered_frames{0};
};
void draw_calibration(simforge::workspace::ScenarioWorkspace&,CalibrationUI&,float);
