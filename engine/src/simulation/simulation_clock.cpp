#include <simforge/simulation/simulation_clock.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace simforge::simulation
{
namespace
{
constexpr std::uint64_t units_per_second = 1'000'000'000;
}

SimulationClock::SimulationClock(std::uint32_t ticks_per_second)
    : rate_{ticks_per_second}
{
    if (rate_ == 0 || rate_ > units_per_second)
    {
        throw std::invalid_argument("Tick rate must be between 1 and 1,000,000,000 Hz");
    }
}

std::uint64_t SimulationClock::advance(std::chrono::nanoseconds elapsed)
{
    if (elapsed.count() < 0)
    {
        throw std::invalid_argument("Elapsed time cannot be negative");
    }
    if (paused_ || time_scale_ == 0.0)
    {
        return 0;
    }

    // Scaling is quantized to nanoseconds per input call, with ties rounded up.
    const double scaled = std::round(static_cast<double>(elapsed.count()) * time_scale_);
    if (!std::isfinite(scaled) || scaled >= 0x1p64)
    {
        throw std::overflow_error("Scaled elapsed time is too large");
    }
    const auto nanoseconds = time_scale_ == 1.0
        ? static_cast<std::uint64_t>(elapsed.count())
        : static_cast<std::uint64_t>(scaled);
    const auto seconds = nanoseconds / units_per_second;
    const auto fraction = (nanoseconds % units_per_second) * rate_ + remainder_;
    const auto ticks = seconds * rate_ + fraction / units_per_second;
    advance_ticks(ticks); // Validate before modifying the remainder.
    remainder_ = fraction % units_per_second;
    return ticks;
}

void SimulationClock::advance_ticks(std::uint64_t ticks)
{
    if (ticks > std::numeric_limits<std::uint64_t>::max() - ticks_)
    {
        throw std::overflow_error("Simulation tick count overflow");
    }
    ticks_ += ticks;
}

void SimulationClock::single_step()
{
    if (!paused_)
    {
        throw std::logic_error("Single-step requires a paused clock");
    }
    advance_ticks(1);
}

void SimulationClock::set_time_scale(double scale)
{
    if (!std::isfinite(scale) || scale < 0.0)
    {
        throw std::invalid_argument("Time scale must be finite and nonnegative");
    }
    time_scale_ = scale;
}

void SimulationClock::reset() noexcept
{
    ticks_ = 0;
    remainder_ = 0;
    paused_ = false;
}

double SimulationClock::fixed_step_seconds() const noexcept
{
    return 1.0 / rate_;
}

double SimulationClock::elapsed_seconds() const noexcept
{
    return static_cast<double>(ticks_) / rate_;
}
} // namespace simforge::simulation
