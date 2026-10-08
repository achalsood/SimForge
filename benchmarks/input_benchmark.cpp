#include <simforge/simulation/input_recording.hpp>
#include <simforge/world/world.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#ifdef NDEBUG
namespace
{
using timer = std::chrono::steady_clock;
struct Sample { double movement, accept, replay, write, read; std::size_t bytes; double checksum; };
double ms(timer::time_point a, timer::time_point b) { return std::chrono::duration<double, std::milli>{b-a}.count(); }
Sample run(std::size_t count)
{
    using namespace simforge;
    using namespace simulation;
    scenario::ScenarioDefinition definition;
    definition.name = "Input benchmark";
    scenario::MovementConfig config;
    config.entities.resize(count, {world::Transform{}, world::Velocity{{1,0,0}}});
    definition.domain = std::move(config);
    const auto initial = scenario::initial_state(definition);
    world::World baseline, live, replay_world;
    SimulationClock baseline_clock, clock, replay_clock;
    restore_snapshot(baseline, baseline_clock, initial); restore_snapshot(live, clock, initial);
    InputRecorder recorder{definition, live, clock};
    const auto base_start = timer::now(); replay_fixed_ticks(baseline, baseline_clock, 600); const auto base_end = timer::now();
    const auto accept_start = timer::now();
    recorder.submit(PauseSimulation{}); recorder.submit(StepSimulation{}); recorder.submit(ResumeSimulation{});
    for (std::size_t i = 0; i < count; ++i)
        recorder.submit(SetEntityVelocity{{static_cast<std::uint32_t>(i), 1}, {1.0 + static_cast<double>(i % 17),0,0}});
    const auto accept_end = timer::now();
    recorder.advance_to(600);
    const auto recording = recorder.capture_recording();
    const auto replay_start = timer::now(); replay_recording(definition, recording, replay_world, replay_clock); const auto replay_end = timer::now();
    const auto write_start = timer::now(); const auto text = serialize_recording(recording); const auto write_end = timer::now();
    const auto read_start = timer::now(); const auto decoded = parse_recording(text); const auto read_end = timer::now();
    if (auto mismatch = first_difference(recording.expected_final, capture_snapshot(replay_world, replay_clock))) throw std::runtime_error(*mismatch);
    if (decoded.commands.size() != count + 3 || decoded.end_tick != 600) throw std::runtime_error("Input benchmark validation failed");
    double checksum = 0;
    replay_world.each_components<world::Transform>([&](const auto& t) { checksum += t.position.x; });
    return {ms(base_start,base_end), ms(accept_start,accept_end), ms(replay_start,replay_end), ms(write_start,write_end), ms(read_start,read_end), text.size(), checksum};
}
template<class Member> double median(const std::array<Sample,7>& samples, Member field)
{
    std::array<double,7> values;
    for (std::size_t i = 0; i < values.size(); ++i) values[i] = samples[i].*field;
    std::ranges::sort(values); return values[3];
}
}
#endif
int main()
{
#ifndef NDEBUG
    std::cerr << "Input benchmark requires Release\n"; return 1;
#else
    try
    {
        std::cout << "Input benchmark: warmup + seven measured runs; 600 movement ticks, one velocity command per entity + pause/step/resume\n"
                  << "Replay includes preflight, isolated restoration, movement, exact comparison, destination commit. Setup excluded. Units ms.\n"
                  << "entities,commands,move_only_ms,accept_ms,replay_ms,json_write_ms,json_read_ms,json_bytes,checksum\n";
        for (std::size_t count : {1000,10000})
        {
            (void)run(count); std::array<Sample,7> samples; double checksum = 0;
            for (auto& sample : samples) { sample = run(count); checksum += sample.checksum; }
            std::cout << std::fixed << std::setprecision(6) << count << ',' << count+3 << ','
                      << median(samples,&Sample::movement) << ',' << median(samples,&Sample::accept) << ',' << median(samples,&Sample::replay) << ','
                      << median(samples,&Sample::write) << ',' << median(samples,&Sample::read) << ',' << samples[0].bytes << ',' << checksum << '\n';
        }
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
#endif
}
