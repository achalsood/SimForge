#include <simforge/core/engine.hpp>
#if SIMFORGE_HAS_RENDERER
#include "graphics.hpp"
#include "viewer.hpp"
#endif
#include <simforge/simulation/movement_system.hpp>
#include <simforge/world/world.hpp>
#include <simforge/simulation/input_recording.hpp>
#include <simforge/warehouse/warehouse_simulation.hpp>
#include <simforge/warehouse/warehouse_input.hpp>
#include <simforge/analysis/scenario_comparison.hpp>
#include <simforge/analysis/statistical_experiment.hpp>
#include <simforge/analysis/sensitivity.hpp>
#include <simforge/analysis/interaction.hpp>
#include <simforge/analysis/confirmation.hpp>
#include <simforge/workspace/project.hpp>
#include <nlohmann/json.hpp>
#include <fstream>

#include <charconv>
#include <algorithm>
#include <filesystem>
#include <optional>
#include <iomanip>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <cmath>

namespace
{
struct Options
{
    bool headless{false};
    bool live{false};
    std::optional<std::filesystem::path> calibration,calibration_source,export_scenario;
    std::optional<std::string> calibration_start;
    std::optional<std::uint64_t> calibration_seed;
    bool accept_periodic{false};
    std::optional<double> speed;
    std::uint64_t entities{10'000};
    std::uint64_t ticks{600};
    std::optional<std::filesystem::path> scenario, record, replay, project, verify_project;
    std::optional<std::filesystem::path> warehouse_replay;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> compare_forklifts;
    std::optional<std::uint64_t> checkpoint_seconds;
    std::optional<std::filesystem::path> experiment, export_json, export_csv, export_seeds,export_marginal;
    std::optional<std::filesystem::path> export_interactions,export_interaction_seeds;
    std::optional<std::uint64_t> graphics_smoke;
    std::optional<std::filesystem::path> graphics_capture;
};

std::uint64_t parse_count(std::string_view value)
{
    std::uint64_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::invalid_argument("Counts must be unsigned decimal integers in range");
    return result;
}

Options parse_options(int argc, char* argv[])
{
    Options options;
    bool entities_seen = false;
    bool ticks_seen = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument{argv[i]};
        if(argument=="--calibrate-demand" || argument=="--calibration-source" || argument=="--export-scenario") {
            auto& destination=argument=="--calibrate-demand"?options.calibration:argument=="--calibration-source"?options.calibration_source:options.export_scenario;
            if(destination || ++i==argc)throw std::invalid_argument("Missing or duplicate calibration path");destination=argv[i];continue;
        }
        if(argument=="--calibration-start" && !options.calibration_start){if(++i==argc)throw std::invalid_argument("Missing calibration UTC start");options.calibration_start=argv[i];continue;}
        if(argument=="--calibration-seed" && !options.calibration_seed){if(++i==argc)throw std::invalid_argument("Missing calibration seed");options.calibration_seed=parse_count(argv[i]);continue;}
        if(argument=="--accept-periodic-approximation" && !options.accept_periodic){options.accept_periodic=true;continue;}
        if(argument=="--project" && !options.project) { if(++i==argc) throw std::invalid_argument("Missing project path"); options.project=argv[i]; }
        else if(argument=="--verify-project" && !options.verify_project) {if(++i==argc)throw std::invalid_argument("Missing verification path");options.verify_project=argv[i];}
        else if (argument == "--live" && !options.live) options.live = true;
        else if (argument == "--speed" && !options.speed)
        {
            if (++i == argc) throw std::invalid_argument("Missing --speed value");
            const std::string_view value{argv[i]}; double speed{};
            const auto parsed = std::from_chars(value.data(),value.data()+value.size(),speed);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data()+value.size() || !std::isfinite(speed) || speed < 0.01 || speed > 3600)
                throw std::invalid_argument("Playback speed must be finite and between 0.01 and 3600");
            options.speed = speed;
        }
        else if (argument == "--graphics-smoke" && !options.graphics_smoke)
        {
            if (++i == argc) throw std::invalid_argument("Missing --graphics-smoke frame count");
            options.graphics_smoke = parse_count(argv[i]);
            if (*options.graphics_smoke < 120 || *options.graphics_smoke > 10000)
                throw std::invalid_argument("Graphics smoke requires 120..10000 frames");
        }
        else if (argument == "--graphics-capture" && !options.graphics_capture)
        {
            if (++i == argc) throw std::invalid_argument("Missing graphics capture output path");
            options.graphics_capture = argv[i];
        }
        else if (argument == "--experiment" || argument == "--export-json" || argument == "--export-csv" || argument == "--export-seeds" || argument=="--export-marginal")
        {
            auto& destination = argument == "--experiment" ? options.experiment : argument == "--export-json" ? options.export_json : argument=="--export-csv"?options.export_csv:argument=="--export-seeds"?options.export_seeds:options.export_marginal;
            if (destination) throw std::invalid_argument("Duplicate experiment/export option");
            if (++i == argc) throw std::invalid_argument("Missing experiment/export path");
            destination = argv[i];
        }
        else if (argument == "--scenario" || argument == "--record" || argument == "--replay" || argument == "--warehouse-replay")
        {
            auto& destination = argument == "--scenario" ? options.scenario : argument == "--record" ? options.record :
                argument == "--replay" ? options.replay : options.warehouse_replay;
            if (destination) throw std::invalid_argument("Duplicate file option");
            if (++i == argc) throw std::invalid_argument("Missing file option value");
            destination = argv[i];
        }
        else if (argument == "--compare-forklifts" && !options.compare_forklifts)
        {
            if (++i == argc) throw std::invalid_argument("Missing --compare-forklifts value");
            const std::string_view value{argv[i]}; const auto comma = value.find(',');
            if (comma == std::string_view::npos) throw std::invalid_argument("--compare-forklifts requires A,B");
            const auto first = parse_count(value.substr(0, comma)), second = parse_count(value.substr(comma + 1));
            if (first > 100000 || second > 100000) throw std::invalid_argument("Forklift capacity exceeds limit");
            options.compare_forklifts = {static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(second)};
        }
        else if (argument == "--checkpoint-seconds" && !options.checkpoint_seconds)
        {
            if (++i == argc) throw std::invalid_argument("Missing --checkpoint-seconds value");
            options.checkpoint_seconds = parse_count(argv[i]);
        }
        else if(argument=="--export-interactions"&&!options.export_interactions) {if(++i==argc)throw std::invalid_argument("Missing interaction export path");options.export_interactions=argv[i];}
        else if(argument=="--export-interaction-seeds"&&!options.export_interaction_seeds) {if(++i==argc)throw std::invalid_argument("Missing contrast seed export path");options.export_interaction_seeds=argv[i];}
        else if (argument == "--headless" && !options.headless) options.headless = true;
        else if (argument == "--entities" && !entities_seen)
        {
            if (++i == argc) throw std::invalid_argument("Missing --entities value");
            options.entities = parse_count(argv[i]);
            entities_seen = true;
        }
        else if (argument == "--ticks" && !ticks_seen)
        {
            if (++i == argc) throw std::invalid_argument("Missing --ticks value");
            options.ticks = parse_count(argv[i]);
            ticks_seen = true;
        }
        else throw std::invalid_argument("Unknown or duplicate argument");
    }
    if(options.verify_project && (!options.headless || options.project || options.live || options.scenario || options.speed || options.record || options.replay || options.warehouse_replay || options.experiment || options.compare_forklifts || options.checkpoint_seconds || entities_seen || ticks_seen || options.graphics_smoke || options.graphics_capture || options.export_json || options.export_csv || options.export_seeds || options.export_marginal || options.export_interactions || options.export_interaction_seeds))
        throw std::invalid_argument("--verify-project requires --headless and conflicts with other workflows");
    if(options.project && (options.headless || options.live || options.scenario || options.speed || options.record || options.replay || options.warehouse_replay || options.experiment || options.compare_forklifts || options.checkpoint_seconds || entities_seen || ticks_seen))
        throw std::invalid_argument("--project requires graphical project playback and conflicts with scenario workflows");
    if(options.calibration) {
        if(!options.headless || options.live || options.speed || options.scenario || options.project || options.verify_project || options.record || options.replay || options.warehouse_replay || options.experiment || options.compare_forklifts || options.checkpoint_seconds || entities_seen || ticks_seen || options.graphics_smoke || options.graphics_capture || options.export_seeds || options.export_marginal || options.export_interactions || options.export_interaction_seeds)
            throw std::invalid_argument("--calibrate-demand requires --headless and conflicts with other workflows");
        const bool conversion=options.calibration_source || options.export_scenario || options.calibration_start || options.calibration_seed || options.accept_periodic;
        if(conversion && !(options.calibration_source && options.export_scenario && options.calibration_start && options.calibration_seed && options.accept_periodic))
            throw std::invalid_argument("Periodic scenario conversion requires source, output, UTC start, explicit seed and --accept-periodic-approximation");
        return options;
    }
    if(options.calibration_source || options.export_scenario || options.calibration_start || options.calibration_seed || options.accept_periodic)throw std::invalid_argument("Calibration conversion options require --calibrate-demand");
    if ((options.live && (!options.scenario || options.headless || options.record || options.replay || options.warehouse_replay || options.experiment ||
            options.compare_forklifts || options.checkpoint_seconds || entities_seen || ticks_seen)) || (options.speed && !options.live))
        throw std::invalid_argument("--live requires a graphical --scenario; --speed requires --live; simulation workflows conflict");
    if ((entities_seen || ticks_seen) && !options.headless)
        throw std::invalid_argument("--entities and --ticks require --headless");
    if (options.entities > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Entity count exceeds entity index space");
    if ((options.record || options.replay || options.warehouse_replay) && !options.headless)
        throw std::invalid_argument("Recording workflows require --headless");
    if ((options.scenario || options.record || options.replay || options.warehouse_replay) && (entities_seen || ticks_seen))
        throw std::invalid_argument("Scenario/recording tick budgets conflict with --entities/--ticks");
    if (options.record && (options.replay || options.warehouse_replay || !options.scenario))
        throw std::invalid_argument("--record requires --scenario and conflicts with --replay");
    if (options.warehouse_replay && options.replay) throw std::invalid_argument("Replay options conflict");
    if (options.compare_forklifts.has_value() != options.checkpoint_seconds.has_value() ||
        (options.compare_forklifts && (!options.headless || !options.scenario || options.replay || options.warehouse_replay || entities_seen || ticks_seen)))
        throw std::invalid_argument("Checkpoint comparison requires --headless --scenario, both comparison options, and no replay/size overrides");
    if ((options.experiment && (!options.headless || options.scenario || options.record || options.replay || options.warehouse_replay ||
        options.compare_forklifts || options.checkpoint_seconds || entities_seen || ticks_seen)) ||
        ((options.export_json || options.export_csv || options.export_seeds || options.export_marginal) && !options.experiment))
        throw std::invalid_argument("--experiment requires --headless and conflicts with other workflows; exports require --experiment");
    if (options.export_json && options.export_csv && std::filesystem::absolute(*options.export_json).lexically_normal() ==
        std::filesystem::absolute(*options.export_csv).lexically_normal())
        throw std::invalid_argument("JSON and CSV output paths must differ");
    if(options.export_seeds) for(const auto* output:{&options.export_json,&options.export_csv}) if(*output && std::filesystem::absolute(**output).lexically_normal()==std::filesystem::absolute(*options.export_seeds).lexically_normal()) throw std::invalid_argument("Per-seed export path must differ from JSON and summary CSV");
    if (options.graphics_smoke && (options.headless || options.record || options.replay || options.warehouse_replay ||
        options.experiment || options.compare_forklifts || options.checkpoint_seconds || options.export_json || options.export_csv || options.export_seeds || options.export_marginal || entities_seen || ticks_seen))
        throw std::invalid_argument("--graphics-smoke conflicts with headless/simulation options");
    if (options.graphics_capture && (!options.graphics_smoke || (options.graphics_capture->extension() != ".ppm" && options.graphics_capture->extension() != ".png") || std::filesystem::exists(*options.graphics_capture)))
        throw std::invalid_argument("--graphics-capture requires --graphics-smoke and a new .png or .ppm output file");
    if((options.export_interactions||options.export_interaction_seeds)&&!options.experiment)throw std::invalid_argument("Interaction exports require --experiment");
    return options;
}

int experiment_workflow(const Options& options)
{
    using namespace simforge::analysis;
    const std::array outputs{&options.export_json,&options.export_csv,&options.export_seeds,&options.export_marginal,&options.export_interactions,&options.export_interaction_seeds};
    for(std::size_t i=0;i<outputs.size();++i)if(*outputs[i]) {
        if(std::filesystem::exists(**outputs[i]))throw std::invalid_argument("Export output already exists");
        for(std::size_t j=0;j<i;++j)if(*outputs[j]&&std::filesystem::absolute(**outputs[i]).lexically_normal()==std::filesystem::absolute(**outputs[j]).lexically_normal())throw std::invalid_argument("Export paths must differ");
    }
    // Check output conflicts before simulation and write only exclusive new files.
    if(options.export_marginal&&!options.experiment)throw std::invalid_argument("Marginal CSV requires sensitivity experiment");
    if(options.export_marginal)for(const auto* other:{&options.export_json,&options.export_csv,&options.export_seeds})if(*other&&std::filesystem::absolute(**other).lexically_normal()==std::filesystem::absolute(*options.export_marginal).lexically_normal())throw std::invalid_argument("Export paths must differ");
    for (const auto* output : {&options.export_json, &options.export_csv,&options.export_seeds,&options.export_marginal})
        if (*output && std::filesystem::exists(**output)) throw std::invalid_argument("Export output already exists: " + (**output).string());
    {
        if(std::filesystem::file_size(*options.experiment)>max_experiment_bytes) throw std::length_error("Experiment specification exceeds byte budget");
        std::ifstream input(*options.experiment);nlohmann::json specification;input>>specification;
        if(specification.is_object() && specification.value("schema_version",0)==5) {
            if(options.export_marginal || options.export_interactions || options.export_interaction_seeds)throw std::invalid_argument("Confirmation supports JSON, summary CSV and per-seed CSV exports");
            const auto request=parse_confirmation_request(specification.dump());const auto report=run_confirmation(request);
            const auto& result=report.paired.candidates.front().metrics[static_cast<std::size_t>(request.objective)];
            std::cout<<"Independent-seed confirmation study | Source: "<<request.source_study_id<<" | "<<request.paired.plan.count<<" new distinct seeds | Disjointness validated\n";
            std::cout<<"Estimated mean difference: ";if(result.mean_difference)std::cout<<*result.mean_difference;else std::cout<<"undefined";
            std::cout<<" | Nominal 95% confidence interval: ";if(result.lower)std::cout<<'['<<*result.lower<<", "<<*result.upper<<']';else std::cout<<"unavailable";std::cout<<'\n';
            if(result.lower && *result.lower<=0 && *result.upper>=0)std::cout<<"The confirmation interval includes zero.\n";
            std::cout<<"Disjoint PRNG seeds do not guarantee statistical independence; confirmation does not prove optimality.\n";
            if(options.export_json) save_confirmation_report(*options.export_json,report);
            if(options.export_csv) save_confirmation_csv(*options.export_csv,report);
            if(options.export_seeds) save_confirmation_csv(*options.export_seeds,report,true);return 0;
        }
        if(specification.is_object()&&specification.contains("schema_version")&&specification.at("schema_version").is_number_integer()&&specification.at("schema_version")==4) {
            if(!specification.contains("mode")||specification.at("mode")!="interaction"){std::cerr<<"Unsupported interaction mode\n";return 3;}
            try {if(options.export_marginal)throw std::invalid_argument("Marginal CSV belongs to sensitivity experiments");
                const auto request=load_interaction_experiment(*options.experiment);const auto report=run_interaction(request);
                std::cout<<"SimForge Two-Factor Interaction | "<<parameter_name(request.x)<<" x "<<parameter_name(request.y)<<" | "<<report.cells.size()<<" cells | "<<request.plan.count<<" seeds | "<<report.simulation_executions<<" executions\n";
                for(const auto& c:report.cells){std::cout<<c.x<<','<<c.y<<" | Mean: ";if(c.means[static_cast<std::size_t>(request.objective)])std::cout<<*c.means[static_cast<std::size_t>(request.objective)];else std::cout<<"undefined";std::cout<<'\n';}
                for(const auto& r:report.interactions)std::cout<<r.x0<<".."<<r.x1<<" x "<<r.y0<<".."<<r.y1<<" | "<<r.interpretation<<'\n';
                if(options.export_json)save_interaction_report(*options.export_json,report);
                if(options.export_csv)save_interaction_csv(*options.export_csv,report);
                if(options.export_seeds)save_interaction_csv(*options.export_seeds,report,InteractionCsv::Seeds);
                if(options.export_interactions)save_interaction_csv(*options.export_interactions,report,InteractionCsv::Interactions);
                if(options.export_interaction_seeds)save_interaction_csv(*options.export_interaction_seeds,report,InteractionCsv::InteractionSeeds);return 0;
            }catch(const simforge::simulation::ExecutionCancelled& e){std::cerr<<e.what()<<'\n';return 5;}catch(const std::invalid_argument& e){std::cerr<<e.what()<<'\n';return 2;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 4;}
        }
        if(options.export_interactions||options.export_interaction_seeds)throw std::invalid_argument("Interaction exports require schema 4 interaction experiment");
        if(specification.is_object()&&specification.contains("schema_version")&&specification.at("schema_version").is_number_integer()&&specification.at("schema_version")==3) {
            if(!specification.contains("mode")||specification.at("mode")!="sensitivity") {std::cerr<<"Unsupported sensitivity mode\n";return 3;}
            try {const auto request=load_sensitivity_experiment(*options.experiment);const auto report=run_sensitivity(request);
                std::cout<<"SimForge Sensitivity | "<<parameter_name(request.parameter)<<" | "<<request.plan.count<<" seeds | "<<report.simulation_executions<<" executions\n";
                for(const auto& p:report.points) {std::cout<<p.value<<" | Mean: ";if(p.means[static_cast<std::size_t>(request.objective)])std::cout<<*p.means[static_cast<std::size_t>(request.objective)];else std::cout<<"undefined";std::cout<<" | "<<p.marginal_interpretation<<'\n';}
                if(options.export_json)save_sensitivity_report(*options.export_json,report);
                if(options.export_csv)save_sensitivity_csv(*options.export_csv,report,SensitivityCsv::Summary);
                if(options.export_seeds)save_sensitivity_csv(*options.export_seeds,report,SensitivityCsv::Seeds);
                if(options.export_marginal)save_sensitivity_csv(*options.export_marginal,report,SensitivityCsv::Marginal);return 0;
            }catch(const simforge::simulation::ExecutionCancelled& e) {std::cerr<<e.what()<<'\n';return 5;}catch(const std::invalid_argument& e) {std::cerr<<e.what()<<'\n';return 2;}catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 4;}
        }
        if(specification.is_object() && specification.contains("schema_version") && specification.at("schema_version").is_number_integer() && specification.at("schema_version")==2) {
            if(options.export_marginal)throw std::invalid_argument("Marginal CSV requires sensitivity experiment");
            if(!specification.contains("mode") || !specification.at("mode").is_string()) {std::cerr<<"Statistical mode must be a string\n";return 2;}
            if(specification.at("mode")!="paired_statistical") {std::cerr<<"Unsupported statistical mode; only paired fresh-start capacity comparisons are supported\n";return 3;}
            try {
                const auto request=load_statistical_experiment(*options.experiment);const auto report=run_statistical_experiment(request);
                std::cout<<"SimForge Paired Statistical Experiment | "<<request.plan.count<<" distinct seeds | 95% paired Student-t | Fresh starts | Objective: "<<objective_name(request.objective)<<'\n';
                for(const auto& c:report.candidates) {const auto& s=c.metrics[static_cast<std::size_t>(objective_metric(request.objective))];
                    std::cout<<c.name<<" | n="<<s.count<<" | Mean paired delta: ";if(s.mean_difference) std::cout<<*s.mean_difference;else std::cout<<"unavailable";
                    std::cout<<" | 95% interval: ";if(s.lower) std::cout<<'['<<*s.lower<<", "<<*s.upper<<']';else std::cout<<"unavailable";std::cout<<" | "<<s.diagnostic<<'\n';}
                if(options.export_json) save_statistical_report(*options.export_json,report);
                if(options.export_csv) save_statistical_csv(*options.export_csv,report);
                if(options.export_seeds) save_statistical_csv(*options.export_seeds,report,true);
                return 0;
            }catch(const simforge::simulation::ExecutionCancelled& e) {std::cerr<<e.what()<<'\n';return 5;}catch(const std::invalid_argument& e) {std::cerr<<e.what()<<'\n';return 2;}catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 4;}
        }
    }
    if(options.export_marginal)throw std::invalid_argument("Marginal CSV requires sensitivity experiment");
    if(options.export_seeds) throw std::invalid_argument("Per-seed CSV requires a statistical experiment");
    const auto report = run_comparison(load_experiment(*options.experiment));
    std::cout << "SimForge Batch Comparison | Objective: " << objective_name(report.request.objective)
              << " | Checkpoint tick: " << report.request.checkpoint.current_tick
              << " | Horizon: " << report.request.checkpoint.horizon_ticks << '\n'
              << "Candidate | Bays/Workers/Forklifts | Completed | Waiting | Throughput/hour | Mean wait seconds | Bay/Worker/Forklift utilization (%) | Rank\n";
    const auto number = [](std::optional<double> value, double scale = 1)
    { if (value) std::cout << std::fixed << std::setprecision(3) << *value * scale; else std::cout << "unavailable"; };
    const auto print = [&](const CandidateResult& result)
    {
        const auto& c = result.candidate.resources; const auto& m = result.metrics;
        std::cout << std::quoted(result.candidate.name) << " | " << c.loading_bays << '/' << c.workers << '/' << c.forklifts
                  << " | " << m.completed << " | " << m.waiting << " | ";
        number(m.throughput_per_hour); std::cout << " | "; number(m.average_wait_seconds); std::cout << " | ";
        number(m.loading_bay_utilization, 100); std::cout << '/'; number(m.worker_utilization, 100); std::cout << '/'; number(m.forklift_utilization, 100);
        std::cout << " | "; if (result.rank) std::cout << *result.rank; else std::cout << "reference/unavailable"; std::cout << '\n';
    };
    print(report.baseline); for (const auto& result : report.candidates) print(result);
    for (const auto& evidence : report.bottlenecks)
    {
        std::cout << evidence.summary << '\n';
        if (evidence.conclusion == BottleneckConclusion::LikelyBottleneck)
            std::cout << simforge::warehouse::resource_name(evidence.resource) << ": likely bottleneck under the tested model and objective.\n";
    }
    for (const auto& warning : report.warnings) std::cout << "Analysis: " << warning << '\n';
    std::cout << "Rankings compare tested candidates; costs and real-world calibration are not modeled.\n";
    if (options.export_json) { save_report(*options.export_json, report); std::cout << "JSON exported: " << options.export_json->string() << '\n'; }
    if (options.export_csv) { save_csv(*options.export_csv, report); std::cout << "CSV exported: " << options.export_csv->string() << '\n'; }
    return 0;
}

void print_warehouse_outcome(const char* label, const simforge::warehouse::WarehouseSimulation& runtime)
{
    const auto metrics = runtime.metrics();
    std::cout << std::fixed << std::setprecision(3) << label << " | Forklifts: " << runtime.resource_capacity().forklifts
              << " | Arrivals: " << metrics.arrived << " | Started: " << metrics.started << " | Completed: " << metrics.completed
              << " | Waiting: " << metrics.waiting << " | In progress: " << metrics.in_progress << '\n';
    const auto print = [](const char* name, std::optional<double> value, double factor)
    {
        std::cout << name << ": "; if (value) std::cout << *value * factor; else std::cout << "unavailable";
        std::cout << '\n';
    };
    print("Throughput (shipments/hour)", metrics.throughput_per_hour, 1);
    print("Average started wait (minutes)", metrics.average_wait_seconds, 1.0 / 60);
    print("Maximum started wait (minutes)", metrics.maximum_wait_seconds, 1.0 / 60);
    std::cout << "Completed processing (seconds): " << metrics.completed_processing_seconds << '\n';
    print("Loading bay utilization (%)", metrics.loading_bay_utilization, 100);
    print("Worker utilization (%)", metrics.worker_utilization, 100);
    print("Forklift utilization over capacity history (%)", metrics.forklift_utilization, 100);
}

int checkpoint_workflow(const Options& options, const simforge::scenario::ScenarioDefinition& definition)
{
    using namespace simforge::warehouse;
    const auto& config = std::get<simforge::scenario::WarehouseConfig>(definition.domain);
    if (options.compare_forklifts->first != config.forklifts)
        throw std::invalid_argument("First comparison capacity must retain the scenario's baseline forklifts");
    if (*options.checkpoint_seconds > definition.simulation.duration_seconds)
        throw std::invalid_argument("Checkpoint exceeds scenario horizon");
    WarehouseSimulation baseline{definition};
    baseline.advance_to(*options.checkpoint_seconds * definition.simulation.tick_rate);
    const auto checkpoint = baseline.capture_snapshot();
    std::cout << "Warehouse checkpoint at " << *options.checkpoint_seconds << " simulated seconds; capacity adjustment occurs after due events here.\n";
    print_warehouse_outcome("Shared checkpoint", baseline);
    auto branch_a = WarehouseSimulation::create_from(checkpoint);
    auto branch_b = WarehouseSimulation::create_from(checkpoint);
    WarehouseInputRecorder recorder{branch_b}; recorder.set_forklift_capacity(options.compare_forklifts->second);
    branch_a.run(); recorder.advance_to(branch_b.horizon_ticks());
    const auto recording = recorder.capture_recording();
    auto replay = WarehouseSimulation::create_from(deserialize_snapshot(serialize_snapshot(checkpoint)));
    replay_recording(recording, replay);
    auto continuation = WarehouseSimulation::create_from(deserialize_snapshot(serialize_snapshot(checkpoint)));
    continuation.run();
    if (auto difference = first_difference(branch_a.capture_snapshot(), continuation.capture_snapshot()))
        throw std::runtime_error(*difference);
    if (options.record) save_recording(*options.record, recording);
    print_warehouse_outcome("Branch A", branch_a); print_warehouse_outcome("Branch B", branch_b);
    const auto a = branch_a.metrics(), b = branch_b.metrics();
    std::cout << "Branch B minus A | Completed: " << static_cast<std::int64_t>(b.completed) - static_cast<std::int64_t>(a.completed)
              << " | Waiting: " << static_cast<std::int64_t>(b.waiting) - static_cast<std::int64_t>(a.waiting)
              << " | Throughput (shipments/hour): " << *b.throughput_per_hour - *a.throughput_per_hour
              << " | Completed processing (seconds): " << b.completed_processing_seconds - a.completed_processing_seconds << '\n'
              << "Serialized checkpoint continuation and capacity-command replay verified exactly.\n"
              << "Outcomes describe the implemented warehouse model, not calibrated real-world predictions.\n";
    return 0;
}

int warehouse_replay_workflow(const Options& options)
{
    using namespace simforge;
    const auto recording = warehouse::load_recording(*options.warehouse_replay);
    if (options.scenario && scenario::serialize_scenario(scenario::load_scenario(*options.scenario)) != recording.scenario_identity)
        throw std::invalid_argument("Warehouse replay scenario identity mismatch");
    auto runtime = warehouse::WarehouseSimulation::create_from(recording.initial_snapshot);
    warehouse::replay_recording(recording, runtime);
    print_warehouse_outcome("Warehouse replay", runtime);
    std::cout << "Replay verified against recorded final warehouse state.\n";
    return 0;
}

int scenario_workflow(const Options& options)
{
    using namespace simforge;
    std::optional<simulation::InputRecording> recording;
    if (options.replay) recording = simulation::load_recording(*options.replay);
    const auto definition = options.scenario ? scenario::load_scenario(*options.scenario)
        : scenario::parse_scenario(recording->scenario_identity);
    std::cout << "Validated scenario: " << definition.name << '\n';
    if (std::holds_alternative<scenario::WarehouseConfig>(definition.domain))
    {
        if (options.compare_forklifts) return checkpoint_workflow(options, definition);
        if (options.record || options.replay) throw std::invalid_argument("Warehouse recording requires checkpoint comparison; replay uses --warehouse-replay");
        using timer = std::chrono::steady_clock;
        const auto begin = timer::now();
        warehouse::WarehouseSimulation simulation{definition};
        const auto initialized = timer::now();
        simulation.run();
        const auto executed = timer::now();
        const auto metrics = simulation.metrics();
        const auto finalized = timer::now();
        const auto& config = std::get<scenario::WarehouseConfig>(definition.domain);
        std::cout << std::fixed << std::setprecision(3)
                  << "SimForge Warehouse Simulation\n"
                  << "Tick rate: " << definition.simulation.tick_rate << " Hz\n"
                  << "Duration: " << definition.simulation.duration_seconds << " seconds ("
                  << static_cast<double>(definition.simulation.duration_seconds) / 3600 << " simulated hours)\n"
                  << "Loading bays: " << config.loading_bays << " | Workers: " << config.workers << " | Forklifts: " << config.forklifts << '\n'
                  << (scenario::workload::advanced(config) ? std::string{} : "Arrival interval: "+std::to_string(config.shipment_interval_seconds)+" seconds | Processing: "+std::to_string(config.processing_duration_seconds)+" seconds\n")
                  << "Arrivals: " << metrics.arrived << " | Started: " << metrics.started << " | Completed: " << metrics.completed
                  << " | Waiting: " << metrics.waiting << " | In progress: " << metrics.in_progress << '\n';
        if(scenario::workload::advanced(config)) std::cout<<"Workload: arrivals="<<scenario::workload::arrival_name(config.workload.arrivals)
            <<" | processing="<<scenario::workload::processing_name(config.workload.processing)<<" | seed="<<definition.simulation.random_seed
            <<" | generator="<<scenario::workload_generator_version<<(scenario::workload::stochastic(config)?" | single-seed realization":"")<<'\n';
        const auto print_metric = [](const char* label, std::optional<double> value, double factor, const char* unit)
        {
            std::cout << label << ": ";
            if (value) std::cout << *value * factor << ' ' << unit;
            else std::cout << "unavailable";
            std::cout << '\n';
        };
        print_metric("Throughput", metrics.throughput_per_hour, 1, "shipments/hour");
        print_metric("Average queue wait (started)", metrics.average_wait_seconds, 1.0 / 60, "minutes");
        print_metric("Maximum queue wait (started)", metrics.maximum_wait_seconds, 1.0 / 60, "minutes");
        print_metric("Average processing (completed)", metrics.average_processing_seconds, 1, "seconds");
        std::cout << "Total processing (completed): " << metrics.completed_processing_seconds << " seconds\n";
        print_metric("Loading bay utilization", metrics.loading_bay_utilization, 100, "%");
        print_metric("Worker utilization", metrics.worker_utilization, 100, "%");
        print_metric("Forklift utilization", metrics.forklift_utilization, 100, "%");
        std::cout << "Initialization: " << std::chrono::duration<double, std::milli>{initialized - begin}.count() << " ms\n"
                  << "Execution: " << std::chrono::duration<double, std::milli>{executed - initialized}.count() << " ms\n"
                  << "Metric finalization: " << std::chrono::duration<double, std::milli>{finalized - executed}.count() << " ms\n";
        return 0;
    }
    if (options.compare_forklifts) throw std::invalid_argument("Checkpoint forklift comparison requires a warehouse scenario");
    world::World world;
    simulation::SimulationClock clock;
    if (recording) simulation::replay_recording(definition, *recording, world, clock);
    else
    {
        simulation::restore_snapshot(world, clock, scenario::initial_state(definition));
        if (options.record)
        {
            const auto initial = world.capture_snapshot();
            if (initial.velocities.empty()) throw std::invalid_argument("Recording demonstration requires at least one Velocity component");
            const auto target = initial.velocities.front().entity;
            simulation::InputRecorder recorder{definition, world, clock};
            recorder.submit(simulation::SetEntityVelocity{target, {2, 0.5, -0.25}});
            recorder.submit(simulation::PauseSimulation{});
            recorder.submit(simulation::StepSimulation{});
            recorder.submit(simulation::ResumeSimulation{});
            recorder.advance_to(std::max<std::uint64_t>(1, scenario::total_ticks(definition) / 2));
            recorder.submit(simulation::SetEntityVelocity{target, {-1, 0, 0}});
            recorder.advance_to(scenario::total_ticks(definition));
            recording = recorder.capture_recording();
            // Verify outcomes before writing the recording.
            world::World replay_world; simulation::SimulationClock replay_clock;
            simulation::replay_recording(definition, *recording, replay_world, replay_clock);
            simulation::save_recording(*options.record, *recording);
            std::cout << "Recorded " << recording->commands.size() << " accepted commands; replay verified.\n";
        }
        else simulation::replay_fixed_ticks(world, clock, scenario::total_ticks(definition));
    }
    if (options.replay) std::cout << "Replay verified against recorded final state.\n";
    double checksum = 0;
    world.each_components<world::Transform>([&](const auto& transform) { checksum += transform.position.x; });
    std::cout << "Executable domain: movement\nEntities: " << world.entity_count() << " | Ticks: " << clock.tick_count()
              << " | simulated seconds: " << clock.elapsed_seconds() << "\nPosition checksum: " << checksum << '\n';
    return 0;
}

void populate(simforge::world::World& world, std::uint64_t count)
{
    world.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i)
    {
        const auto entity = world.create_entity();
        world.emplace<simforge::world::Transform>(entity);
        world.emplace<simforge::world::Velocity>(entity, simforge::world::Vec3{1.0, 0.5, -0.25});
    }
}

int run(const Options& options)
{
    if(options.calibration) {
        namespace demand=simforge::analysis::demand;
        const std::array outputs{&options.export_json,&options.export_csv,&options.export_scenario};
        for(std::size_t i=0;i<outputs.size();++i)if(*outputs[i]) {
            if(std::filesystem::exists(**outputs[i]))throw std::invalid_argument("Calibration output already exists");
            for(std::size_t j=0;j<i;++j)if(*outputs[j] && std::filesystem::absolute(**outputs[i]).lexically_normal()==std::filesystem::absolute(**outputs[j]).lexically_normal())throw std::invalid_argument("Calibration outputs must differ");
        }
        auto report=demand::calibrate(demand::load_request(*options.calibration));
        if(options.export_scenario) {
            auto converted=demand::convert(*report,simforge::scenario::load_scenario(*options.calibration_source),"Calibrated periodic demand",*options.calibration_seed,demand::parse_timestamp(*options.calibration_start),options.accept_periodic);
            auto evidence=std::make_shared<demand::Report>(*report);evidence->conversions.push_back(converted);report=std::move(evidence);
            simforge::scenario::save_scenario(*options.export_scenario,converted.scenario);
            std::cout<<converted.semantics<<'\n';
            for(std::size_t i=0;i<converted.fitted_per_second.size();++i)std::cout<<"Rate bin "<<i<<" fitted="<<converted.fitted_per_second[i]<<" represented="<<converted.represented_per_second[i]<<" arrivals/second\n";
        }
        if(options.export_json)demand::save_export(*options.export_json,demand::report_json(*report));
        if(options.export_csv)demand::save_export(*options.export_csv,demand::report_csv(*report));
        std::cout<<"SimForge Demand Calibration | "<<report->id<<" | dataset="<<report->provenance.dataset_id<<'\n'
            <<"Training: "<<report->training.observed<<" arrivals / "<<report->training.exposure_seconds<<" seconds | Holdout actual="<<report->holdout.observed<<" expected="<<report->holdout.expected<<" signed_error="<<report->holdout.signed_error<<'\n'
            <<"Import_ms="<<report->timings.import_ms<<" fit_ms="<<report->timings.fit_ms<<" validation_ms="<<report->timings.validation_ms<<'\n';
        for(const auto& warning:report->warnings)std::cout<<warning<<'\n';return 0;
    }
    if(options.verify_project){simforge::workspace::ScenarioWorkspace workspace;simforge::workspace::Project project(workspace);project.open(*options.verify_project);
        std::cout<<"SimForge project verified: "<<project.info().name<<" | studies="<<workspace.studies().size()<<" | bytes="<<project.timings().bytes<<" | open_ms="<<project.timings().total_ms<<'\n';return 0;}
    if (options.experiment) return experiment_workflow(options);
    if (options.warehouse_replay) return warehouse_replay_workflow(options);
    if ((options.headless && options.scenario) || options.replay) return scenario_workflow(options);
    if (!options.headless)
    {
#if SIMFORGE_HAS_RENDERER
        if(options.project) return run_viewer(options.graphics_smoke,options.graphics_capture,{},300,false,nullptr,options.project);
        return run_graphics(options.graphics_smoke,options.graphics_capture,options.scenario,options.live,options.speed.value_or(300));
#else
        throw std::invalid_argument("This executable has no renderer; use --headless or build with SIMFORGE_BUILD_RENDERER=ON");
#endif
    }
    using timer = std::chrono::steady_clock;
    simforge::Engine engine;
    engine.initialize();
    simforge::world::World world;
    const auto setup_start = timer::now();
    populate(world, options.entities);
    const auto setup_end = timer::now();
    const auto update_start = timer::now();
    for (std::uint64_t tick = 0; tick < options.ticks; ++tick)
    {
        simforge::simulation::MovementSystem::update(world, engine.simulation_clock().fixed_step_seconds());
        engine.advance_simulation_ticks(1);
    }
    const auto update_end = timer::now();
    double checksum = 0.0;
    world.each<simforge::world::Transform>([&](simforge::world::Entity, const auto& transform)
    {
        checksum += transform.position.x;
    });
    std::cout << "Entities: " << world.entity_count()
              << " | Ticks: " << engine.simulation_clock().tick_count()
              << " | simulated seconds: " << engine.simulation_clock().elapsed_seconds()
              << "\nSetup wall seconds: " << std::chrono::duration<double>{setup_end - setup_start}.count()
              << "\nSimulation update wall seconds: " << std::chrono::duration<double>{update_end - update_start}.count()
              << "\nPosition checksum: " << checksum << '\n';
    engine.shutdown();
    return 0;
}
} // namespace

int main(int argc, char* argv[])
{
    try { return run(parse_options(argc, argv)); }
    catch (const std::exception& error)
    {
        std::cerr << "SimForge sandbox: " << error.what()
                  << "\nUsage: simforge_sandbox [--headless [--entities N] [--ticks N]]\n"
                  << "  --headless --scenario FILE [--record OUTPUT]\n"
                  << "  --headless --replay FILE [--scenario FILE]\n";
        std::cerr << "  --headless --scenario FILE --compare-forklifts A,B --checkpoint-seconds S [--record OUTPUT]\n"
                  << "  --headless --warehouse-replay FILE [--scenario FILE]\n"
                  << "  --headless --experiment FILE [--export-json NEW_FILE] [--export-csv NEW_FILE] [--export-seeds NEW_FILE]\n"
                  << "    Sensitivity: [--export-marginal NEW_FILE] | Interaction: [--export-interactions NEW_FILE] [--export-interaction-seeds NEW_FILE]\n"
                  << "  --graphics-smoke FRAMES (120..10000; display/GPU/validation required)\n";
        std::cerr << "  --project PROJECT.simforge (restore saved workspace)\n";
        std::cerr << "  --headless --calibrate-demand REQUEST.json [--export-json NEW_FILE] [--export-csv NEW_FILE]\n"
                  << "    [--calibration-source SCENARIO --export-scenario NEW_FILE --calibration-start UTC_TIMESTAMP --calibration-seed N --accept-periodic-approximation]\n";
        std::cerr << "  --headless --verify-project PROJECT.simforge (bounded validation; no experiment reruns)\n";
        std::cerr << "  --scenario WAREHOUSE_FILE [--graphics-smoke FRAMES] [--graphics-capture NEW_FILE.png]\n";
        std::cerr << "  --scenario WAREHOUSE_FILE --live [--speed 0.01..3600] (default 300; interactive dashboard)\n";
        return 1;
    }
}
