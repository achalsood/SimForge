#include <simforge/analysis/scenario_comparison.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#ifdef NDEBUG
namespace
{
using namespace simforge;
using namespace simforge::analysis;
using namespace simforge::warehouse;
using timer = std::chrono::steady_clock;
struct Sample
{
    double validation, checkpoint, branch, execute, metrics, analysis, json, csv, total, end_to_end;
    std::uint64_t checksum;
};
double ms(timer::time_point a, timer::time_point b) { return std::chrono::duration<double,std::milli>{b-a}.count(); }
Sample run(std::uint32_t count)
{
    scenario::ScenarioDefinition definition; definition.name = "Comparison benchmark"; definition.simulation = {60,10000,42};
    definition.domain = scenario::WarehouseConfig{4,8,2,1,4};
    std::vector<ScenarioCandidate> candidates;
    for (std::uint32_t i = 0; i < count; ++i)
        candidates.push_back({"Candidate "+std::to_string(i),{4+i%4,8+i%5,2+i%4}});
    const auto setup_start = timer::now();
    const auto request = prepare_request(definition,300000,Objective::MaximizeCompleted,candidates);
    const auto setup_end = timer::now();
    const auto validation_start = timer::now(); validate_request(request); const auto validation_end = timer::now();
    double branches = 0, execution = 0, metrics_cost = 0;
    std::vector<CandidateResult> measured;
    const auto baseline = execute_candidate(request.checkpoint,request.checkpoint.capacity);
    for (const auto& candidate : candidates)
    {
        const auto branch_start = timer::now(); auto runtime = WarehouseSimulation::create_from(request.checkpoint); const auto branch_end = timer::now();
        const auto execute_start = timer::now();
        for (auto resource : {ResourceType::LoadingBay,ResourceType::Worker,ResourceType::Forklift})
        {
            const auto value = capacity_for(candidate.resources,resource);
            if (value != capacity_for(request.checkpoint.capacity,resource)) runtime.set_resource_capacity(resource,value);
        }
        runtime.run(); const auto execute_end = timer::now(); const auto m = runtime.metrics(); const auto metrics_end = timer::now();
        branches += ms(branch_start,branch_end); execution += ms(execute_start,execute_end); metrics_cost += ms(execute_end,metrics_end);
        CandidateResult result; result.candidate = candidate; result.metrics = m; measured.push_back(result);
    }
    const auto analyze_start = timer::now();
    std::uint64_t delta_checksum = 0;
    for (auto& result : measured)
    {
        result.delta = compare_metrics(baseline,result.metrics,request.checkpoint.capacity,result.candidate.resources);
        delta_checksum += static_cast<std::uint64_t>(result.delta.completed);
    }
    rank_candidates(measured,request.objective);
    const auto analyze_end = timer::now();
    const auto total_start = timer::now(); const auto report = run_comparison(request); const auto total_end = timer::now();
    const auto json_start = timer::now(); const auto json = serialize_report(report); const auto json_end = timer::now();
    const auto csv_start = timer::now(); const auto csv = serialize_csv(report); const auto csv_end = timer::now();
    const auto end_to_end_start = timer::now();
    const auto second_request = prepare_request(definition,300000,Objective::MaximizeCompleted,candidates);
    const auto second_report = run_comparison(second_request);
    const auto second_json = serialize_report(second_report), second_csv = serialize_csv(second_report);
    const auto end_to_end_end = timer::now();
    // Exact independent branch measurements must match batch results. All checks,
    // checksum consumption and report replay are outside measured phases.
    for (const auto& result : measured)
    {
        const auto found = std::find_if(report.candidates.begin(),report.candidates.end(),[&](const auto& r){return r.candidate == result.candidate;});
        if (found == report.candidates.end() || found->metrics != result.metrics || found->delta != result.delta || found->rank != result.rank)
            throw std::runtime_error("Batch benchmark branch/rank mismatch");
    }
    if (replay_report(json) != report) throw std::runtime_error("Batch benchmark report replay mismatch");
    if (second_report != report || second_json != json || second_csv != csv) throw std::runtime_error("End-to-end batch benchmark mismatch");
    std::uint64_t checksum = json.size()+csv.size()+baseline.completed+delta_checksum;
    for (const auto& result : report.candidates) checksum += result.metrics.completed+result.metrics.occupied_resource_ticks;
    return {ms(validation_start,validation_end),ms(setup_start,setup_end),branches,execution,metrics_cost,
        ms(analyze_start,analyze_end),ms(json_start,json_end),ms(csv_start,csv_end),ms(total_start,total_end),ms(end_to_end_start,end_to_end_end),checksum};
}
double median(const std::array<Sample,7>& samples,double Sample::*field)
{
    std::array<double,7> values; for (std::size_t i = 0; i < values.size(); ++i) values[i] = samples[i].*field;
    std::ranges::sort(values); return values[3];
}
}
#endif
int main()
{
#ifndef NDEBUG
    std::cerr << "Comparison benchmarks require Release\n"; return 1;
#else
    try
    {
        std::cout << "Comparison benchmark: one warmup + seven trials; 10K arrivals, 60 Hz, checkpoint H/2, resources 4/8/2.\n"
                  << "Branch/execution/metric columns sum candidate phases. Derived analysis measures deltas/ranking; controlled probes and evidence included in batch total.\n"
                  << "Checkpoint includes initialization/advance/capture/preflight. JSON includes one shared binary checkpoint. No file I/O.\n"
                  << "candidates,validation_ms,checkpoint_ms,branches_ms,execution_ms,metrics_ms,derived_analysis_ms,json_ms,csv_ms,batch_ms,end_to_end_ms,batch_per_candidate_ms,checksum\n";
        for (std::uint32_t count : {3,10,50})
        {
            (void)run(count); std::array<Sample,7> samples; std::uint64_t checksum = 0;
            for (auto& sample : samples) { sample = run(count); checksum += sample.checksum; }
            std::cout << std::fixed << std::setprecision(6) << count;
            for (auto field : {&Sample::validation,&Sample::checkpoint,&Sample::branch,&Sample::execute,&Sample::metrics,&Sample::analysis,&Sample::json,&Sample::csv,&Sample::total,&Sample::end_to_end})
                std::cout << ',' << median(samples,field);
            std::cout << ',' << median(samples,&Sample::total)/count << ',' << checksum << '\n';
        }
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what()<<'\n'; return 1; }
#endif
}
