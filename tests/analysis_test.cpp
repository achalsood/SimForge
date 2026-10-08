#include <simforge/analysis/scenario_comparison.hpp>
#include <simforge/warehouse/warehouse_input.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <source_location>
#include <sstream>

using namespace simforge;
using namespace simforge::warehouse;
using namespace simforge::analysis;
namespace
{
void check(bool value, std::source_location location = std::source_location::current())
{ if (!value) throw std::runtime_error("Analysis check failed at line " + std::to_string(location.line())); }
template<class Function> void rejects(Function&& function, std::source_location location = std::source_location::current())
{
    bool failed = false; try { function(); } catch (const std::exception&) { failed = true; } check(failed, location);
}
void near(double a, double b) { check(std::isfinite(a) && std::abs(a - b) < 1e-10); }
scenario::ScenarioDefinition definition(ResourceCounts counts = {3,3,1}, std::uint32_t duration = 12,
    std::uint32_t processing = 4, std::uint32_t rate = 1)
{
    scenario::ScenarioDefinition result; result.name = "Analysis correctness"; result.simulation = {rate,duration,42};
    result.domain = scenario::WarehouseConfig{counts.loading_bays,counts.workers,counts.forklifts,1,processing}; return result;
}
ComparisonRequest request(ResourceCounts counts = {3,3,1}, std::uint64_t tick = 3,
    Objective objective = Objective::MaximizeCompleted)
{
    return prepare_request(definition(counts), tick, objective, {{"Same", counts}, {"All three", {3,3,3}}});
}
std::string read(const std::filesystem::path& path)
{
    std::ifstream file{path,std::ios::binary}; if (!file) throw std::runtime_error("Cannot read test input");
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}
void resources()
{
    for (auto resource : {ResourceType::LoadingBay,ResourceType::Worker,ResourceType::Forklift})
    {
        auto counts = ResourceCounts{3,3,3}; set_capacity(counts,resource,1);
        WarehouseSimulation simulation{definition(counts)}; simulation.advance_to(3); const auto checkpoint = simulation.capture_snapshot();
        const auto old_job = simulation.shipments()[0];
        WarehouseInputRecorder recorder{simulation}; recorder.set_resource_capacity(resource,3);
        check(simulation.resource_usage() == ResourceCounts{3,3,3}); check(simulation.shipments()[0] == old_job);
        check(simulation.shipments()[1].start_tick == 3 && simulation.shipments()[2].start_tick == 3);
        const auto changed = simulation.capture_snapshot();
        rejects([&] { recorder.set_resource_capacity(resource,2); }); check(changed == simulation.capture_snapshot());
        rejects([&] { simulation.set_resource_capacity(static_cast<ResourceType>(255),3); }); check(changed == simulation.capture_snapshot());
        simulation.advance_to(12); const auto recording = recorder.capture_recording();
        check(recording.schema_version == 2 && simulation.metrics().completed == 7 && simulation.metrics().occupied_resource_ticks == 30);
        const auto snapshot = deserialize_snapshot(serialize_snapshot(simulation.capture_snapshot()));
        check(snapshot == simulation.capture_snapshot());
        const auto available = resource == ResourceType::LoadingBay ? snapshot.totals.available_bay_ticks :
            resource == ResourceType::Worker ? snapshot.totals.available_worker_ticks : snapshot.totals.available_forklift_ticks;
        check(available == 30); validate_snapshot(snapshot);
        auto replay = WarehouseSimulation::create_from(checkpoint); replay_recording(parse_recording(serialize_recording(recording)),replay);
        check(replay.capture_snapshot() == snapshot);
        simulation.set_resource_capacity(resource,2); validate_snapshot(simulation.capture_snapshot());
        check(simulation.shipments()[7].start_tick == 11);
        if (resource != ResourceType::Forklift)
        { check(snapshot.version == 2); auto bad = snapshot; bad.version = 1; rejects([&] { validate_snapshot(bad); }); }
    }
    WarehouseSimulation same_tick{definition({1,1,1})}; same_tick.advance_to(3); const auto initial = same_tick.capture_snapshot();
    WarehouseInputRecorder commands{same_tick};
    commands.set_resource_capacity(ResourceType::Worker,3); commands.set_resource_capacity(ResourceType::LoadingBay,3);
    check(same_tick.metrics().started == 1); commands.set_resource_capacity(ResourceType::Forklift,3); check(same_tick.metrics().started == 3);
    const auto changed = same_tick.capture_snapshot();
    check(changed.capacity_changes[0].started_before == 1 && changed.capacity_changes[1].started_before == 1 && changed.capacity_changes[2].started_before == 1);
    rejects([&] { commands.set_resource_capacity(ResourceType::LoadingBay,2); }); check(changed == same_tick.capture_snapshot());
    const auto log = commands.capture_recording(); auto replay = WarehouseSimulation::create_from(initial); replay_recording(log,replay);
    check(replay.capture_snapshot() == changed);
    auto malformed = log; malformed.commands[0].resource = static_cast<ResourceType>(255); const auto before = replay.capture_snapshot();
    rejects([&] { replay_recording(malformed,replay); }); check(before == replay.capture_snapshot());
    auto json = nlohmann::json::parse(serialize_recording(log)); json["commands"][0]["capacity"] = -1;
    rejects([&] { (void)parse_recording(json.dump()); });
    json = nlohmann::json::parse(serialize_recording(log)); json["commands"][0]["resource"] = "unknown";
    rejects([&] { (void)parse_recording(json.dump()); });
    json = nlohmann::json::parse(serialize_recording(log)); json["commands"][0]["capacity"] = 100001;
    rejects([&] { (void)parse_recording(json.dump()); });
    rejects([&] { commands.submit({13,3,WarehouseCommandType::SetResourceCapacity,3,ResourceType::Worker}); });
}
void compatibility()
{
    const auto text = read(std::filesystem::path{SIMFORGE_SOURCE_DIR} / "tests/fixtures/warehouse-v1-inputs.json");
    const auto recording = parse_recording(text); check(recording.schema_version == 1);
    check(serialize_recording(recording) == text); // Real commit-9 data, including exact binary bytes.
    auto runtime = WarehouseSimulation::create_from(recording.initial_snapshot); replay_recording(recording,runtime);
    check(runtime.capture_snapshot() == recording.expected_final);
    runtime.set_resource_capacity(ResourceType::Worker,2); const auto migrated = runtime.capture_snapshot();
    check(migrated.version == 2); check(deserialize_snapshot(serialize_snapshot(migrated)) == migrated);
    auto restored = WarehouseSimulation::create_from(migrated); check(restored.capture_snapshot() == migrated);
    auto bad = migrated; bad.version = 255; rejects([&] { restored.restore_snapshot(bad); }); check(restored.capture_snapshot() == migrated);
    bad = migrated; bad.capacity_changes.back().resource = static_cast<ResourceType>(255);
    rejects([&] { restored.restore_snapshot(bad); }); check(restored.capture_snapshot() == migrated);
    bad = migrated; ++bad.totals.available_worker_ticks; rejects([&] { restored.restore_snapshot(bad); });
    check(restored.capture_snapshot() == migrated);
}
void validation()
{
    const auto valid = request(); const auto unchanged = valid.checkpoint;
    const auto mutation = [&](auto modify)
    { auto bad = valid; modify(bad); rejects([&] { (void)run_comparison(bad); }); check(valid.checkpoint == unchanged); };
    mutation([](auto& r) { r.schema_version = 2; }); mutation([](auto& r) { r.candidates.clear(); });
    mutation([](auto& r) { r.candidates.resize(51); }); mutation([](auto& r) { r.candidates[1].name = r.candidates[0].name; });
    mutation([](auto& r) { r.candidates[0].name = "Baseline"; }); mutation([](auto& r) { r.candidates[0].name = ""; });
    mutation([](auto& r) { r.candidates[0].name = std::string(257,'a'); }); mutation([](auto& r) { r.candidates[0].name = std::string(1,static_cast<char>(255)); });
    mutation([](auto& r) { r.objective = static_cast<Objective>(255); }); mutation([](auto& r) { ++r.checkpoint.current_tick; });
    mutation([](auto& r) { r.scenario_identity += " "; }); mutation([](auto& r) { r.candidates[0].resources.workers = 0; });
    mutation([](auto& r) { r.candidates[0].resources.loading_bays = std::numeric_limits<std::uint32_t>::max(); });
    WarehouseSimulation fresh{definition()}; auto bad = valid; bad.checkpoint = fresh.capture_snapshot();
    rejects([&] { validate_request(bad); });
    rejects([&] { (void)prepare_request(definition(),13,Objective::MaximizeCompleted,valid.candidates); });
    rejects([&] { (void)prepare_request(definition(),std::numeric_limits<std::uint64_t>::max(),Objective::MaximizeCompleted,valid.candidates); });
    auto movement = definition(); movement.domain = scenario::MovementConfig{};
    rejects([&] { (void)prepare_request(movement,0,Objective::MaximizeCompleted,valid.candidates); });
}
void experiment_json()
{
    const auto path = std::filesystem::path{SIMFORGE_SOURCE_DIR} / "examples/warehouse-comparison.json";
    const auto text = read(path); const auto experiment = parse_experiment(text);
    check(parse_experiment(serialize_experiment(experiment)).candidates == experiment.candidates);
    const auto loaded = load_experiment(path); check(loaded.checkpoint.current_tick == 432000 && loaded.candidates.size() == 3);
    auto json = nlohmann::json::parse(text);
    const auto mutation = [&](auto modify)
    { auto bad = json; modify(bad); rejects([&] { (void)parse_experiment(bad.dump()); }); };
    mutation([](auto& j) { j["extra"] = 1; }); mutation([](auto& j) { j["schema_version"] = 2; });
    mutation([](auto& j) { j["checkpoint_seconds"] = -1; }); mutation([](auto& j) { j["checkpoint_seconds"] = 1.5; });
    mutation([](auto& j) { j["checkpoint_seconds"] = std::numeric_limits<std::uint64_t>::max(); });
    mutation([](auto& j) { j["objective"] = "global_optimal"; }); mutation([](auto& j) { j["candidates"] = "bad"; });
    mutation([](auto& j) { j["candidates"] = nlohmann::json::array(); });
    mutation([](auto& j) { j["candidates"][0]["workers"] = -1; }); mutation([](auto& j) { j["candidates"][0]["forklifts"] = 1.5; });
    mutation([](auto& j) { j["candidates"][0]["extra"] = true; });
    for (auto name : {"../warehouse.json", "C:/other.json", "/root.json", "https://example.org/scenario.json", "file.csv", ""})
        mutation([&](auto& j) { j["scenario"] = name; });
    rejects([&] { (void)parse_experiment("{\"schema_version\":1,\"schema_version\":1}"); });
    rejects([&] { (void)parse_experiment("{"); }); rejects([&] { (void)parse_experiment(std::string(max_experiment_bytes + 1,' ')); });
    rejects([&] { (void)load_experiment(std::filesystem::path{SIMFORGE_TEST_DIR} / "absent.json"); });
    const auto directory = std::filesystem::path{SIMFORGE_TEST_DIR};
    std::filesystem::remove(directory / "analysis-scenario.json");
    scenario::save_scenario(directory / "analysis-scenario.json",definition());
    auto test = experiment; test.scenario = "analysis-scenario.json"; test.checkpoint_seconds = 13;
    { std::ofstream file{directory / "analysis-experiment.json"}; file << serialize_experiment(test); }
    rejects([&] { (void)load_experiment(directory / "analysis-experiment.json"); });
    test.scenario = "absent.json"; test.checkpoint_seconds = 0;
    { std::ofstream file{directory / "analysis-experiment.json"}; file << serialize_experiment(test); }
    rejects([&] { (void)load_experiment(directory / "analysis-experiment.json"); });
    std::filesystem::remove(directory / "analysis-scenario.json"); std::filesystem::remove(directory / "analysis-experiment.json");
}
void comparison()
{
    const auto input = request(); const auto checkpoint = input.checkpoint; const auto report = run_comparison(input);
    check(report.baseline.metrics.completed == 3 && report.baseline.metrics.waiting == 9);
    check(report.candidates[0].candidate.name == "All three" && report.candidates[0].metrics.completed == 7);
    check(report.candidates[1].metrics == report.baseline.metrics);
    check(input.checkpoint == checkpoint); check(run_comparison(input) == report);
    auto reversed = input; std::reverse(reversed.candidates.begin(),reversed.candidates.end()); check(run_comparison(reversed) == report);
    auto branch = WarehouseSimulation::create_from(checkpoint); branch.run(); check(branch.metrics() == report.baseline.metrics);
    auto altered = request({1,3,3}); altered.candidates = {{"Multiple",{3,4,4}}, {"Same",{1,3,3}}};
    const auto multi = run_comparison(altered); check(multi.candidates[0].metrics.completed == 7);
    // Reductions before increases avoid temporary combined-resource violations.
    auto large = request({1,99997,1}); large.candidates = {{"Trade capacity",{3,99995,1}}};
    const auto trade = run_comparison(large);
    check(trade.candidates[0].metrics.completed == trade.baseline.metrics.completed &&
        trade.candidates[0].metrics.occupied_resource_ticks == trade.baseline.metrics.occupied_resource_ticks &&
        trade.candidates[0].metrics.total_wait_ticks == trade.baseline.metrics.total_wait_ticks);
    auto serialized = input; serialized.checkpoint = deserialize_snapshot(serialize_snapshot(input.checkpoint)); check(run_comparison(serialized) == report);
    auto unknown = input; unknown.candidates.push_back({"Invalid",{0,0,0}}); rejects([&] { (void)run_comparison(unknown); });
    check(run_comparison(input) == report);
}
void deltas()
{
    const auto report = run_comparison(request()); const auto& best = report.candidates[0];
    check(best.delta.completed == 4 && best.delta.waiting == -6); near(*best.delta.completed_percent,400.0/3);
    near(*best.delta.throughput_per_hour,1200); check(best.delta.resources.additions == 2 && best.delta.resources.forklifts == 2);
    const auto initial = prepare_request(definition({0,3,3}),0,Objective::MaximizeCompleted,{{"Add bay",{1,3,3}}});
    const auto zero = run_comparison(initial); check(zero.baseline.metrics.completed == 0 && !zero.candidates[0].delta.completed_percent);
    check(!zero.candidates[0].delta.average_wait_seconds && !zero.candidates[0].delta.average_wait_percent);
    const auto no_wait = prepare_request(definition({3,3,3},6,1),0,Objective::MaximizeCompleted,{{"Same",{3,3,3}}});
    const auto constant = run_comparison(no_wait); check(constant.baseline.metrics.average_wait_seconds == 0 && !constant.candidates[0].delta.average_wait_percent);
    auto bad = report.candidates[0].metrics; bad.elapsed_ticks = 0;
    rejects([&] { (void)compare_metrics(report.baseline.metrics,bad,{3,3,1},{3,3,3}); });
    bad = report.baseline.metrics; bad.completed = std::numeric_limits<std::uint64_t>::max();
    rejects([&] { (void)compare_metrics(bad,bad,{3,3,1},{3,3,1}); });
    bad = report.baseline.metrics; bad.average_wait_seconds = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { (void)compare_metrics(bad,bad,{3,3,1},{3,3,1}); });
}
void ranking()
{
    for (auto objective : {Objective::MaximizeCompleted,Objective::MinimizeAverageWait,Objective::MinimizeWaiting})
    {
        auto input = request({3,3,1},3,objective);
        input.candidates = {{"Extra workers",{3,4,1}},{"Same Z",{3,3,1}},{"Same A",{3,3,1}},{"Improved",{3,3,3}}};
        const auto report = run_comparison(input); check(report.ranking == std::vector<std::string>{"Improved","Same A","Same Z","Extra workers"});
        check(!report.baseline.rank && report.baseline.candidate.name == "Baseline");
    }
    auto zero = prepare_request(definition({0,0,0}),0,Objective::MinimizeAverageWait,{{"No starts",{0,0,0}},{"Can start",{1,1,1}}});
    const auto report = run_comparison(zero); check(report.ranking == std::vector<std::string>{"Can start"});
    check(!report.baseline.eligible && !report.candidates[1].eligible && !report.candidates[1].rank);
    // Equal additions: prefer fewer absolute changes, then name.
    auto tie = request({3,3,1}); tie.candidates = {{"Keep",{3,3,1}},{"Reduce bay",{2,3,1}}};
    check(run_comparison(tie).ranking == std::vector<std::string>{"Keep","Reduce bay"});
}
void bottlenecks()
{
    for (auto limiting : {ResourceType::LoadingBay,ResourceType::Worker,ResourceType::Forklift})
    {
        ResourceCounts counts{3,3,3}; set_capacity(counts,limiting,1);
        const auto report = run_comparison(request(counts));
        for (const auto& evidence : report.bottlenecks)
        {
            check(evidence.result.has_value());
            check((evidence.conclusion == BottleneckConclusion::LikelyBottleneck) == (evidence.resource == limiting));
            check(evidence.result->delta.completed == (evidence.resource == limiting ? 2 : 0));
        }
    }
    const auto joint = run_comparison(request({1,1,1}));
    check(std::none_of(joint.bottlenecks.begin(),joint.bottlenecks.end(),[](const auto& b){return b.conclusion == BottleneckConclusion::LikelyBottleneck;}));
    check(joint.candidates[0].metrics.completed > joint.baseline.metrics.completed && !joint.warnings.empty());
    const auto sufficient = run_comparison(prepare_request(definition({3,3,3},8,1),0,Objective::MaximizeCompleted,{{"Same",{3,3,3}}}));
    for (const auto& b : sufficient.bottlenecks) check(b.result->delta.completed == 0);
    const auto short_run = run_comparison(prepare_request(definition({3,3,1},3,10),1,Objective::MaximizeCompleted,{{"Add forklift",{3,3,2}}}));
    for (const auto& b : short_run.bottlenecks) check(b.conclusion == BottleneckConclusion::NoPositiveEffect);
    const auto censored = run_comparison(prepare_request(definition(),11,Objective::MinimizeAverageWait,{{"Add forklift",{3,3,2}}}));
    near(*censored.baseline.metrics.average_wait_seconds,3);
    near(*censored.candidates[0].metrics.average_wait_seconds,4.25);
    check(censored.bottlenecks[2].conclusion == BottleneckConclusion::NoPositiveEffect);
    check(censored.candidates[0].delta.waiting == -1 && censored.candidates[0].delta.average_wait_seconds == 1.25);
    const auto horizon = run_comparison(request({3,3,1},12));
    for (const auto& b : horizon.bottlenecks) check(b.result->delta.completed == 0);
    const auto limited = run_comparison(request({1,99998,1}));
    for (const auto& b : limited.bottlenecks) check(b.conclusion == BottleneckConclusion::Unavailable && !b.result);
    const auto missing = run_comparison(prepare_request(definition({0,0,0}),0,Objective::MinimizeAverageWait,{{"Same",{0,0,0}}}));
    for (const auto& b : missing.bottlenecks) check(b.conclusion == BottleneckConclusion::Inconclusive);
}
std::vector<std::vector<std::string>> csv_parse(const std::string& text)
{
    std::vector<std::vector<std::string>> rows; std::vector<std::string> row; std::string field; bool quoted = false;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const auto c = text[i];
        if (c == '"')
        { if (quoted && i+1 < text.size() && text[i+1] == '"') { field += '"'; ++i; } else quoted = !quoted; }
        else if (c == ',' && !quoted) { row.push_back(field); field.clear(); }
        else if (c == '\r' && !quoted && i+1 < text.size() && text[i+1] == '\n')
        { row.push_back(field); rows.push_back(row); field.clear(); row.clear(); ++i; }
        else field += c;
    }
    check(!quoted && row.empty() && field.empty()); return rows;
}
void export_results()
{
    auto input = request(); input.candidates[0].name = "Quote, \"name\" \xC3\xA9";
    const auto report = run_comparison(input); const auto json = serialize_report(report); const auto parsed = nlohmann::json::parse(json);
    check(json == serialize_report(report) && replay_report(json) == report);
    check(parsed["baseline"]["metrics"]["average_wait_seconds"].is_number()); check(parsed["baseline"]["rank"].is_null());
    check(parsed["units"]["wait_time"] == "seconds");
    const auto csv = serialize_csv(report); const auto rows = csv_parse(csv);
    check(rows.size() == 4 && rows[0].size() == 29);
    for (const auto& row : rows) check(row.size() == rows[0].size());
    check(rows[3][0] == input.candidates[0].name); check(csv == serialize_csv(report));
    auto damaged = parsed; damaged["baseline"]["metrics"]["completed"] = 99;
    rejects([&] { (void)replay_report(damaged.dump()); });
    damaged = parsed; damaged["schema_version"] = 2; rejects([&] { (void)replay_report(damaged.dump()); });
    damaged = parsed; damaged["request"]["checkpoint"] = "ff"; rejects([&] { (void)replay_report(damaged.dump()); });
    auto invalid = report; invalid.candidates[0].metrics.average_wait_seconds = std::numeric_limits<double>::infinity();
    rejects([&] { (void)serialize_report(invalid); }); rejects([&] { (void)serialize_csv(invalid); });
    invalid = report; invalid.candidates[0].rank = 50; rejects([&] { (void)serialize_report(invalid); });
    const auto directory = std::filesystem::path{SIMFORGE_TEST_DIR}; const auto jfile = directory / "analysis-report.json", cfile = directory / "analysis-report.csv";
    std::filesystem::remove(jfile); std::filesystem::remove(cfile);
    save_report(jfile,report); save_csv(cfile,report); check(read(jfile) == json && read(cfile) == csv);
    rejects([&] { save_report(jfile,report); }); check(read(jfile) == json);
    rejects([&] { save_csv(cfile,report); }); check(read(cfile) == csv);
    rejects([&] { save_report(directory / "absent-directory" / "file.json",report); });
    rejects([&] { save_csv(directory,report); }); rejects([&] { save_report({},report); });
    const auto utf8_file = directory / std::filesystem::path{u8"analysis-\u00e9.json"};
    std::filesystem::remove(utf8_file); save_report(utf8_file,report); check(read(utf8_file) == json); std::filesystem::remove(utf8_file);
    auto bad_name = (directory / "invalid-output").native(); bad_name.push_back(0); bad_name += std::filesystem::path{".json"}.native();
    rejects([&] { save_report(std::filesystem::path{bad_name},report); });
    std::filesystem::remove(jfile); std::filesystem::remove(cfile);
    const auto zero = run_comparison(prepare_request(definition({0,0,0}),0,Objective::MinimizeAverageWait,{{"Same",{0,0,0}}}));
    check(nlohmann::json::parse(serialize_report(zero))["candidates"][0]["metrics"]["average_wait_seconds"].is_null());
    check(csv_parse(serialize_csv(zero))[2][11].empty());
}
void randomized()
{
    constexpr std::uint64_t seed = 0x5F102026; std::mt19937_64 random{seed};
    for (unsigned iteration = 0; iteration < 100; ++iteration)
    {
        const ResourceCounts counts{static_cast<std::uint32_t>(random()%4),static_cast<std::uint32_t>(random()%4),static_cast<std::uint32_t>(random()%4)};
        const auto config = definition(counts,10+static_cast<std::uint32_t>(random()%20),1+static_cast<std::uint32_t>(random()%7),7);
        const auto tick = random() % (scenario::total_ticks(config)+1);
        WarehouseSimulation runtime{config}; runtime.advance_to(tick); const auto snapshot = runtime.capture_snapshot();
        ComparisonRequest input{1,scenario::serialize_scenario(config),snapshot,static_cast<Objective>(random()%3),{}};
        for (unsigned candidate = 0; candidate < 1+random()%6; ++candidate)
        {
            ResourceCounts c{}; for (auto resource : {ResourceType::LoadingBay,ResourceType::Worker,ResourceType::Forklift})
            { const auto usage = capacity_for(snapshot.usage,resource); set_capacity(c,resource,usage+static_cast<std::uint32_t>(random()%(5-usage))); }
            input.candidates.push_back({"Candidate "+std::to_string(candidate),c});
        }
        try
        {
            const auto report = run_comparison(input); auto shuffled = input; std::shuffle(shuffled.candidates.begin(),shuffled.candidates.end(),random);
            check(run_comparison(shuffled) == report && runtime.capture_snapshot() == snapshot);
            for (const auto& result : report.candidates)
            {
                check(result.metrics.arrived == result.metrics.waiting+result.metrics.in_progress+result.metrics.completed);
                auto branch = WarehouseSimulation::create_from(snapshot); WarehouseInputRecorder recorder{branch};
                for (bool reductions : {true,false}) for (auto resource : {ResourceType::LoadingBay,ResourceType::Worker,ResourceType::Forklift})
                {
                    const auto before = capacity_for(snapshot.capacity,resource), after = capacity_for(result.candidate.resources,resource);
                    if (after != before && ((after < before) == reductions)) recorder.set_resource_capacity(resource,after);
                }
                recorder.advance_to(branch.horizon_ticks()); check(branch.metrics() == result.metrics);
                const auto log = recorder.capture_recording(); auto replay = WarehouseSimulation::create_from(snapshot); replay_recording(log,replay);
                check(replay.capture_snapshot() == branch.capture_snapshot());
            }
            if (iteration % 10 == 0) check(replay_report(serialize_report(report)) == report);
        }
        catch (...)
        { std::cerr << "seed="<<seed<<" iteration="<<iteration<<" checkpoint="<<tick<<" scenario="<<scenario::serialize_scenario(config)<<'\n'; throw; }
    }
}
}
int main(int argc,char* argv[])
{
    try
    {
        if (argc != 2) throw std::invalid_argument("Analysis test case required"); const std::string_view name{argv[1]};
        if (name == "resources") resources(); else if (name == "compatibility") compatibility(); else if (name == "validation") validation();
        else if (name == "experiment_json") experiment_json(); else if (name == "comparison") comparison(); else if (name == "deltas") deltas();
        else if (name == "ranking") ranking(); else if (name == "bottlenecks") bottlenecks(); else if (name == "export") export_results();
        else if (name == "randomized") randomized(); else throw std::invalid_argument("Unknown analysis case"); return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what()<<'\n'; return 1; }
}
