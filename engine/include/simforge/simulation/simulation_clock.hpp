#pragma once

#include <chrono>
#include <cstdint>

namespace simforge::simulation
{

// Scheduling is repeatable for identical inputs and control operations in the
// same execution environment. This does not make simulation state deterministic.
class SimulationClock
{
public:
    explicit SimulationClock(std::uint32_t ticks_per_second = 60);

    // Returns the number of fixed updates the caller must execute.
    [[nodiscard]] std::uint64_t advance(std::chrono::nanoseconds elapsed);
    // Explicit tick execution, independent of wall time, pause and scale.
    void advance_ticks(std::uint64_t ticks);
    void single_step();
    void pause() noexcept { paused_ = true; }
    void resume() noexcept { paused_ = false; }
    void set_time_scale(double scale);
    // Clears progress and pause; preserves rate and scale.
    void reset() noexcept;

    [[nodiscard]] bool paused() const noexcept { return paused_; }
    [[nodiscard]] double time_scale() const noexcept { return time_scale_; }
    [[nodiscard]] std::uint32_t ticks_per_second() const noexcept { return rate_; }
    [[nodiscard]] double fixed_step_seconds() const noexcept;
    [[nodiscard]] std::uint64_t tick_count() const noexcept { return ticks_; }
    [[nodiscard]] double elapsed_seconds() const noexcept;

private:
    std::uint32_t rate_;
    std::uint64_t ticks_{0};
    std::uint64_t remainder_{0};
    double time_scale_{1.0};
    bool paused_{false};
};

} // namespace simforge::simulation
