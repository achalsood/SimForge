#include <simforge/analysis/sensitivity.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>

namespace simforge::analysis {
std::string_view parameter_name(SweepParameter p) {
    switch(p) {case SweepParameter::Forklifts:return "forklifts";case SweepParameter::Workers:return "workers";
    case SweepParameter::LoadingBays:return "loading_bays";case SweepParameter::ArrivalInterval:return "shipment_interval_seconds";
    case SweepParameter::ProcessingDuration:return "processing_duration_seconds";}throw std::invalid_argument("Unsupported sweep parameter");
}
SweepParameter parse_parameter(std::string_view name) {
    for(auto p:{SweepParameter::Forklifts,SweepParameter::Workers,SweepParameter::LoadingBays,SweepParameter::ArrivalInterval,SweepParameter::ProcessingDuration})
        if(parameter_name(p)==name)return p;throw std::invalid_argument("Unsupported sweep parameter");
}
namespace {
std::uint32_t& field(scenario::WarehouseConfig& c,SweepParameter p) {
    switch(p) {case SweepParameter::Forklifts:return c.forklifts;case SweepParameter::Workers:return c.workers;
    case SweepParameter::LoadingBays:return c.loading_bays;case SweepParameter::ArrivalInterval:return c.shipment_interval_seconds;
    case SweepParameter::ProcessingDuration:return c.processing_duration_seconds;}throw std::invalid_argument("Unsupported sweep parameter");
}
}
std::uint32_t parameter_value(const scenario::ScenarioDefinition& d,SweepParameter p) {auto c=std::get<scenario::WarehouseConfig>(d.domain);return field(c,p);}
std::vector<std::uint32_t> sweep_range(std::uint32_t lo,std::uint32_t hi,std::uint32_t step) {
    if(!step || lo>hi || (std::uint64_t{hi}-lo)%step)throw std::invalid_argument("Range requires positive step, ordered bounds and an exactly reachable maximum");
    const auto n=(std::uint64_t{hi}-lo)/step+1;if(n>max_sweep_points)throw std::length_error("At most 50 sweep points");
    std::vector<std::uint32_t> values;for(std::uint64_t i=0;i<n;++i)values.push_back(static_cast<std::uint32_t>(std::uint64_t{lo}+i*step));return values;
}
scenario::ScenarioDefinition tested_definition(const SensitivityRequest& r,std::uint32_t v,std::uint64_t seed) {
    auto d=r.source;auto& c=std::get<scenario::WarehouseConfig>(d.domain);field(c,r.parameter)=v;d.simulation.random_seed=seed;scenario::validate(d);return d;
}
void validate_sensitivity_request(const SensitivityRequest& r) {
    if(r.schema_version!=1)throw std::invalid_argument("Unsupported sensitivity request version");scenario::validate(r.source);
    if(!std::holds_alternative<scenario::WarehouseConfig>(r.source.domain))throw std::invalid_argument("Warehouse source required");
    (void)parameter_name(r.parameter);(void)metric_name(r.objective);(void)replication_seeds(r.plan);
    if(!r.multi_seed && r.plan.count!=1)throw std::invalid_argument("Single-seed sweep requires exactly one seed");
    if(!std::isfinite(r.practical_threshold)||r.practical_threshold<0)throw std::invalid_argument("Threshold must be finite and nonnegative");
    if(r.values.empty()||r.values.size()>max_sweep_points)throw std::invalid_argument("Sweep requires 1..50 distinct points");
    const auto& c=std::get<scenario::WarehouseConfig>(r.source.domain);
    if(r.parameter==SweepParameter::ArrivalInterval && c.workload.arrivals!=scenario::ArrivalMode::Constant)throw std::invalid_argument("Arrival interval sweep requires constant arrivals");
    if(r.parameter==SweepParameter::ProcessingDuration && c.workload.processing!=scenario::ProcessingMode::Constant)throw std::invalid_argument("Processing duration sweep requires constant processing");
    std::set<std::uint32_t> distinct;std::uint64_t work=scenario::workload::arrival_bound(c,r.source.simulation.duration_seconds,r.source.simulation.tick_rate)*std::uint64_t{4};
    for(auto v:r.values) {if(!distinct.insert(v).second)throw std::invalid_argument("Duplicate sweep value");auto d=tested_definition(r,v,r.source.simulation.random_seed);
        work+=scenario::workload::arrival_bound(std::get<scenario::WarehouseConfig>(d.domain),d.simulation.duration_seconds,d.simulation.tick_rate);}
    if(work*r.plan.count>max_statistical_arrival_work)throw std::length_error("Sensitivity workload exceeds 20 million conservative arrival executions");
}
void aggregate_sensitivity_report(SensitivityReport& r) {
    validate_sensitivity_request(r.request);const auto seeds=replication_seeds(r.request.plan);
    if(r.replications.size()!=seeds.size())throw std::invalid_argument("Incomplete sensitivity report");
    const auto& values=r.request.values;if(!std::is_sorted(values.begin(),values.end()))throw std::invalid_argument("Completed sensitivity rows must use canonical ascending value order");
    for(std::size_t i=0;i<seeds.size();++i)if(r.replications[i].seed!=seeds[i]||r.replications[i].points.size()!=values.size())throw std::invalid_argument("Sensitivity replication identity mismatch");
    const bool varies=workload_varies(std::get<scenario::WarehouseConfig>(r.request.source.domain));
    const auto pairs=[&](auto before,auto after,std::size_t m) {
        std::vector<std::optional<double>> a,b;for(const auto& row:r.replications) {a.push_back(before(row,static_cast<StatisticalMetric>(m)));b.push_back(after(row,static_cast<StatisticalMetric>(m)));}
        return summarize_pairs(a,b,higher_is_better(static_cast<StatisticalMetric>(m)),varies);
    };
    r.points.clear();r.best_point.reset();const auto objective=static_cast<std::size_t>(r.request.objective);
    for(std::size_t m=0;m<statistical_metric_count;++m) {const auto baseline=[](const auto& row,auto metric){return metric_value(row.baseline,metric);};r.baseline_means[m]=pairs(baseline,baseline,m).baseline_mean;}
    for(std::size_t i=0;i<values.size();++i) {
        SensitivityPoint p;p.value=values[i];p.id=std::uint64_t{p.value}+1;p.baseline=p.value==parameter_value(r.request.source,r.request.parameter);
        for(std::size_t m=0;m<statistical_metric_count;++m) {
            const auto after=[i](const auto& row,auto metric){return metric_value(row.points[i],metric);};
            // X - 0 reuses the validated centered sample-variance kernel for an
            // absolute mean interval. Zero is a numerical reference, not a warehouse baseline.
            const auto absolute=pairs([](const auto&,auto)->std::optional<double>{return 0.0;},after,m);
            p.means[m]=absolute.candidate_mean;p.mean_lower[m]=absolute.lower;p.mean_upper[m]=absolute.upper;
            p.against_baseline[m]=pairs([](const auto& row,auto metric){return metric_value(row.baseline,metric);},after,m);
            if(i)p.adjacent[m]=pairs([i](const auto& row,auto metric){return metric_value(row.points[i-1],metric);},after,m);
        }
        if(i) {const auto& s=p.adjacent[objective];const double step=static_cast<double>(values[i]-values[i-1]);const double sign=higher_is_better(r.request.objective)?1:-1;
            if(s.mean_difference) {p.marginal_per_unit=*s.mean_difference/step;const double gain=sign**p.marginal_per_unit;
                if(gain<0)p.marginal_interpretation="Observed objective worsened at this adjacent point.";
                else if(gain>r.request.practical_threshold)p.marginal_interpretation="Measured improvement exceeds the practical threshold.";
                else if(varies&&(!s.lower||!s.upper))p.marginal_interpretation="Results are inconclusive at the selected replication count.";
                else if(varies&&std::max(sign**s.lower,sign**s.upper)/step>r.request.practical_threshold)p.marginal_interpretation="Small observed marginal improvement; uncertainty is inconclusive.";
                else p.marginal_interpretation="Marginal improvement is small over this tested interval.";
            }else p.marginal_interpretation="Adjacent objective is undefined; no diminishing-return conclusion.";
        }
        if(p.means[objective]) {
            auto best=std::find_if(r.points.begin(),r.points.end(),[&](const auto& x){return r.best_point&&x.id==*r.best_point;});
            if(best==r.points.end() || (higher_is_better(r.request.objective)?*p.means[objective]>*best->means[objective]:*p.means[objective]<*best->means[objective]))r.best_point=p.id;
        }r.points.push_back(std::move(p));
    }
    for(std::size_t p=0;p<3;++p)for(std::size_t m=0;m<statistical_metric_count;++m)r.probes[p][m]=pairs([](const auto& row,auto metric){return metric_value(row.baseline,metric);},[p](const auto& row,auto metric)->std::optional<double>{return row.probes[p]?metric_value(*row.probes[p],metric):std::nullopt;},m);
    r.limitations={"One-factor-at-a-time responses hold all other operational fields fixed; interactions and global optimality are not established.",
        "Absolute point-mean intervals use the shared Student-t kernel on X minus numerical zero, not a fabricated warehouse baseline. They estimate means, not the range of individual seed outcomes; they are distinct from paired source and adjacent effect intervals.",
        "Raw effects are tested minus source baseline; adjacent effects are next minus previous, paired directly within each seed.",
        "Nominal 95% paired-t intervals are not simultaneous. Selecting the best observed point introduces selection bias.",
        "Practical threshold is favorable objective units per parameter unit, a decision criterion rather than statistical proof.",
        "Utilization is diagnostic, not automatically higher-is-better. No financial cost or ROI model is provided.",
        "Aggregate reports have no average warehouse snapshot. Playback reconstructs one explicitly selected seed.",
        "Workload sweeps change external demand or processing by design; resource sweeps retain identical external workloads per seed.",
        "Started-wait metrics exclude still-waiting shipments; changes can alter the measured population."};
    if(!r.request.multi_seed||!varies)r.limitations.push_back("Single realization or deterministic workload: no stochastic uncertainty intervals.");
    if(varies&&r.request.plan.count<20)r.limitations.push_back("Small-sample Student-t mean/effect intervals can be unreliable for skewed or discrete outcomes; distinct seeds must be representative model replications.");
}
SensitivityReport run_sensitivity(const SensitivityRequest& request,const simulation::ExecutionControl* control) {
    validate_sensitivity_request(request);SensitivityReport r;r.request=request;std::sort(r.request.values.begin(),r.request.values.end());
    const auto seeds=replication_seeds(request.plan);const auto baseline_value=parameter_value(request.source,request.parameter);
    for(std::size_t s=0;s<seeds.size();++s) {
        if(control)control->update("Replication",std::to_string(seeds[s]),s+1,seeds.size());SensitivityReplication row;row.seed=seeds[s];
        const auto execute=[&](const auto& d) {warehouse::WarehouseSimulation simulation(d);simulation.run(control);++r.simulation_executions;r.processed_events+=simulation.processed_events();return simulation.metrics();};
        auto baseline=request.source;baseline.simulation.random_seed=seeds[s];if(control)control->update("Simulating baseline");row.baseline=execute(baseline);
        for(auto v:r.request.values) {
            if(control)control->update("Evaluating candidate",std::to_string(v));
            row.points.push_back(v==baseline_value?row.baseline:execute(tested_definition(request,v,seeds[s])));
            if(control)control->update("Candidate complete",std::to_string(v));
        }
        const auto& b=std::get<scenario::WarehouseConfig>(baseline.domain);
        for(std::size_t p=0;p<3;++p) {auto probe=baseline;auto& c=std::get<scenario::WarehouseConfig>(probe.domain);auto parameter=p==0?SweepParameter::LoadingBays:p==1?SweepParameter::Workers:SweepParameter::Forklifts;
            const auto capacity=parameter_value(baseline,parameter);if(std::uint64_t{b.loading_bays}+b.workers+b.forklifts>=100000)continue;field(c,parameter)=capacity+1;
            if(control)control->update("Bottleneck probes",parameter_name(parameter));
            auto existing=std::find(r.request.values.begin(),r.request.values.end(),capacity+1);
            if(parameter==request.parameter&&existing!=r.request.values.end())row.probes[p]=row.points[static_cast<std::size_t>(existing-r.request.values.begin())];else row.probes[p]=execute(probe);
        }
        r.replications.push_back(std::move(row));if(control)control->update("Replication complete",{},s+1,seeds.size());
    }
    if(control)control->update("Aggregating sensitivity results");const auto start=std::chrono::steady_clock::now();aggregate_sensitivity_report(r);
    r.aggregation_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();return r;
}
}
