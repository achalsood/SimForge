#pragma once
#include "dashboard.hpp"
#include "scenario_language_panel.hpp"
#include <simforge/workspace/scenario_workspace.hpp>
enum class WorkspaceAction { Validate, Revert, Open, Save, Run, Add, Edit, Update, Rename, Duplicate, Remove, Restore, Compare, Cancel, View, Baseline, Json, Csv, SelectRun, RemoveRun, Live, SimulateCandidate, SimulateResult, LoadRun, ApplyLanguage, CsvSeeds };
struct WorkspaceCommand
{
    WorkspaceAction action;
    std::string text, notes;
    std::uint64_t id{0};
    simforge::workspace::ComparisonMode mode{simforge::workspace::ComparisonMode::Initial};
    simforge::analysis::Objective objective{simforge::analysis::Objective::MaximizeCompleted};
    std::optional<std::uint64_t> tick;
    bool discard{false};
    std::optional<simforge::scenario::language::Proposal> proposal;
    bool approved{false};
    std::optional<simforge::analysis::ReplicationPlan> replications;
};
struct WorkspaceUI
{
    std::vector<WorkspaceCommand> pending;
    std::string path{"examples/warehouse-baseline.json"}, export_path{"build/warehouse-report.json"}, save_path{"build/warehouse-alternative.json"};
    std::string candidate_name, notes, error, result_name;
    std::vector<std::string> result_order; // Cached authoritative ranks, rebuilt after comparison.
    std::optional<std::uint64_t> editing;
    std::optional<WorkspaceCommand> confirmation;
    int objective{0}, mode{0};
    bool statistical{false};std::string replications{"20"},base_seed{"1001"},seed_list;
    int seed_policy{0},statistical_metric{0};
    bool close_statistical_settings{false};
    glm::vec2 statistical_settings_header{};
    glm::vec2 statistical_toggle{},statistical_count{},statistical_seed{},statistical_start{},statistical_metric_control{},statistical_candidate_control{},export_seeds{};
    std::array<glm::vec2,5> statistical_metric_options{};
    std::vector<glm::vec2> statistical_candidate_options;
    bool current_checkpoint{true}; std::string checkpoint_seconds{"7200"};
    glm::vec2 name_field{}, forklift_field{}, validate{}, add{}, compare{}, candidate_view{}, baseline_view{};
    glm::vec2 probe_toggle{};
    glm::vec2 cancel_experiment{};
    glm::vec2 editor_compare{};
    glm::vec2 run_simulation{}, simulate_result{};
    glm::vec2 workload_arrival_mode{},workload_processing_mode{},run_workload{};
    std::array<glm::vec2,3> workload_arrival_options{};
    std::array<glm::vec2,2> workload_processing_options{};
    std::array<glm::vec2,4> workload_bounds{};
    glm::vec2 export_field{}, export_json{}, export_csv{};
    double results_ui_us{0}; std::uint64_t results_frames{0};
    bool analysis_page{false};
    LanguageUI language;
    glm::vec2 describe{};
    bool dialog_used{false};
    int requested_tab{-1};
    int current_tab{0};
    int scroll_target{-1}; // Opt-in smoke navigation: field 0..8, actions 10..13, top 20.
};
void apply_workspace(simforge::workspace::ScenarioWorkspace& workspace, const WorkspaceCommand& command);
void draw_workspace(simforge::workspace::ScenarioWorkspace& workspace, WorkspaceUI& ui, const DashboardFrame& frame);
