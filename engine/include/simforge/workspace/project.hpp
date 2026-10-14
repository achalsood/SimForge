#pragma once
#include <simforge/workspace/scenario_workspace.hpp>
#include <simforge/viewer/presentation.hpp>
#include <functional>
#include <memory>

namespace simforge::workspace
{
inline constexpr std::uint32_t project_format_version=2;
inline constexpr std::size_t project_max_bytes=256*1024*1024;
// 32 retained studies * 10,100 observations * approximately 100 JSON events,
// plus report summaries and viewer state. Individual evidence keeps the 4M limit.
inline constexpr std::size_t project_max_json_events=36'000'000;
inline constexpr const char* project_engine_version="warehouse-2-analysis-1";
struct ProjectPreferences
{
    int application_mode{0};
    bool metrics{true}, inspector{true}, focus{false}, workspace{false}, metrics_explicit{false};
    ComparisonMode comparison_mode{ComparisonMode::Initial};
    analysis::Objective objective{analysis::Objective::MaximizeCompleted};
    bool current_checkpoint{true};
    std::string checkpoint_seconds{"7200"};
    std::optional<std::uint64_t> selected_candidate;
    std::string notes, result_name;
    int editor_tab{0};
    bool statistical{false};
    std::string replications{"20"},base_seed{"1001"},seed_list;
    int seed_policy{0},statistical_metric{0};
    bool interaction{false},grid_multi{false},grid_delta{false},grid_interactions{false};
    int grid_x{0},grid_y{1},grid_objective{0},grid_metric{0},grid_selected{0},grid_rectangle{0},grid_seed{0};
    std::array<bool,2> grid_explicit{false,false};
    std::array<std::string,2> grid_min{"1","1"},grid_max{"3","3"},grid_step{"1","1"},grid_values{"1,2,3","1,2,3"};
    std::string grid_name{"Tested interaction configuration"};
    bool sensitivity{false},sweep_multi{false},sweep_explicit{false};
    int sweep_parameter{0},sweep_objective{0},sweep_selected{0},sweep_seed{0};
    std::string sweep_min{"1"},sweep_max{"6"},sweep_step{"1"},sweep_values{"1,2,3,4,5,6"},sweep_threshold{"1"},sweep_name{"Tested configuration"};
    bool operator==(const ProjectPreferences&) const = default;
};
struct ProjectInfo
{
    std::string id, name, created, saved;
    std::filesystem::path path;
};
struct ProjectTimings
{
    double snapshot_ms{0}, integrity_ms{0}, publication_ms{0};
    double serialization_ms{0}, write_ms{0}, replacement_ms{0};
    double parsing_ms{0}, validation_ms{0}, restoration_ms{0}, scene_ms{0}, total_ms{0};
    std::size_t bytes{0};
};
// Optional fault hook supports tests without changing real user files.
using ProjectWriteHook=std::function<void(std::string_view)>;
enum class ProjectOperation { Save, Open, RecoverySave, RecoveryOpen };
struct ProjectProgress {
    ProjectOperation operation{ProjectOperation::Save};
    std::filesystem::path path;
    std::string phase;
    std::size_t bytes{0}, studies{0};
    bool active{false}, cancellable{false};
};
struct ProjectCompletion {
    ProjectOperation operation;
    bool success{false}, cancelled{false};
    std::string error;
};
class Project
{
public:
    // Control, capture, preferences and publication belong to the workspace's
    // owning thread. Workers read only owned captures or private staged state.
    explicit Project(ScenarioWorkspace& workspace);
    ~Project();
    Project(const Project&)=delete;
    Project& operator=(const Project&)=delete;
    [[nodiscard]] const ProjectInfo& info() const noexcept { return info_; }
    [[nodiscard]] const ProjectPreferences& preferences() const noexcept { return preferences_; }
    void preferences(ProjectPreferences value);
    void rename(std::string name);
    [[nodiscard]] bool dirty() const;
    [[nodiscard]] std::string serialize() const;
    void save(const std::filesystem::path& path={}, bool save_as=false, const ProjectWriteHook& hook={});
    // Caller confirms destructive intent; validation completes before the worker is joined.
    void open(const std::filesystem::path& path);
    void start_save(const std::filesystem::path& path={},bool save_as=false,const ProjectWriteHook& hook={});
    void start_open(const std::filesystem::path& path,bool recovery=false,const ProjectWriteHook& hook={});
    bool start_recovery_if_due(const std::filesystem::path&,std::chrono::steady_clock::time_point);
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool opening() const noexcept;
    [[nodiscard]] ProjectProgress progress() const;
    // Returns false once file publication has begun; cancellation never rolls it back.
    bool cancel();
    std::optional<ProjectCompletion> poll();
    // Cooperative stop/join for application teardown; completed file publication
    // is accounted for, and a cancelled staged Open never replaces the workspace.
    void shutdown();
    void create(const scenario::ScenarioDefinition& baseline, std::string name="Untitled project");
    [[nodiscard]] const ProjectTimings& timings() const noexcept { return timings_; }
    // One local recovery slot; primary saves are never used as recovery destinations.
    bool recover_if_due(const std::filesystem::path& recovery, std::chrono::steady_clock::time_point now);
    void recover(const std::filesystem::path& recovery);
    static bool recovery_available(const std::filesystem::path& recovery);
    static void discard_recovery(const std::filesystem::path& recovery);
    void discard_own_recovery(const std::filesystem::path& recovery) const;
private:
    struct Operation;
    std::unique_ptr<Operation> operation_;
    void require_idle() const;
    void submit_save(const std::filesystem::path&,bool,bool,const ProjectWriteHook&);
    [[nodiscard]] std::filesystem::path save_destination(const std::filesystem::path&,bool) const;
    void load(const std::filesystem::path& path, bool recovery);
    [[nodiscard]] std::string marker() const;
    ScenarioWorkspace& workspace_;
    ProjectInfo info_;
    ProjectPreferences preferences_;
    std::string saved_marker_, recovery_marker_;
    std::chrono::steady_clock::time_point last_recovery_{std::chrono::steady_clock::now()};
    mutable ProjectTimings timings_;
};
}
