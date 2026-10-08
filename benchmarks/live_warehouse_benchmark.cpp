#include <simforge/visualization/live_warehouse.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <array>

int main()
{
    using namespace simforge; using clock = std::chrono::steady_clock;
    std::cout << "Live CPU benchmark; 1000 observations/updates, means in microseconds, allocations included\n";
    for (const auto& config : {std::array{120u,10u,30u,3u,10u,2u},std::array{100000u,1u,100u,3u,10u,2u},std::array{1000u,1u,100u,64u,128u,64u}})
    {
        scenario::ScenarioDefinition definition; definition.name = "Live benchmark"; definition.simulation = {1,config[0],12345};
        definition.domain = scenario::WarehouseConfig{config[3],config[4],config[5],config[1],config[2]};
        warehouse::WarehouseSimulation runtime{definition}; runtime.run(); visualization::LiveWarehouseScene scene{definition};
        auto state = runtime.capture_observation(scene.waiting_limit(),scene.processing_limit()); scene.set_state(state,true);
        double extraction = 0, targets = 0, frame = 0;
        for (int i = 0; i < 1000; ++i)
        {
            const auto a = clock::now(); state = runtime.capture_observation(scene.waiting_limit(),scene.processing_limit()); const auto b = clock::now();
            scene.set_state(state); const auto c = clock::now(); scene.animate(std::chrono::milliseconds{17}); const auto d = clock::now();
            extraction += std::chrono::duration<double,std::micro>{b-a}.count(); targets += std::chrono::duration<double,std::micro>{c-b}.count(); frame += std::chrono::duration<double,std::micro>{d-c}.count();
        }
        const auto visibility = scene.visibility();
        std::cout << "Arrivals/completed/waiting/processing: " << state.metrics.arrived << '/' << state.metrics.completed << '/' << state.metrics.waiting << '/' << state.metrics.in_progress
                  << " | Visible waiting/processing: " << visibility.visible_waiting << '/' << visibility.visible_processing << " | Hidden waiting: " << visibility.hidden_waiting
                  << " | Objects/draws: " << scene.frame().objects.size() << " | Layout generations: " << scene.layout_generations()
                  << std::fixed << std::setprecision(3) << " | Extraction: " << extraction/1000 << " us | Dynamic construction: " << targets/1000 << " us | Frame update: " << frame/1000 << " us\n";
    }
    for (double speed : {1.0,60.0,300.0,3600.0})
    {
        scenario::ScenarioDefinition definition; definition.name = "Pacing benchmark"; definition.simulation = {60,120,0};
        definition.domain = scenario::WarehouseConfig{3,10,2,1,30};
        warehouse::WarehouseSimulation runtime{definition}; visualization::LiveWarehouseScene scene{definition}; runtime.set_time_scale(speed);
        std::uint64_t updates = 0; const auto start = clock::now();
        while (runtime.clock().tick_count() < runtime.horizon_ticks())
        {
            runtime.advance_elapsed(std::chrono::milliseconds{17}); scene.set_state(runtime.capture_observation(scene.waiting_limit(),scene.processing_limit()));
            scene.animate(std::chrono::milliseconds{17}); ++updates;
        }
        const auto us = std::chrono::duration<double,std::micro>{clock::now()-start}.count();
        std::cout << "Speed: " << speed << " | Synthetic 17ms frames: " << updates << " | Total CPU: " << us << " us | Mean loop: " << us/static_cast<double>(updates)
                  << " us | Final completed: " << runtime.metrics().completed << '\n';
    }
}
