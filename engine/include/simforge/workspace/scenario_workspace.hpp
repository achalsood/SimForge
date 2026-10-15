#pragma once
#include <simforge/analysis/scenario_comparison.hpp>
#include <simforge/analysis/statistical_experiment.hpp>
#include <simforge/analysis/sensitivity.hpp>
#include <simforge/analysis/interaction.hpp>
#include <simforge/analysis/confirmation.hpp>
#include <simforge/viewer/viewer.hpp>
#include <simforge/scenario/scenario_language.hpp>
#include <simforge/workspace/calibration_execution.hpp>
#include <array>

namespace simforge::workspace
{
class ExperimentExecution;
enum class Field : std::size_t { Name, Bays, Workers, Forklifts, Arrival, Processing, Duration, TickRate, Seed, Count };
inline constexpr std::array<const char*,9> field_names{"Name","Loading bays","Workers","Forklifts","Arrival interval (s)",
    "Processing duration (s)","Simulation duration (s)","Tick rate (Hz)","Random seed"};
struct WorkloadDraft
{
    scenario::ArrivalMode arrivals{scenario::ArrivalMode::Constant};
    scenario::ProcessingMode processing{scenario::ProcessingMode::Constant};
    std::array<std::string,4> bounds{"240","480","600","1200"};
    std::vector<std::array<std::string,3>> segments; // Raw simulated start/end/interval seconds; invalid edits are retained.
    std::string rate_per_second{"0.016666666666666666"};
    std::vector<std::array<std::string,3>> intensities;
    bool operator==(const WorkloadDraft&) const = default;
};
struct Draft { std::array<std::string,9> values; WorkloadDraft workload; bool operator==(const Draft&) const = default; };
[[nodiscard]] WorkloadDraft workload_draft(const scenario::WarehouseConfig& config);
void apply_workload_draft(scenario::WarehouseConfig& config,const WorkloadDraft& draft);
struct ValidationError { std::string field, message; };
struct Candidate { std::uint64_t id; scenario::ScenarioDefinition definition; std::string notes, changed_inputs; };
struct PlayableRun
{
    std::uint64_t id;
    std::unique_ptr<viewer::ViewerController> controller;
};
inline constexpr std::size_t max_custom_runs = 8;
enum class ComparisonMode { Initial, Checkpoint };
enum class Status { Empty, Ready, Modified, Invalid, ComparisonReady, Results, ExportFailed };
struct Comparison
{
    std::optional<analysis::ConfirmationRequest> confirmation;
    std::optional<analysis::SensitivityReport> sensitivity;
    std::optional<analysis::InteractionReport> interaction;
    std::optional<analysis::StatisticalReport> statistics;
    std::uint64_t request_id{0};
    std::vector<std::uint64_t> candidate_ids;
    // Final baseline then canonical candidates; asynchronous previews never rerun a simulation.
    std::vector<warehouse::WarehouseSnapshot> final_snapshots;
    ComparisonMode mode{ComparisonMode::Initial};
    scenario::ScenarioDefinition baseline_definition;
    analysis::Objective objective{analysis::Objective::MaximizeCompleted};
    analysis::CandidateResult baseline;
    std::vector<analysis::CandidateResult> candidates;
    std::vector<analysis::BottleneckEvidence> bottlenecks;
    std::vector<std::string> ranking, warnings, limitations;
    // Fresh runs retain independent authoritative reports; checkpoint retains one shared report.
    std::vector<analysis::AnalysisReport> executions;
    std::vector<scenario::ScenarioDefinition> definitions; // Canonical candidate-name order.
    std::vector<std::string> changed_inputs;
    double execution_ms{0}, report_ms{0};
};
inline constexpr std::size_t max_studies=32, max_study_bytes=32*1024*1024,
    max_history_bytes=192*1024*1024, max_study_observations=10'100;
enum class StudyType { SingleSeed, Statistical, Sensitivity, Interaction, Confirmation };
const char* study_type_name(StudyType);
const char* study_type_label(StudyType);
StudyType study_type(const Comparison&);
struct StudyViewState {
    std::uint64_t configuration{0},seed{0};
    int metric{0};
    std::array<std::uint32_t,4> rectangle{}; // x0, x1, y0, y1; never a UI row index.
    bool operator==(const StudyViewState&) const = default;
};
struct StudyRecord {
    std::string id,title,notes,completed;
    StudyType type{StudyType::SingleSeed};
    std::shared_ptr<const Comparison> evidence;
    std::size_t serialized_bytes{0};
    StudyViewState view;
    std::vector<std::vector<std::uint64_t>> reserved_confirmation_seeds;
};
std::string comparison_json(const Comparison&);
struct CalibrationStudy {
    std::shared_ptr<const analysis::demand::Report> evidence;
    std::size_t serialized_bytes{0};
};
std::string comparison_csv(const Comparison&);
struct ExperimentRequest
{
    std::uint64_t id{0};
    scenario::ScenarioDefinition baseline;
    std::vector<Candidate> candidates;
    ComparisonMode mode{ComparisonMode::Initial};
    analysis::Objective objective{analysis::Objective::MaximizeCompleted};
    std::optional<std::uint64_t> checkpoint_tick;
    std::optional<warehouse::WarehouseSnapshot> checkpoint;
    std::optional<analysis::ReplicationPlan> replications;
    std::optional<analysis::SensitivityRequest> sensitivity;
    std::optional<analysis::InteractionRequest> interaction;
    std::optional<analysis::ConfirmationRequest> confirmation;
};
void validate_experiment_request(const ExperimentRequest& request);
[[nodiscard]] Comparison execute_experiment(const ExperimentRequest& request,
    const simulation::ExecutionControl* control = nullptr);
class ScenarioWorkspace
{
public:
    ScenarioWorkspace();
    ~ScenarioWorkspace();
    ScenarioWorkspace(ScenarioWorkspace&&) noexcept;
    ScenarioWorkspace& operator=(ScenarioWorkspace&&) noexcept;
    explicit ScenarioWorkspace(const scenario::ScenarioDefinition& definition, double speed = 300);
    void open(const std::filesystem::path& path, bool discard_draft = false);
    void load(const scenario::ScenarioDefinition& definition, bool discard_draft = false);
    void edit(Field field, std::string value);
    void edit_workload(WorkloadDraft draft);
    [[nodiscard]] scenario::ScenarioDefinition draft_definition() const;
    [[nodiscard]] scenario::language::Stamp language_stamp() const noexcept {return {language_epoch_,draft_revision_};}
    void apply_language_proposal(const scenario::language::Proposal& proposal,bool approved);
    [[nodiscard]] bool validate_draft();
    [[nodiscard]] bool dirty() const;
    void revert(bool discard_draft = false);
    void save_draft(const std::filesystem::path& path);
    void run_draft();
    std::uint64_t create_run(const scenario::ScenarioDefinition& definition);
    void select_run(std::uint64_t id); // 0 is the preserved original baseline.
    void remove_run(std::uint64_t id);
    void resume_live_view();
    void simulate_candidate(std::uint64_t id);
    void simulate_result(std::string_view name);
    [[nodiscard]] const std::vector<PlayableRun>& runs() const noexcept { return runs_; }
    [[nodiscard]] std::uint64_t active_run_id() const noexcept { return active_run_; }
    std::uint64_t add_candidate(std::string notes = {});
    std::uint64_t duplicate_candidate(std::uint64_t id, std::string name);
    void rename_candidate(std::uint64_t id, std::string name);
    void edit_candidate(std::uint64_t id, bool discard_draft = false);
    void update_candidate(std::uint64_t id, std::string notes = {});
    void remove_candidate(std::uint64_t id);
    void restore_candidate(std::uint64_t id);
    void compare(ComparisonMode mode, analysis::Objective objective, std::optional<std::uint64_t> checkpoint_tick = {});
    [[nodiscard]] ExperimentRequest capture_request(ComparisonMode mode, analysis::Objective objective,
        std::optional<std::uint64_t> tick = {}) const;
    void start_experiment(ComparisonMode mode, analysis::Objective objective, std::optional<std::uint64_t> tick = {});
    void start_statistical_experiment(ComparisonMode mode, analysis::Objective objective,const analysis::ReplicationPlan& plan);
    void start_interaction(analysis::InteractionRequest request);
    std::uint64_t add_interaction_candidate(std::uint64_t experiment,std::uint64_t cell,std::string name,std::uint64_t seed);
    void simulate_interaction(std::uint64_t experiment,std::uint64_t cell,std::uint64_t seed);
    void export_interaction_csv(const std::filesystem::path&,bool per_seed=false);
    void start_sensitivity(analysis::SensitivityRequest request);
    std::uint64_t add_sensitivity_candidate(std::uint64_t point,std::string name,std::uint64_t seed);
    void simulate_sensitivity(std::uint64_t point,std::uint64_t seed);
    void export_marginal_csv(const std::filesystem::path& path);
    bool poll_experiment();
    ExperimentExecution& execution();
    void view_result(std::optional<std::string> candidate); // nullopt previews report baseline.
    void return_to_baseline();
    void export_json(const std::filesystem::path& path);
    void export_csv(const std::filesystem::path& path);
    void export_seed_csv(const std::filesystem::path& path);
    [[nodiscard]] std::string json() const;
    [[nodiscard]] std::string csv() const;
    static void verify_export(std::string_view json);
    [[nodiscard]] viewer::ViewerController& view();
    [[nodiscard]] const viewer::ViewerController& baseline_viewer() const;
    [[nodiscard]] const scenario::ScenarioDefinition& baseline() const;
    [[nodiscard]] const Draft& draft() const noexcept { return draft_; }
    [[nodiscard]] const std::vector<ValidationError>& errors() const noexcept { return errors_; }
    [[nodiscard]] const std::vector<Candidate>& candidates() const noexcept { return candidates_; }
    [[nodiscard]] const std::shared_ptr<const Comparison>& report() const noexcept { return report_; }
    [[nodiscard]] const std::vector<StudyRecord>& studies() const noexcept {return studies_;}
    CalibrationExecution& calibration_execution();
    void start_calibration(analysis::demand::FileRequest,CalibrationExecution::Observer={});
    bool poll_calibration();
    void import_calibration(analysis::demand::FileRequest);
    void fit_calibration(analysis::demand::FitSettings);
    [[nodiscard]] const std::shared_ptr<const analysis::demand::Dataset>& calibration_dataset()const noexcept{return calibration_dataset_;}
    [[nodiscard]] const std::shared_ptr<const analysis::demand::Report>& calibration_report()const noexcept{return calibration_report_;}
    [[nodiscard]] const std::vector<CalibrationStudy>& calibrations()const noexcept{return calibrations_;}
    void retain_calibration(std::shared_ptr<const analysis::demand::Report>);
    void select_calibration(std::string_view);
    void delete_calibration(std::string_view,bool confirmed);
    void discard_calibration(bool confirmed);
    bool has_pending_calibration()const;
    void record_calibration_conversion(analysis::demand::Conversion);
    [[nodiscard]] const std::string& selected_study_id() const noexcept {return selected_study_;}
    [[nodiscard]] std::size_t history_bytes() const noexcept;
    void select_study(std::string_view id);
    void rename_study(std::string_view id,std::string title,std::string notes);
    void delete_study(std::string_view id,bool confirmed);
    void study_view(StudyViewState view);
    // Ownership transfers only after validation and retention checks succeed.
    void retain_study(Comparison report,std::string title={});
    bool retain_pending_study();
    [[nodiscard]] bool has_pending_study() const noexcept {return bool(pending_study_);}
    void discard_pending_study(bool confirmed);
    void export_pending_json(const std::filesystem::path&);
    [[nodiscard]] analysis::ConfirmationRequest freeze_confirmation(std::uint64_t selection,const analysis::ReplicationPlan&) const;
    void start_confirmation(analysis::ConfirmationRequest,
        std::function<void(std::string_view,std::string_view,std::uint64_t,std::uint64_t)> observer={});
    [[nodiscard]] std::string history_json() const;
    void export_history_json(const std::filesystem::path&);
    void export_study_json(const std::filesystem::path&);
    [[nodiscard]] Status status() const noexcept { return status_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] const std::string& viewed_identity() const noexcept { return viewed_identity_; }
    [[nodiscard]] bool viewing_baseline() const noexcept { return !preview_view_ && active_run_ == 0; }
private:
    friend class Project;
    friend struct ProjectCodec;
    friend std::shared_ptr<const Comparison> parse_comparison_storage_json(std::string_view,std::uint64_t);
    std::uint64_t persistent_generation_{0};
    std::uint64_t language_epoch_{0}, draft_revision_{0}; // Runtime source context; no project-schema change.
    std::uint64_t report_generation_{0}, preview_report_generation_{0};
    static Draft make_draft(const scenario::ScenarioDefinition& definition);
    scenario::ScenarioDefinition checked_draft();
    Candidate& candidate(std::uint64_t id);
    void check_name(const std::string& name, std::optional<std::uint64_t> excluding = {}) const;
    void invalidate();
    std::optional<scenario::ScenarioDefinition> baseline_;
    Draft draft_, draft_origin_;
    std::vector<ValidationError> errors_;
    std::vector<Candidate> candidates_;
    std::shared_ptr<const Comparison> report_;
    std::vector<StudyRecord> studies_;
    std::string selected_study_;
    std::uint64_t next_study_id_{1};
    std::shared_ptr<const Comparison> pending_study_;
    void publish_study(std::shared_ptr<const Comparison>,std::string title={});
    std::unique_ptr<viewer::ViewerController> baseline_view_, preview_view_;
    std::vector<PlayableRun> runs_;
    std::uint64_t active_run_{0}, next_run_id_{1};
    std::uint64_t next_id_{1};
    double speed_{300};
    Status status_{Status::Empty};
    std::string message_, viewed_identity_;
    std::unique_ptr<ExperimentExecution> execution_;
    std::unique_ptr<CalibrationExecution> calibration_execution_{std::make_unique<CalibrationExecution>()};
    std::vector<CalibrationStudy> calibrations_;
    std::shared_ptr<const analysis::demand::Report> calibration_report_;
    std::shared_ptr<const analysis::demand::Dataset> calibration_dataset_;
};
[[nodiscard]] std::string differences(const scenario::ScenarioDefinition& baseline, const scenario::ScenarioDefinition& candidate);
[[nodiscard]] const char* objective_label(analysis::Objective objective);
[[nodiscard]] const char* change_label(std::optional<double> delta, bool higher_is_better);
[[nodiscard]] std::uint64_t checkpoint_tick(const scenario::ScenarioDefinition& definition, std::string_view integer_seconds);
}
