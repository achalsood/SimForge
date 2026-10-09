#pragma once
#include <simforge/analysis/scenario_comparison.hpp>
#include <simforge/viewer/viewer.hpp>
#include <array>

namespace simforge::workspace
{
enum class Field : std::size_t { Name, Bays, Workers, Forklifts, Arrival, Processing, Duration, TickRate, Seed, Count };
inline constexpr std::array<const char*,9> field_names{"Name","Loading bays","Workers","Forklifts","Arrival interval (s)",
    "Processing duration (s)","Simulation duration (s)","Tick rate (Hz)","Random seed"};
struct Draft { std::array<std::string,9> values; bool operator==(const Draft&) const = default; };
struct ValidationError { std::string field, message; };
struct Candidate { std::uint64_t id; scenario::ScenarioDefinition definition; std::string notes, changed_inputs; };
enum class ComparisonMode { Initial, Checkpoint };
enum class Status { Empty, Ready, Modified, Invalid, ComparisonReady, Results, ExportFailed };
struct Comparison
{
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
class ScenarioWorkspace
{
public:
    ScenarioWorkspace() = default;
    explicit ScenarioWorkspace(const scenario::ScenarioDefinition& definition, double speed = 300);
    void open(const std::filesystem::path& path, bool discard_draft = false);
    void load(const scenario::ScenarioDefinition& definition, bool discard_draft = false);
    void edit(Field field, std::string value);
    [[nodiscard]] bool validate_draft();
    [[nodiscard]] bool dirty() const;
    void revert(bool discard_draft = false);
    void save_draft(const std::filesystem::path& path);
    void run_draft();
    std::uint64_t add_candidate(std::string notes = {});
    std::uint64_t duplicate_candidate(std::uint64_t id, std::string name);
    void rename_candidate(std::uint64_t id, std::string name);
    void edit_candidate(std::uint64_t id, bool discard_draft = false);
    void update_candidate(std::uint64_t id, std::string notes = {});
    void remove_candidate(std::uint64_t id);
    void restore_candidate(std::uint64_t id);
    void compare(ComparisonMode mode, analysis::Objective objective, std::optional<std::uint64_t> checkpoint_tick = {});
    void view_result(std::optional<std::string> candidate); // nullopt previews report baseline.
    void return_to_baseline();
    void export_json(const std::filesystem::path& path);
    void export_csv(const std::filesystem::path& path);
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
    [[nodiscard]] bool viewing_baseline() const noexcept { return !alternative_view_; }
private:
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
    std::unique_ptr<viewer::ViewerController> baseline_view_, alternative_view_;
    std::uint64_t next_id_{1};
    double speed_{300};
    Status status_{Status::Empty};
    std::string message_, viewed_identity_;
};
[[nodiscard]] std::string differences(const scenario::ScenarioDefinition& baseline, const scenario::ScenarioDefinition& candidate);
[[nodiscard]] const char* objective_label(analysis::Objective objective);
[[nodiscard]] const char* change_label(std::optional<double> delta, bool higher_is_better);
[[nodiscard]] std::uint64_t checkpoint_tick(const scenario::ScenarioDefinition& definition, std::string_view integer_seconds);
}
