#include <simforge/analysis/statistical_experiment.hpp>
#include <algorithm>
#include <set>

namespace simforge::analysis {
bool workload_varies(const scenario::WarehouseConfig& c) noexcept {
    return (c.workload.arrivals==scenario::ArrivalMode::Uniform && c.workload.arrival_min_seconds<c.workload.arrival_max_seconds) ||
        (c.workload.processing==scenario::ProcessingMode::Uniform && c.workload.processing_min_seconds<c.workload.processing_max_seconds);
}
namespace {
warehouse::ResourceCounts counts(const scenario::ScenarioDefinition& d) {const auto& c=std::get<scenario::WarehouseConfig>(d.domain);return {c.loading_bays,c.workers,c.forklifts};}
}
void validate_statistical_request(const StatisticalRequest& r) {
    if(r.schema_version!=1) throw std::invalid_argument("Unsupported statistical request version");
    scenario::validate(r.baseline);(void)objective_name(r.objective);(void)replication_seeds(r.plan);
    if(!std::holds_alternative<scenario::WarehouseConfig>(r.baseline.domain)) throw std::invalid_argument("Statistical experiments require a warehouse");
    if(r.candidates.empty() || r.candidates.size()>max_statistical_candidates) throw std::invalid_argument("Statistical experiments require 1..20 candidates");
    std::set<std::string> names;std::set<std::uint64_t> ids;
    const auto& b=std::get<scenario::WarehouseConfig>(r.baseline.domain);
    for(const auto& candidate:r.candidates) {
        scenario::validate(candidate.definition);auto normalized=candidate.definition;
        auto* c=std::get_if<scenario::WarehouseConfig>(&normalized.domain);if(!c) throw std::invalid_argument("Warehouse candidate required");
        normalized.name=r.baseline.name;normalized.simulation.random_seed=r.baseline.simulation.random_seed;
        c->loading_bays=b.loading_bays;c->workers=b.workers;c->forklifts=b.forklifts;
        if(scenario::serialize_scenario(normalized)!=scenario::serialize_scenario(r.baseline)) throw std::invalid_argument("Paired statistical candidates may change resource capacities only; workload, horizon and tick rate must match");
        if(!candidate.id || candidate.definition.name=="Baseline" || !names.insert(candidate.definition.name).second || !ids.insert(candidate.id).second) throw std::invalid_argument("Duplicate/invalid statistical candidate identity");
    }
    const auto volume=scenario::workload::arrival_bound(b,r.baseline.simulation.duration_seconds,r.baseline.simulation.tick_rate);
    if(std::uint64_t{volume}*r.plan.count*(r.candidates.size()+4)>max_statistical_arrival_work) throw std::invalid_argument("Statistical workload exceeds 20 million conservative arrival executions (including probes)");
}
void aggregate_statistical_report(StatisticalReport& r) {
    validate_statistical_request(r.request);const auto seeds=replication_seeds(r.request.plan);
    if(r.replications.size()!=seeds.size()) throw std::invalid_argument("Incomplete statistical report");
    const bool stochastic=workload_varies(std::get<scenario::WarehouseConfig>(r.request.baseline.domain));
    for(std::size_t i=0;i<seeds.size();++i) if(r.replications[i].seed!=seeds[i] || r.replications[i].candidates.size()!=r.request.candidates.size()) throw std::invalid_argument("Replication identity mismatch");
    r.candidates.clear();
    const auto summary=[&](std::size_t metric,auto extract) {
        std::vector<std::optional<double>> baseline,candidate;
        for(const auto& row:r.replications) {baseline.push_back(metric_value(row.baseline,static_cast<StatisticalMetric>(metric)));candidate.push_back(extract(row,static_cast<StatisticalMetric>(metric)));}
        return summarize_pairs(baseline,candidate,higher_is_better(static_cast<StatisticalMetric>(metric)),stochastic);
    };
    for(std::size_t i=0;i<r.request.candidates.size();++i) {
        const auto& input=r.request.candidates[i];StatisticalResult candidate;candidate.candidate_id=input.id;candidate.name=input.definition.name;
        for(std::size_t m=0;m<statistical_metric_count;++m) candidate.metrics[m]=summary(m,[i](const auto& row,auto metric){return metric_value(row.candidates[i],metric);});
        r.candidates.push_back(std::move(candidate));
    }
    for(std::size_t p=0;p<3;++p) for(std::size_t m=0;m<statistical_metric_count;++m)
        r.probes[p][m]=summary(m,[p](const auto& row,auto metric)->std::optional<double>{return row.probes[p]?metric_value(*row.probes[p],metric):std::nullopt;});
    const auto metric=static_cast<std::size_t>(objective_metric(r.request.objective));
    std::vector<std::size_t> order;for(std::size_t i=0;i<r.candidates.size();++i) if(r.candidates[i].metrics[metric].candidate_mean) order.push_back(i);
    std::sort(order.begin(),order.end(),[&](auto a,auto b) {
        const auto x=*r.candidates[a].metrics[metric].candidate_mean,y=*r.candidates[b].metrics[metric].candidate_mean;
        if(x!=y) return higher_is_better(static_cast<StatisticalMetric>(metric))?x>y:x<y;
        const auto da=compare_metrics({}, {},counts(r.request.baseline),counts(r.request.candidates[a].definition)).resources;
        const auto db=compare_metrics({}, {},counts(r.request.baseline),counts(r.request.candidates[b].definition)).resources;
        if(da.additions!=db.additions) return da.additions<db.additions;if(da.changes!=db.changes) return da.changes<db.changes;
        return r.candidates[a].name<r.candidates[b].name;
    });
    for(std::size_t i=0;i<order.size();++i) r.candidates[order[i]].rank=static_cast<std::uint32_t>(i+1);
    r.limitations={"Paired raw differences are candidate minus baseline; improvement direction depends on the metric.",
        "95% paired Student-t intervals use the NIST 0.975 critical-value table rounded to three decimals and n-1 degrees of freedom.",
        "Intervals assume representative distinct-seed stochastic replications and an adequate mean-difference approximation; skew/discreteness can invalidate small-sample coverage.",
        "Nominal per-candidate, per-metric intervals have no simultaneous familywise coverage adjustment. Selecting the largest observed gain introduces selection bias.",
        "Point-estimate ranks do not establish superiority across metrics or guaranteed future improvement.",
        "Favorable-seed fraction is descriptive, not a probability of future superiority. Relative change compares means; no relative-effect confidence interval is provided.",
        "Started queue-wait means exclude still-waiting shipments. Different service outcomes can change the measured population.",
        "Fresh-start capacity comparisons only; historical checkpoints are seed-specific and are not reused. No aggregate warehouse snapshot exists.",
        "Seeds select reproducible pseudo-random model realizations; they do not calibrate this FIFO warehouse to reality."};
    if(!stochastic) r.limitations.push_back("Deterministic workload: distinct seeds do not create varying independent stochastic observations; uncertainty intervals suppressed.");
}
StatisticalReport run_statistical_experiment(const StatisticalRequest& request,const simulation::ExecutionControl* control) {
    validate_statistical_request(request);StatisticalReport r;r.request=request;
    std::sort(r.request.candidates.begin(),r.request.candidates.end(),[](const auto& a,const auto& b){return a.definition.name<b.definition.name;});
    const auto seeds=replication_seeds(request.plan);
    for(std::size_t i=0;i<seeds.size();++i) {
        if(control) control->update("Replication",std::to_string(seeds[i]),i+1,seeds.size());
        auto d=request.baseline;d.simulation.random_seed=seeds[i];
        auto input=prepare_request(d,0,request.objective,{{"Unchanged",counts(d)}},control);input.candidates.clear();
        for(const auto& c:r.request.candidates) input.candidates.push_back({c.definition.name,counts(c.definition)});
        const auto result=run_comparison(input,control,nullptr,true);
        ReplicationOutcome row;row.seed=seeds[i];row.baseline=result.baseline.metrics;++r.simulation_executions;
        for(const auto& c:result.candidates) {row.candidates.push_back(c.metrics);++r.simulation_executions;}
        for(std::size_t p=0;p<3;++p) if(result.bottlenecks[p].result) {
            row.probes[p]=result.bottlenecks[p].result->metrics;
            if(std::none_of(result.candidates.begin(),result.candidates.end(),[&](const auto& c){return c.candidate.resources==result.bottlenecks[p].result->candidate.resources;})) ++r.simulation_executions;
        }
        r.replications.push_back(std::move(row));if(control) control->update("Replication complete",{},i+1,seeds.size());
    }
    if(control) control->update("Aggregating paired results");aggregate_statistical_report(r);return r;
}
}
