#include <simforge/viewer/viewer.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>

using namespace simforge;
using Timer = std::chrono::steady_clock;
template<class F> double measure(F f, int count = 2000)
{
    const auto start = Timer::now(); for(int i = 0; i < count; ++i) f(i);
    return std::chrono::duration<double,std::micro>{Timer::now()-start}.count()/count;
}
int main(int argc, char** argv)
{
    try
    {
        if (argc != 1 && argc != 2 && !(argc == 4 && std::string_view{argv[2]} == "--write-large-scenario"))
            throw std::invalid_argument("Usage: simforge_viewer_benchmark [SOURCE_ROOT [--write-large-scenario NEW_FILE]]");
        const std::filesystem::path root = argc >= 2 ? argv[1] : ".";
        auto base = scenario::load_scenario(root/"examples/warehouse-baseline.json");
        auto large = base; large.name = "Large viewer benchmark";
        auto& c = std::get<scenario::WarehouseConfig>(large.domain); c.loading_bays = 128; c.workers = 256; c.forklifts = 64; c.shipment_interval_seconds = 1;
        std::cout << std::fixed << std::setprecision(3);
        for (const auto* definition : {&base,&large})
        {
            viewer::ViewerController v{*definition}; v.set_viewport({{244,60},{1032,530},{1,1}}); v.seek(7200ULL*definition->simulation.tick_rate);
            auto camera = v.orbit_camera();
            double matrix_sum = 0;
            const double camera_us = measure([&](int i){camera.orbit({i%2?1.0f:-1.0f,0}); matrix_sum += camera.camera().view_projection()[0][0];});
            std::uint64_t hit_sum = 0;
            const double picking_us = measure([&](int i)
            {
                const auto ray = viewer::pointer_ray(camera.camera(),v.viewport(),{300.0f+static_cast<float>(i%900),150.0f+static_cast<float>(i%300)});
                if (ray) { const auto hit = viewer::pick(*ray,v.scene().selectables()); if (hit) hit_sum += hit->id+1; }
            });
            const double dynamic_us = measure([&](int){v.advance(std::chrono::milliseconds{16}); (void)v.present();});
            std::cout << definition->name << " | objects/draws=" << v.present().objects.size() << " selectables=" << v.scene().selectables().size()
                << " | camera=" << camera_us << " us | ray+pick=" << picking_us << " us | paused animation+present=" << dynamic_us << " us | hits checksum=" << hit_sum << " matrix checksum=" << matrix_sum << '\n';
        }
        if (argc == 4) scenario::save_scenario(argv[3],large);
        return 0;
    }
    catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
