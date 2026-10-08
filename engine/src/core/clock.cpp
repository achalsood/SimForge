#include <simforge/core/clock.hpp>

namespace simforge::core
{

Clock::Clock()
{
    reset();
}

void Clock::reset()
{
    start_time_ = clock_type::now();
    last_time_ = start_time_;
}

double Clock::tick()
{
    const auto current_time = clock_type::now();

    const std::chrono::duration<double> delta =
        current_time - last_time_;

    last_time_ = current_time;

    return delta.count();
}

double Clock::elapsed_seconds() const
{
    const std::chrono::duration<double> elapsed =
        clock_type::now() - start_time_;

    return elapsed.count();
}

} // namespace simforge::core