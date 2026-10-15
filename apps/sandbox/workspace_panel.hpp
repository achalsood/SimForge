#pragma once
#include "dashboard.hpp"
#include "scenario_language_panel.hpp"
#include "calibration_panel.hpp"
#include <simforge/workspace/scenario_workspace.hpp>
enum class WorkspaceAction { Validate, Revert, Open, Save, Run, Add, Edit, Update, Rename, Duplicate, Remove, Restore, Compare, Cancel, View, Baseline, Json, Csv, SelectRun, RemoveRun, Live, SimulateCandidate, SimulateResult, LoadRun, ApplyLanguage, CsvSeeds, Sweep, SweepCandidate, SweepRun, CsvMarginal, Interaction, InteractionCandidate, InteractionRun, CsvInteractions, CsvInteractionSeeds };
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
    std::optional<simforge::analysis::SensitivityRequest> sensitivity;
    std::uint64_t seed{0};
    std::optional<simforge::analysis::InteractionRequest> interaction;
};
struct WorkspaceUI
{
    CalibrationUI calibration;
    std::vector<WorkspaceCommand> pending;
    std::string path{"examples/warehouse-baseline.json"}, export_path{"build/warehouse-report.json"}, save_path{"build/warehouse-alternative.json"};
    std::string candidate_name, notes, error, result_name;
    std::vector<std::string> result_order; // Cached authoritative ranks, rebuilt after comparison.
    std::optional<std::uint64_t> editing;
    std::optional<WorkspaceCommand> confirmation;
    std::optional<simforge::analysis::ConfirmationRequest> frozen_confirmation;
    std::string history_observed,study_title,study_notes,delete_study_id;
    std::string confirmation_count{"30"},confirmation_base{"50001"},confirmation_seeds;
    int confirmation_policy{0};
    glm::vec2 history_header{},confirmation_prepare{},confirmation_start{},study_rename{};
    std::vector<glm::vec2> history_open_controls;
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
    glm::vec2 poisson_rate{},add_intensity{};
    glm::vec2 seed_field{};
    std::array<glm::vec2,5> workload_arrival_options{};
    std::array<glm::vec2,2> workload_processing_options{};
    std::array<glm::vec2,4> workload_bounds{};
    glm::vec2 export_field{}, export_json{}, export_csv{};
    double results_ui_us{0}; std::uint64_t results_frames{0};
    bool analysis_page{false};
    bool interaction{false},grid_multi{false},grid_delta{false},grid_interactions{false},close_grid_settings{false};
    int grid_x{0},grid_y{1},grid_objective{0},grid_metric{0},grid_selected{0},grid_rectangle{0},grid_seed{0};
    std::array<bool,2> grid_explicit{false,false};
    std::array<std::string,2> grid_min{"1","1"},grid_max{"3","3"},grid_step{"1","1"},grid_values{"1,2,3","1,2,3"};
    std::string grid_name{"Tested interaction configuration"};
    glm::vec2 grid_toggle{},grid_start{},grid_settings{},grid_add{},grid_run{},grid_view{},grid_heatmap{},grid_seed_details{};
    std::vector<glm::vec2> grid_cell_controls;
    double grid_chart_us{0};std::uint64_t grid_chart_frames{0};
    bool sensitivity{false},sweep_multi{false},sweep_explicit{false};
    bool close_sweep_settings{false};glm::vec2 sweep_settings_header{};
    int sweep_parameter{0},sweep_objective{0},sweep_selected{0},sweep_seed{0};
    std::string sweep_min{"1"},sweep_max{"6"},sweep_step{"1"},sweep_values{"1,2,3,4,5,6"},sweep_threshold{"1"},sweep_name{"Tested configuration"};
    glm::vec2 sweep_toggle{},sweep_start{},sweep_multi_control{},sweep_point_control{},sweep_add{},sweep_run{},sweep_curve{},sweep_marginal{};
    std::array<glm::vec2,3> sweep_range_controls{};
    std::vector<glm::vec2> sweep_point_options;
    glm::vec2 sweep_parameter_control{};std::array<glm::vec2,5> sweep_parameter_options{};
    double sweep_chart_us{0};std::uint64_t sweep_chart_frames{0};
    LanguageUI language;
    glm::vec2 describe{};
    bool dialog_used{false};
    int requested_tab{-1};
    int current_tab{0};
    int scroll_target{-1}; // Opt-in smoke navigation: field 0..8, actions 10..13, top 20.
};
void apply_workspace(simforge::workspace::ScenarioWorkspace& workspace, const WorkspaceCommand& command);
void draw_workspace(simforge::workspace::ScenarioWorkspace& workspace, WorkspaceUI& ui, const DashboardFrame& frame);
