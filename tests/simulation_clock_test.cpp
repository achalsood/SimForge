#include <simforge/core/engine.hpp>
#include <simforge/simulation/simulation_clock.hpp>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <source_location>
#include <string>

using simforge::simulation::SimulationClock;
using namespace std::chrono_literals;
void check(bool value, std::source_location location = std::source_location::current())
{
    if (!value) throw std::runtime_error("Check failed at line " + std::to_string(location.line()));
}
template<class Exception, class Function>
void rejects(Function function)
{
    try { function(); } catch (const Exception&) { return; }
    throw std::runtime_error("Expected exception");
}
int main()
{
    try
    {
        SimulationClock clock;
        check(clock.ticks_per_second() == 60 && clock.tick_count() == 0);
        check(clock.elapsed_seconds() == 0 && clock.fixed_step_seconds() == 1.0 / 60);
        check(clock.advance(0ns) == 0);
        check(clock.advance(16'666'666ns) == 0);
        check(clock.advance(1ns) == 1);
        check(clock.advance(983'333'333ns) == 59);
        check(clock.elapsed_seconds() == 1);
        clock.reset();
        check(clock.advance(10ms) == 0);
        clock.pause();
        check(clock.advance(10s) == 0);
        clock.single_step();
        check(clock.tick_count() == 1);
        clock.resume();
        check(clock.advance(10ms) == 1); // Preserved fractional tick.
        rejects<std::logic_error>([&] { clock.single_step(); });
        clock.reset();
        clock.set_time_scale(0.5);
        check(clock.advance(1s) == 30);
        clock.set_time_scale(2);
        check(clock.advance(1s) == 120);
        clock.set_time_scale(0);
        check(clock.advance(10s) == 0);
        clock.pause();
        clock.single_step(); // Step ignores zero scale.
        check(clock.tick_count() == 151);
        clock.reset();
        check(!clock.paused() && clock.tick_count() == 0 && clock.time_scale() == 0);
        SimulationClock custom{120};
        check(custom.advance(1s) == 120 && custom.elapsed_seconds() == 1);
        rejects<std::invalid_argument>([] { SimulationClock invalid{0}; });
        rejects<std::invalid_argument>([] { SimulationClock invalid{1'000'000'001}; });
        rejects<std::invalid_argument>([&] { (void)clock.advance(-1ns); });
        for (double invalid : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
            rejects<std::invalid_argument>([&] { clock.set_time_scale(invalid); });
        check(clock.time_scale() == 0);
        clock.set_time_scale(std::numeric_limits<double>::max());
        rejects<std::overflow_error>([&] { (void)clock.advance(1s); });
        check(clock.tick_count() == 0);
        clock.advance_ticks(std::numeric_limits<std::uint64_t>::max());
        rejects<std::overflow_error>([&] { clock.advance_ticks(1); });
        clock.pause();
        rejects<std::overflow_error>([&] { clock.single_step(); });
        check(clock.tick_count() == std::numeric_limits<std::uint64_t>::max());
        SimulationClock overflow;
        check(overflow.advance(10ms) == 0);
        overflow.advance_ticks(std::numeric_limits<std::uint64_t>::max());
        rejects<std::overflow_error>([&] { (void)overflow.advance(10ms); });
        check(overflow.tick_count() == std::numeric_limits<std::uint64_t>::max());
        check(overflow.advance(1ns) == 0); // Overflow preserved the remainder.
        overflow.reset();
        check(overflow.advance(10ms) == 0); // Reset cleared the remainder.
        SimulationClock first, second, total;
        std::chrono::nanoseconds sum{0};
        for (int i = 0; i < 10000; ++i)
        {
            const auto input = std::chrono::nanoseconds{(i * 7919) % 30'000'000};
            sum += input;
            check(first.advance(input) == second.advance(input));
        }
        (void)total.advance(sum);
        check(first.tick_count() == second.tick_count() && first.elapsed_seconds() == second.elapsed_seconds());
        check(total.tick_count() == first.tick_count());
        first.set_time_scale(0.123);
        second.set_time_scale(0.123);
        for (int i = 0; i < 1000; ++i)
            check(first.advance(123456ns) == second.advance(123456ns));
        check(first.elapsed_seconds() == second.elapsed_seconds());
        SimulationClock rounding{1'000'000'000};
        rounding.set_time_scale(0.5);
        check(rounding.advance(1ns) == 1);
        SimulationClock fastest{1'000'000'000};
        check(fastest.advance(1ns) == 1);
        SimulationClock large{1};
        check(large.advance(std::chrono::nanoseconds::max()) == 9'223'372'036);
        simforge::Engine engine{120};
        rejects<std::logic_error>([&] { (void)engine.advance_simulation(1s); });
        rejects<std::logic_error>([&] { engine.advance_simulation_ticks(1); });
        engine.initialize();
        check(engine.advance_simulation(1s) == 120);
        check(engine.frame_count() == 0 && engine.last_frame_delta() == 0);
        (void)engine.begin_frame();
        engine.end_frame();
        check(engine.simulation_clock().tick_count() == 120);
        engine.advance_simulation_ticks(120);
        check(engine.simulation_clock().elapsed_seconds() == 2);
        engine.shutdown();
        rejects<std::logic_error>([&] { (void)engine.advance_simulation(1s); });
        engine.initialize();
        check(engine.simulation_clock().tick_count() == 0 && engine.frame_count() == 0);
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
