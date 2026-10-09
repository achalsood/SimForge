#include <simforge/workspace/experiment_execution.hpp>
#include <limits>

namespace simforge::workspace
{
const char* execution_state_name(ExecutionState state) noexcept
{
    switch (state) {
    case ExecutionState::Idle: return "Idle";
    case ExecutionState::Preparing: return "Preparing";
    case ExecutionState::Running: return "Running";
    case ExecutionState::Cancelling: return "Cancelling";
    case ExecutionState::Completed: return "Completed";
    case ExecutionState::Cancelled: return "Cancelled";
    case ExecutionState::Failed: return "Failed";
    }
    return "Unknown";
}
ExperimentExecution::~ExperimentExecution() { shutdown(); }
void ExperimentExecution::reserve_request_ids(std::uint64_t last_id)
{
    if(progress().active() || last_id==std::numeric_limits<std::uint64_t>::max()) throw std::invalid_argument("Invalid saved experiment identity");
    next_id_=std::max(next_id_,last_id+1);
}
std::uint64_t ExperimentExecution::start(ExperimentRequest request, Observer observer)
{
    if (closed_) throw std::logic_error("Experiment service is shut down");
    if (progress().active()) throw std::logic_error("An experiment is already active");
    validate_experiment_request(request);
    if (next_id_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Request identities exhausted");
    if (worker_.joinable()) worker_.join();
    request.id = next_id_++;
    const auto id = request.id;
    {
        std::lock_guard lock{mutex_};
        result_.reset(); progress_ = {}; progress_.request_id = id;
        progress_.total_candidates = request.candidates.size(); progress_.state = ExecutionState::Preparing;
        progress_.phase = "Preparing"; started_ = std::chrono::steady_clock::now();
    }
    try {
        worker_ = std::jthread{[this, request = std::move(request), observer = std::move(observer)](std::stop_token stop) {
            try {
                simulation::ExecutionControl control{stop,[&](auto phase, auto name, auto tick, auto horizon) {
                    // Test/benchmark observer is never called with the publication lock held.
                    {
                        std::lock_guard lock{mutex_};
                        if (progress_.state != ExecutionState::Cancelling) progress_.state = ExecutionState::Running;
                        if (phase == "Candidate complete") ++progress_.completed_candidates;
                        if (phase != "Simulating") { progress_.phase = phase; progress_.tick = 0; progress_.horizon = 0; }
                        if (!name.empty() && phase != "Bottleneck probes") progress_.candidate = name;
                        if (phase == "Simulating") { progress_.tick = tick; progress_.horizon = horizon; }
                    }
                    if (observer) observer(phase,name,tick,horizon);
                }};
                auto report = execute_experiment(request,&control);
                control.update("Publishing");
                std::lock_guard lock{mutex_};
                // Cancellation and publication use the same lock. First committed terminal outcome wins.
                if (progress_.cancellation_requested) progress_.state = ExecutionState::Cancelled;
                else { result_ = std::move(report); progress_.state = ExecutionState::Completed; }
                progress_.phase = execution_state_name(progress_.state);
                progress_.elapsed_seconds = std::chrono::duration<double>{std::chrono::steady_clock::now()-started_}.count();
                if (progress_.state == ExecutionState::Cancelled) last_unsuccessful_ = progress_;
            } catch (const simulation::ExecutionCancelled&) {
                std::lock_guard lock{mutex_}; progress_.state = ExecutionState::Cancelled; progress_.phase = "Cancelled";
                progress_.elapsed_seconds = std::chrono::duration<double>{std::chrono::steady_clock::now()-started_}.count();
                last_unsuccessful_ = progress_;
            } catch (const std::exception& e) {
                std::lock_guard lock{mutex_}; progress_.state = ExecutionState::Failed; progress_.phase = "Failed"; progress_.error = e.what();
                progress_.elapsed_seconds = std::chrono::duration<double>{std::chrono::steady_clock::now()-started_}.count();
                last_unsuccessful_ = progress_;
            } catch (...) {
                std::lock_guard lock{mutex_}; progress_.state = ExecutionState::Failed; progress_.phase = "Failed"; progress_.error = "Unknown experiment execution failure";
                progress_.elapsed_seconds = std::chrono::duration<double>{std::chrono::steady_clock::now()-started_}.count();
                last_unsuccessful_ = progress_;
            }
        }};
    } catch (const std::exception& e) {
        std::lock_guard lock{mutex_}; progress_.state = ExecutionState::Failed; progress_.error = e.what(); progress_.phase = "Worker startup failed";
        progress_.elapsed_seconds = std::chrono::duration<double>{std::chrono::steady_clock::now()-started_}.count();
        last_unsuccessful_ = progress_;
        throw;
    }
    return id;
}
void ExperimentExecution::cancel()
{
    std::lock_guard lock{mutex_};
    if (!progress_.active()) return;
    progress_.cancellation_requested = true; progress_.state = ExecutionState::Cancelling;
    worker_.request_stop();
}
void ExperimentExecution::shutdown()
{ closed_ = true; cancel(); if (worker_.joinable()) worker_.join(); }
ExperimentProgress ExperimentExecution::progress() const
{
    std::lock_guard lock{mutex_}; auto snapshot = progress_;
    if (snapshot.active()) snapshot.elapsed_seconds = std::chrono::duration<double>{std::chrono::steady_clock::now()-started_}.count();
    return snapshot;
}
std::optional<Comparison> ExperimentExecution::take_result(std::uint64_t id)
{
    std::lock_guard lock{mutex_};
    if (progress_.state != ExecutionState::Completed || progress_.request_id != id || !result_) return {};
    auto report = std::move(result_); result_.reset(); return report;
}
std::optional<ExperimentProgress> ExperimentExecution::last_unsuccessful_attempt() const
{ std::lock_guard lock{mutex_}; return last_unsuccessful_; }
}
