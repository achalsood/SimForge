#pragma once
#include <simforge/workspace/scenario_workspace.hpp>
#include <chrono>
#include <mutex>
#include <thread>

namespace simforge::workspace
{
enum class ExecutionState { Idle, Preparing, Running, Cancelling, Completed, Cancelled, Failed };
const char* execution_state_name(ExecutionState state) noexcept;
struct ExperimentProgress
{
    ExecutionState state{ExecutionState::Idle};
    std::uint64_t request_id{0}, completed_candidates{0}, total_candidates{0}, tick{0}, horizon{0};
    std::uint64_t current_replication{0},completed_replications{0},total_replications{0},candidates_per_replication{0};
    std::uint64_t current_candidate{0};
    std::string replication_seed;
    std::string phase, candidate, error;
    bool sensitivity{false};std::string parameter,sweep_value;
    bool cancellation_requested{false};
    double elapsed_seconds{0};
    [[nodiscard]] bool active() const noexcept
    { return state == ExecutionState::Preparing || state == ExecutionState::Running || state == ExecutionState::Cancelling; }
};
// Public operations are serialized by the owning (main) thread; progress may be read by any thread.
class ExperimentExecution
{
public:
    using Observer = std::function<void(std::string_view,std::string_view,std::uint64_t,std::uint64_t)>;
    ~ExperimentExecution();
    std::uint64_t start(ExperimentRequest request, Observer observer = {});
    void cancel();
    void shutdown();
    void reserve_request_ids(std::uint64_t last_id);
    [[nodiscard]] ExperimentProgress progress() const;
    [[nodiscard]] std::optional<ExperimentProgress> last_unsuccessful_attempt() const;
    [[nodiscard]] std::optional<Comparison> take_result(std::uint64_t request_id);
private:
    mutable std::mutex mutex_;
    ExperimentProgress progress_;
    std::optional<ExperimentProgress> last_unsuccessful_;
    std::optional<Comparison> result_;
    std::chrono::steady_clock::time_point started_{};
    std::jthread worker_;
    std::uint64_t next_id_{1};
    bool closed_{false};
};
}
