#include <simforge/simulation/input_recording.hpp>
#include <simforge/world/world.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <source_location>
#include <string_view>

using namespace simforge;
using namespace simforge::simulation;
using Json = nlohmann::json;
namespace
{
void check(bool condition, std::source_location location = std::source_location::current())
{
    if (!condition) throw std::runtime_error("Check failed at line " + std::to_string(location.line()));
}
template<class Exception = std::invalid_argument, class Function>
void rejects(Function function, std::string_view field = {})
{
    try { function(); }
    catch (const Exception& error)
    {
        if (!field.empty() && std::string_view{error.what()}.find(field) == std::string_view::npos)
            throw std::runtime_error("Expected field " + std::string{field} + "; actual: " + error.what());
        return;
    }
    throw std::runtime_error("Expected rejection for " + std::string{field});
}
void equal(const SimulationSnapshot& a, const SimulationSnapshot& b)
{
    if (auto mismatch = first_difference(a, b)) throw std::runtime_error(*mismatch);
}
scenario::ScenarioDefinition movement()
{
    return scenario::load_scenario(std::filesystem::path{SIMFORGE_SOURCE_DIR} / "examples/movement-baseline.json");
}
void validation()
{
    const auto warehouse = scenario::load_scenario(std::filesystem::path{SIMFORGE_SOURCE_DIR} / "examples/warehouse-baseline.json");
    check(std::get<scenario::WarehouseConfig>(warehouse.domain).forklifts == 2);
    rejects([&] { (void)scenario::initial_state(warehouse); }, "warehouse");
    const auto base = Json::parse(scenario::serialize_scenario(warehouse));
    const auto bad = [&](auto mutation, std::string_view field)
    {
        auto json = base; mutation(json);
        rejects([&] { (void)scenario::parse_scenario(json.dump()); }, field);
    };
    for (const auto* key : {"schema_version", "name", "domain", "simulation", "warehouse"})
        bad([&](auto& j) { j.erase(key); }, key);
    bad([](auto& j) { j["schema_version"] = 2; }, "schema_version");
    bad([](auto& j) { j["domain"] = "unknown"; }, "domain");
    bad([](auto& j) { j["simulation"]["tick_rate"] = 0; }, "tick_rate");
    bad([](auto& j) { j["simulation"]["tick_rate"] = 1001; }, "tick_rate");
    bad([](auto& j) { j["simulation"]["duration_seconds"] = 0; }, "duration_seconds");
    bad([](auto& j) { j["simulation"]["duration_seconds"] = 604801; }, "duration_seconds");
    bad([](auto& j) { j["simulation"]["duration_seconds"] = 604800; j["simulation"]["tick_rate"] = 1000; }, "budget");
    bad([](auto& j) { j["warehouse"]["forklifts"] = -3; }, "forklifts");
    bad([](auto& j) { j["warehouse"]["workers"] = 100001; }, "workers");
    bad([](auto& j) { j["warehouse"]["forklifts"] = 100000; }, "warehouse");
    bad([](auto& j) { j["warehouse"]["shipment_interval_seconds"] = 0; }, "shipment_interval_seconds");
    bad([](auto& j) { j["warehouse"]["forklifts"] = 2.0; }, "forklifts");
    bad([](auto& j) { j["simulation"]["random_seed"] = "123"; }, "random_seed");
    bad([](auto& j) { j["simulation"]["tick_rate"] = true; }, "tick_rate");
    bad([](auto& j) { j["simulation"]["surprise"] = 1; }, "surprise");
    bad([](auto& j) { j["surprise"] = 1; }, "surprise");
    bad([](auto& j) { j["initial_world"] = {{"entities", Json::array()}}; }, "warehouse");
    rejects([] { (void)scenario::parse_scenario("{broken}"); }, "JSON");
    rejects([] { (void)scenario::parse_scenario(std::string{"{}\0trailing", 11}); }, "NUL");
    rejects([] { (void)scenario::parse_scenario("{\"a\":1,\"a\":2}"); }, "duplicate");
    rejects([] { (void)scenario::parse_scenario("{\"simulation\":{\"tick_rate\":1,\"tick_\\u0072ate\":2}}"); }, "duplicate");
    rejects([] { (void)scenario::parse_scenario("{\"name\":\"\xff\"}"); }, "JSON");
    rejects([] { (void)scenario::parse_scenario("{\"x\":1e999}"); }, "JSON");
    auto overflow = base.dump();
    const auto offset = overflow.find("12345"); overflow.replace(offset, 5, "18446744073709551616");
    rejects([&] { (void)scenario::parse_scenario(overflow); }, "random_seed");
    rejects<std::length_error>([] { (void)scenario::parse_scenario(std::string(scenario::max_scenario_bytes + 1, ' ')); });
    rejects<std::length_error>([] { (void)scenario::parse_scenario(std::string(40, '[') + "0" + std::string(40, ']')); });
    auto definition = movement();
    auto initial = scenario::initial_state(definition);
    check(initial.world.live_count == 1 && initial.clock.ticks_per_second == 60);
    auto json = Json::parse(scenario::serialize_scenario(definition));
    json["initial_world"]["entities"][0]["transform"]["position"] = {0, "bad", 0};
    rejects([&] { (void)scenario::parse_scenario(json.dump()); }, "position");
    std::get<scenario::MovementConfig>(definition.domain).entities.resize(10001);
    rejects([&] { scenario::validate(definition); }, "entities");
    definition = movement(); std::get<scenario::MovementConfig>(definition.domain).entities[0].velocity->linear.x = std::numeric_limits<double>::infinity();
    rejects([&] { scenario::validate(definition); }, "linear");
}
void canonical_and_files()
{
    const auto definition = movement();
    const auto canonical = scenario::serialize_scenario(definition);
    check(scenario::serialize_scenario(scenario::parse_scenario(canonical)) == canonical);
    auto json = Json::parse(canonical);
    json["initial_world"]["entities"][0]["transform"].erase("rotation");
    json["initial_world"]["entities"][0]["transform"].erase("scale");
    json["initial_world"]["entities"][0]["transform"]["position"][1] = -0.0;
    check(scenario::serialize_scenario(scenario::parse_scenario(json.dump(4))) == canonical);
    auto minimum = definition; std::get<scenario::MovementConfig>(minimum.domain).entities.clear();
    check(scenario::initial_state(minimum).world.live_count == 0);
    auto maximum_seed = definition; maximum_seed.simulation.random_seed = std::numeric_limits<std::uint64_t>::max();
    check(scenario::parse_scenario(scenario::serialize_scenario(maximum_seed)).simulation.random_seed == maximum_seed.simulation.random_seed);
    const auto path = std::filesystem::path{SIMFORGE_TEST_DIR} / "scenario-test-generated.json";
    std::filesystem::remove(path);
    scenario::save_scenario(path, definition);
    check(scenario::serialize_scenario(scenario::load_scenario(path)) == canonical);
    rejects<std::runtime_error>([&] { scenario::save_scenario(path, minimum); });
    check(scenario::serialize_scenario(scenario::load_scenario(path)) == canonical);
    std::filesystem::remove(path);
    rejects<std::runtime_error>([&] { (void)scenario::load_scenario(path); });
    { std::ofstream file{path}; file << std::string(scenario::max_scenario_bytes + 1, ' '); }
    rejects<std::length_error>([&] { (void)scenario::load_scenario(path); });
    std::filesystem::remove(path);
}
InputRecording make_recording(const scenario::ScenarioDefinition& definition)
{
    world::World world; SimulationClock clock;
    restore_snapshot(world, clock, scenario::initial_state(definition));
    InputRecorder recorder{definition, world, clock};
    recorder.submit(SetEntityVelocity{{0,1}, {2,0,0}});
    recorder.submit(SetEntityVelocity{{0,1}, {3,0,0}}); // Same boundary: later sequence wins.
    recorder.submit(PauseSimulation{}); recorder.submit(StepSimulation{});
    recorder.submit(ResumeSimulation{}); recorder.advance_to(120);
    check(world.get<world::Transform>(world.handle_at(0)).position.x > 5.999999999);
    return recorder.capture_recording();
}
void commands()
{
    const auto definition = movement();
    world::World world; SimulationClock clock;
    restore_snapshot(world, clock, scenario::initial_state(definition));
    InputRecorder recorder{definition, world, clock};
    const auto reject = [&](InputCommand command)
    {
        const auto before = capture_snapshot(world, clock); const auto count = recorder.command_count();
        rejects([&] { recorder.submit(command); });
        equal(before, capture_snapshot(world, clock)); check(recorder.command_count() == count);
    };
    reject({0,0,ResumeSimulation{}}); reject({0,0,StepSimulation{}});
    reject({1,0,PauseSimulation{}}); reject({0,1,PauseSimulation{}});
    reject({0,0,SetEntityVelocity{{999,1}, {1,0,0}}});
    reject({0,0,SetEntityVelocity{{0,2}, {1,0,0}}});
    reject({0,0,SetEntityVelocity{{0,0}, {1,0,0}}});
    reject({0,0,SetEntityVelocity{{0,1}, {std::numeric_limits<double>::quiet_NaN(),0,0}}});
    recorder.submit(PauseSimulation{});
    reject({0,1,PauseSimulation{}});
    rejects([&] { recorder.advance_to(1); }, "paused"); check(clock.tick_count() == 0);
    recorder.submit(StepSimulation{}); check(clock.tick_count() == 1 && clock.paused());
    recorder.submit(ResumeSimulation{}); recorder.advance_to(10);
    reject({9,3,PauseSimulation{}});
    rejects([&] { recorder.advance_to(9); }, "past");
    rejects([&] { recorder.advance_to(121); }, "budget");
    check(recorder.capture_recording().commands.size() == 3);
    // Missing component rejection, with no log or clock mutation.
    world.remove<world::Velocity>(world.handle_at(0));
    reject({10,3,SetEntityVelocity{{0,1}, {1,0,0}}});
}
void replay()
{
    const auto definition = movement();
    const auto recording = make_recording(definition);
    check(recording.commands[0].tick == recording.commands[1].tick && recording.commands[1].sequence == 1);
    const auto text = serialize_recording(recording);
    const auto decoded = parse_recording(text);
    check(serialize_recording(decoded) == text);
    world::World destination; SimulationClock clock;
    const auto old = destination.create_entity(); destination.emplace<world::Transform>(old);
    replay_recording(definition, decoded, destination, clock);
    check(!destination.valid(old)); equal(recording.expected_final, capture_snapshot(destination, clock));
    restore_snapshot(destination, clock, recording.initial_snapshot);
    replay_recording(definition, recording, destination, clock);
    equal(recording.expected_final, capture_snapshot(destination, clock));
    const auto handle = destination.handle_at(0);
    const auto unchanged = capture_snapshot(destination, clock);
    auto mismatch = definition; mismatch.name += " different";
    rejects([&] { replay_recording(mismatch, recording, destination, clock); }, "scenario_identity");
    equal(unchanged, capture_snapshot(destination, clock)); check(destination.valid(handle));
    auto divergent = recording;
    std::get<SetEntityVelocity>(divergent.commands[1].payload).linear.x = 4;
    rejects<std::runtime_error>([&] { replay_recording(definition, divergent, destination, clock); }, "position.x");
    equal(unchanged, capture_snapshot(destination, clock)); check(destination.valid(handle));
    auto missing = recording; std::get<SetEntityVelocity>(missing.commands[0].payload).target.index = 99;
    rejects([&] { replay_recording(definition, missing, destination, clock); }, "target");
    equal(unchanged, capture_snapshot(destination, clock));
    auto corrupted = Json::parse(text);
    const auto reject_json = [&](auto mutation, std::string_view field)
    {
        auto json = corrupted; mutation(json);
        rejects([&] { (void)parse_recording(json.dump()); }, field);
    };
    reject_json([](auto& j) { j["schema_version"] = 2; }, "schema_version");
    reject_json([](auto& j) { j["commands"][0]["type"] = "destroy_everything"; }, "type");
    reject_json([](auto& j) { j["commands"][0]["linear"][0] = "1"; }, "linear");
    reject_json([](auto& j) { j["commands"][0]["target"]["index"] = -1; }, "index");
    reject_json([](auto& j) { j["commands"][1]["sequence"] = 0; }, "sequence");
    reject_json([](auto& j) { j["commands"][4]["tick"] = 0; }, "tick");
    reject_json([](auto& j) { j["commands"][3]["tick"] = 1; }, "paused");
    reject_json([](auto& j) { j["commands"][0]["unexpected"] = 1; }, "unexpected");
    reject_json([](auto& j) { j["initial_snapshot"] = "deadbeef"; }, "initial_snapshot");
    reject_json([](auto& j) { j["commands"][0].erase("linear"); }, "linear");
    rejects([&] { (void)parse_recording(text.substr(0, text.size() - 1)); }, "JSON");
    rejects<std::length_error>([] { (void)parse_recording(std::string(max_recording_bytes + 1, ' ')); });
    // Nonzero checkpoint with paused clock and remainder resumes via logical IDs.
    restore_snapshot(destination, clock, scenario::initial_state(definition));
    replay_fixed_ticks(destination, clock, 10); (void)clock.advance(std::chrono::nanoseconds{1}); clock.pause();
    InputRecorder checkpoint{definition, destination, clock};
    checkpoint.submit(StepSimulation{}); checkpoint.submit(ResumeSimulation{}); checkpoint.advance_to(30);
    const auto resumed = checkpoint.capture_recording();
    world::World branch; SimulationClock branch_clock;
    replay_recording(definition, parse_recording(serialize_recording(resumed)), branch, branch_clock);
    check(resumed.initial_tick == 10 && branch_clock.capture_snapshot().remainder == clock.capture_snapshot().remainder);
    equal(resumed.expected_final, capture_snapshot(branch, branch_clock));
    const auto path = std::filesystem::path{SIMFORGE_TEST_DIR} / "recording-test-generated.json";
    std::filesystem::remove(path); save_recording(path, recording);
    check(serialize_recording(load_recording(path)) == text);
    rejects<std::runtime_error>([&] { save_recording(path, recording); }); std::filesystem::remove(path);
}
void randomized()
{
    constexpr std::uint32_t seed = 0x5F072026; std::mt19937 random{seed}; std::size_t operation = 0;
    try
    {
        auto definition = movement(); definition.simulation.duration_seconds = 100;
        world::World world; SimulationClock clock; restore_snapshot(world, clock, scenario::initial_state(definition));
        InputRecorder recorder{definition, world, clock};
        double position = 0, velocity = 1; bool paused = false;
        for (; operation < 1000; ++operation)
        {
            switch (random() % 5)
            {
            case 0:
                velocity = static_cast<double>(static_cast<int>(random() % 33) - 16) / 4;
                recorder.submit(SetEntityVelocity{{0,1}, {velocity,0,0}}); break;
            case 1:
                if (paused) { recorder.submit(ResumeSimulation{}); paused = false; }
                else { recorder.submit(PauseSimulation{}); paused = true; } break;
            case 2:
                if (paused) { recorder.submit(StepSimulation{}); position += velocity / 60; }
                else
                {
                    const auto count = random() % 5;
                    recorder.advance_to(clock.tick_count() + count);
                    for (std::uint32_t i = 0; i < count; ++i) position += velocity / 60;
                }
                break;
            case 3:
            {
                const auto before = capture_snapshot(world, clock); const auto count = recorder.command_count();
                rejects([&] { recorder.submit(SetEntityVelocity{{0,2}, {1,0,0}}); });
                equal(before, capture_snapshot(world, clock)); check(recorder.command_count() == count); break;
            }
            case 4:
            {
                const auto recording = parse_recording(serialize_recording(recorder.capture_recording()));
                world::World replay_world; SimulationClock replay_clock;
                replay_recording(definition, recording, replay_world, replay_clock);
                equal(recording.expected_final, capture_snapshot(replay_world, replay_clock)); break;
            }
            }
            check(world.get<world::Transform>(world.handle_at(0)).position.x == position);
            check(clock.paused() == paused);
        }
    }
    catch (...) { std::cerr << "Input random seed=" << seed << " operation=" << operation << '\n'; throw; }
}
}
int main(int argc, char* argv[])
{
    try
    {
        if (argc != 2) throw std::invalid_argument("Expected test case");
        const std::string_view name{argv[1]};
        if (name == "validation") validation();
        else if (name == "files") canonical_and_files();
        else if (name == "commands") commands();
        else if (name == "replay") replay();
        else if (name == "randomized") randomized();
        else throw std::invalid_argument("Unknown case");
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
