#pragma once
#include <simforge/analysis/scenario_comparison.hpp>
#include <simforge/analysis/statistical_experiment.hpp>
#include <simforge/analysis/sensitivity.hpp>
#include <simforge/viewer/viewer.hpp>
#include <simforge/scenario/scenario_language.hpp>
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
    std::optional<analysis::SensitivityReport> sensitivity;
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
    [[nodiscard]] const std::optional<Comparison>& report() const noexcept { return report_; }
    [[nodiscard]] Status status() const noexcept { return status_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] const std::string& viewed_identity() const noexcept { return viewed_identity_; }
    [[nodiscard]] bool viewing_baseline() const noexcept { return !preview_view_ && active_run_ == 0; }
private:
    friend class Project;
    friend struct ProjectCodec;
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
    std::optional<Comparison> report_;
    std::unique_ptr<viewer::ViewerController> baseline_view_, preview_view_;
    std::vector<PlayableRun> runs_;
    std::uint64_t active_run_{0}, next_run_id_{1};
    std::uint64_t next_id_{1};
    double speed_{300};
    Status status_{Status::Empty};
    std::string message_, viewed_identity_;
    std::unique_ptr<ExperimentExecution> execution_;
};
[[nodiscard]] std::string differences(const scenario::ScenarioDefinition& baseline, const scenario::ScenarioDefinition& candidate);
[[nodiscard]] const char* objective_label(analysis::Objective objective);
[[nodiscard]] const char* change_label(std::optional<double> delta, bool higher_is_better);
[[nodiscard]] std::uint64_t checkpoint_tick(const scenario::ScenarioDefinition& definition, std::string_view integer_seconds);
}
