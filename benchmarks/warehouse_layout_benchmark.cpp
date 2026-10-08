#include <simforge/visualization/warehouse_layout.hpp>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <array>

int main()
{
    using namespace simforge; using clock = std::chrono::steady_clock;
    std::cout << "Scenario-to-layout CPU benchmark; 100 repetitions, mean microseconds; allocation included\n";
    for (auto capacity : {std::array{3u,10u,2u},std::array{32u,100u,32u},std::array{256u,256u,256u}})
    {
        scenario::ScenarioDefinition input; input.name = "Layout benchmark";
        input.domain = scenario::WarehouseConfig{capacity[0],capacity[1],capacity[2],1,10};
        double layout_us = 0, frame_us = 0; std::size_t objects = 0, bytes = 0;
        for (int repeat = 0; repeat < 100; ++repeat)
        {
            const auto start = clock::now(); const auto layout = visualization::build_warehouse_layout(input); const auto middle = clock::now();
            const auto frame = visualization::make_warehouse_frame(layout); const auto end = clock::now();
            layout_us += std::chrono::duration<double,std::micro>{middle-start}.count();
            frame_us += std::chrono::duration<double,std::micro>{end-middle}.count();
            objects = layout.objects.size(); bytes = sizeof(layout)+sizeof(frame)+layout.objects.capacity()*sizeof(visualization::WarehouseVisual)+
                layout.zones.capacity()*sizeof(visualization::WarehouseZone)+frame.objects.capacity()*sizeof(renderer::RenderObject);
        }
        std::cout << "Bays/workers/forklifts: " << capacity[0] << '/' << capacity[1] << '/' << capacity[2]
                  << " | Logical/render objects: " << objects << " | Approx retained CPU bytes (layout+frame): " << bytes
                  << std::fixed << std::setprecision(3) << " | Layout: " << layout_us/100 << " us | Owned frame: " << frame_us/100 << " us\n";
    }
}
