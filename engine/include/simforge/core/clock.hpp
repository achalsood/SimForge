#pragma once

#include <chrono>

namespace simforge::core
{

class Clock
{
public:
    using clock_type = std::chrono::steady_clock;
    using time_point = clock_type::time_point;

    Clock();

    void reset();

    [[nodiscard]] double tick();
    [[nodiscard]] double elapsed_seconds() const;

private:
    time_point start_time_;
    time_point last_time_;
};

} // namespace simforge::core