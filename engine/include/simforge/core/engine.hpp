#pragma once

#include <simforge/core/clock.hpp>
#include <simforge/simulation/simulation_clock.hpp>

#include <cstdint>

namespace simforge
{

class Engine
{
public:
    explicit Engine(std::uint32_t ticks_per_second = 60)
        : simulation_clock_{ticks_per_second} {}

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    ~Engine();

    void initialize();
    void shutdown();

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] bool running() const noexcept;

    void request_exit() noexcept;

    [[nodiscard]] double begin_frame();
    [[nodiscard]] std::uint64_t advance_simulation(std::chrono::nanoseconds elapsed);
    void advance_simulation_ticks(std::uint64_t ticks);
    [[nodiscard]] simulation::SimulationClock& simulation_clock() noexcept;
    [[nodiscard]] const simulation::SimulationClock& simulation_clock() const noexcept;
    void end_frame() noexcept;

    [[nodiscard]] double uptime_seconds() const;
    [[nodiscard]] double last_frame_delta() const noexcept;
    [[nodiscard]] std::uint64_t frame_count() const noexcept;

private:
    bool initialized_{false};
    bool running_{false};

    core::Clock clock_{};
    simulation::SimulationClock simulation_clock_;

    double last_frame_delta_{0.0};
    std::uint64_t frame_count_{0};
};

} // namespace simforge
