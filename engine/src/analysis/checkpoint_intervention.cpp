#include <simforge/analysis/checkpoint_intervention.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>

namespace simforge::analysis::checkpoint {
namespace {
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
std::string digest(std::span<const std::byte> bytes){
    std::uint64_t hash=14695981039346656037ULL;for(auto c:bytes){hash^=std::to_integer<unsigned char>(c);hash*=1099511628211ULL;}
    char buffer[16];auto end=std::to_chars(buffer,buffer+16,hash,16);return "fnv1a64:"+std::string(buffer,end.ptr);
}
std::array<std::uint64_t,3> available(const warehouse::WarehouseSnapshot& s){return {s.totals.available_bay_ticks,s.totals.available_worker_ticks,s.totals.available_forklift_ticks};}
warehouse::ResourceCounts source_capacity(const Request& r){const auto& c=std::get<scenario::WarehouseConfig>(r.source.domain);return {c.loading_bays,c.workers,c.forklifts};}
std::uint32_t resulting(const Intervention& a,warehouse::ResourceCounts c){
    const std::uint64_t before=warehouse::capacity_for(c,a.resource);std::uint64_t after=0;
    switch(a.action){case Action::Set:after=a.value;break;case Action::Increase:after=before+a.value;break;
    case Action::Decrease:if(a.value>before)throw std::invalid_argument("Capacity subtraction underflow");after=before-a.value;break;
    default:throw std::invalid_argument("Unsupported checkpoint capacity action");}
    if(after>100000)throw std::invalid_argument("Capacity outside 0..100000");
    warehouse::set_capacity(c,a.resource,static_cast<std::uint32_t>(after));
    if(std::uint64_t{c.loading_bays}+c.workers+c.forklifts>100000)throw std::invalid_argument("Total capacity exceeds 100000");
    return static_cast<std::uint32_t>(after);
}
void advance(warehouse::WarehouseSimulation& runtime,std::span<const warehouse::WarehouseCommand> commands,
    std::uint64_t target,warehouse::WarehouseInputRecorder& recorder,const simulation::ExecutionControl* control){
    for(const auto& command:commands){runtime.advance_to(command.tick,control);if(control)control->check();recorder.set_resource_capacity(command.resource,command.capacity);}
    runtime.advance_to(target,control);
}
std::string workload_signature(const warehouse::WarehouseSnapshot& a,const warehouse::WarehouseSnapshot& b){
    if(a.shipments.size()!=b.shipments.size())throw std::runtime_error("Checkpoint branches received different external demand");
    std::vector<std::byte> bytes;bytes.reserve(a.shipments.size()*24);
    for(std::size_t i=0;i<a.shipments.size();++i){const auto& x=a.shipments[i];const auto& y=b.shipments[i];
        if(x.id!=y.id || x.arrival_tick!=y.arrival_tick || x.required_processing_ticks!=y.required_processing_ticks)
            throw std::runtime_error("Checkpoint branches received unmatched shipment identities/requirements");
        for(const auto v:std::array<std::uint64_t,3>{x.id,x.arrival_tick,x.required_processing_ticks})for(unsigned shift=0;shift<64;shift+=8)bytes.push_back(static_cast<std::byte>((v>>shift)&255));
    }return digest(bytes);
}
}
void validate(const Request& r){
    if(r.version!=1)throw std::invalid_argument("Unsupported checkpoint request version");
    scenario::validate(r.source);(void)objective_name(r.objective);(void)replication_seeds(r.plan);
    if(!std::holds_alternative<scenario::WarehouseConfig>(r.source.domain))throw std::invalid_argument("Checkpoint studies require a warehouse scenario");
    if(r.checkpoint_tick>scenario::total_ticks(r.source))throw std::invalid_argument("Checkpoint tick outside 0..horizon");
    if(r.source_commands.size()>max_source_commands)throw std::invalid_argument("At most 32 source commands supported");
    auto capacity=source_capacity(r);std::uint64_t previous=0;
    for(std::size_t i=0;i<r.source_commands.size();++i){const auto& c=r.source_commands[i];warehouse::validate_command(c);
        if(c.sequence!=i || c.tick<previous || c.tick>scenario::total_ticks(r.source))throw std::invalid_argument("Invalid source command order/horizon");
        if(c.tick>=r.checkpoint_tick && c.resource==r.intervention.resource)throw std::invalid_argument("Source command at/after checkpoint conflicts with intervention resource");
        previous=c.tick;warehouse::set_capacity(capacity,c.resource,c.capacity);
        if(std::uint64_t{capacity.loading_bays}+capacity.workers+capacity.forklifts>100000)throw std::invalid_argument("Source command total capacity exceeds schema limit");
    }
    capacity=source_capacity(r);for(const auto& c:r.source_commands)if(c.tick<=r.checkpoint_tick)warehouse::set_capacity(capacity,c.resource,c.capacity);
    const auto after=resulting(r.intervention,capacity);warehouse::set_capacity(capacity,r.intervention.resource,after);
    for(const auto& c:r.source_commands)if(c.tick>r.checkpoint_tick){warehouse::set_capacity(capacity,c.resource,c.capacity);if(std::uint64_t{capacity.loading_bays}+capacity.workers+capacity.forklifts>100000)throw std::invalid_argument("Intervention and future commands exceed total capacity limit");}
    const auto& config=std::get<scenario::WarehouseConfig>(r.source.domain);
    const auto bound=scenario::workload::arrival_bound(config,r.source.simulation.duration_seconds,r.source.simulation.tick_rate);
    if(std::uint64_t{bound}*r.plan.count*3>max_statistical_arrival_work)throw std::invalid_argument("Checkpoint study exceeds 20 million conservative arrival executions");
}
std::string_view metric_name(std::size_t m){if(m<5)return analysis::metric_name(static_cast<StatisticalMetric>(m));switch(m){case 5:return "loading_bay_utilization";case 6:return "worker_utilization";case 7:return "forklift_utilization";default:throw std::invalid_argument("Unsupported checkpoint metric");}}
std::string_view metric_unit(std::size_t m){if(m<5)return analysis::metric_unit(static_cast<StatisticalMetric>(m));(void)metric_name(m);return "fraction";}
bool higher_is_better(std::size_t m){(void)metric_name(m);return m==0 || m==2;}
std::optional<double> metric_value(const Outcome& row,bool intervention,std::size_t m,bool post,const Request& r){
    (void)metric_name(m);const auto& end=intervention?row.intervention:row.control;const auto& before=row.checkpoint;
    if(!post){if(m<5)return analysis::metric_value(end,static_cast<StatisticalMetric>(m));return m==5?end.loading_bay_utilization:m==6?end.worker_utilization:end.forklift_utilization;}
    const auto duration=end.elapsed_ticks-before.elapsed_ticks;if(!duration)return {};
    if(m==0)return static_cast<double>(end.completed-before.completed);
    if(m==1 || m==4)return {}; // End stocks and cumulative maxima are not interval-additive.
    if(m==2)return static_cast<double>(end.completed-before.completed)*3600*r.source.simulation.tick_rate/static_cast<double>(duration);
    if(m==3){const auto starts=end.started-before.started;return starts?std::optional<double>{static_cast<double>(end.total_wait_ticks-before.total_wait_ticks)/static_cast<double>(starts)/r.source.simulation.tick_rate}:std::nullopt;}
    const auto capacity=(intervention?row.intervention_available:row.control_available)[m-5]-row.checkpoint_available[m-5];
    return capacity?std::optional<double>{static_cast<double>(end.occupied_resource_ticks-before.occupied_resource_ticks)/static_cast<double>(capacity)}:std::nullopt;
}
Branches reconstruct(const Request& request,std::uint64_t seed,const simulation::ExecutionControl* control,Timings* timings){
    validate(request);const auto seeds=replication_seeds(request.plan);if(std::find(seeds.begin(),seeds.end(),seed)==seeds.end())throw std::invalid_argument("Seed is not part of the frozen checkpoint study");
    Timings local;auto& time=timings?*timings:local;Branches result;auto source=request.source;source.simulation.random_seed=seed;
    const auto split=std::upper_bound(request.source_commands.begin(),request.source_commands.end(),request.checkpoint_tick,[](auto tick,const auto& c){return tick<c.tick;});
    const auto prefix_commands=std::span(request.source_commands.begin(),split),future_commands=std::span(split,request.source_commands.end());
    if(control)control->update("Pre-checkpoint",std::to_string(seed));auto started=Clock::now();
    warehouse::WarehouseSimulation prefix{source,control};warehouse::WarehouseInputRecorder prefix_recorder{prefix};
    advance(prefix,prefix_commands,request.checkpoint_tick,prefix_recorder,control);time.prefix_ms+=elapsed(started);
    started=Clock::now();result.checkpoint=prefix.capture_snapshot();warehouse::validate_snapshot(result.checkpoint);
    auto& row=result.outcome;row.seed=seed;row.checkpoint_digest=digest(warehouse::serialize_snapshot(result.checkpoint));row.checkpoint=prefix.metrics();row.checkpoint_events=prefix.processed_events();row.checkpoint_capacity=prefix.resource_capacity();row.checkpoint_available=available(result.checkpoint);time.snapshot_ms+=elapsed(started);
    if(control)control->update("Constructing branches",std::to_string(seed));started=Clock::now();
    auto control_branch=warehouse::WarehouseSimulation::create_from(result.checkpoint);auto intervention_branch=warehouse::WarehouseSimulation::create_from(result.checkpoint);
    if(warehouse::first_difference(control_branch.capture_snapshot(),intervention_branch.capture_snapshot()))throw std::runtime_error("Checkpoint restoration differs between branches");
    time.branch_ms+=elapsed(started);warehouse::WarehouseInputRecorder control_recorder{control_branch},intervention_recorder{intervention_branch};
    if(control)control->update("Control continuation",std::to_string(seed));started=Clock::now();
    advance(control_branch,future_commands,scenario::total_ticks(source),control_recorder,control);row.control=control_branch.metrics();result.control=control_branch.capture_snapshot();row.control_available=available(result.control);time.control_ms+=elapsed(started);
    if(control)control->update("Intervention continuation",std::to_string(seed));started=Clock::now();
    row.applied={request.checkpoint_tick,0,warehouse::WarehouseCommandType::SetResourceCapacity,resulting(request.intervention,row.checkpoint_capacity),request.intervention.resource};
    intervention_recorder.submit(row.applied);advance(intervention_branch,future_commands,scenario::total_ticks(source),intervention_recorder,control);
    row.intervention=intervention_branch.metrics();result.intervention=intervention_branch.capture_snapshot();row.intervention_available=available(result.intervention);time.intervention_ms+=elapsed(started);
    if(control)control->update("Verifying paired workload",std::to_string(seed));row.workload_digest=workload_signature(result.control,result.intervention);
    return result;
}
void aggregate(Report& report){
    validate(report.request);const auto seeds=replication_seeds(report.request.plan);if(report.outcomes.size()!=seeds.size())throw std::invalid_argument("Incomplete checkpoint study");
    const bool stochastic=workload_varies(std::get<scenario::WarehouseConfig>(report.request.source.domain));
    for(std::size_t i=0;i<seeds.size();++i)if(report.outcomes[i].seed!=seeds[i])throw std::invalid_argument("Checkpoint seed order differs from plan");
    for(bool post:{false,true})for(std::size_t m=0;m<metric_count;++m){std::vector<std::optional<double>> b,c;
        for(const auto& row:report.outcomes){b.push_back(metric_value(row,false,m,post,report.request));c.push_back(metric_value(row,true,m,post,report.request));}
        auto& summary=(post?report.post_metrics:report.end_metrics)[m];summary=summarize_pairs(b,c,higher_is_better(m),stochastic);
        if(m>=5){summary.favorable_fraction.reset();summary.diagnostic="Utilization is descriptive; positive/negative differences have no preferred direction. "+std::string(summary.lower?"Nominal paired interval under the stated assumptions.":!stochastic?"Deterministic workload: no uncertainty interval.":summary.defined!=summary.count?"Undefined pairs: no aggregate or interval.":"Fewer than three pairs: interval unavailable.");}
    }
    report.limitations={"Fresh-start per-seed prefix, then two independently restored continuations. Checkpoint follows all due events and source commands at that tick; the new intervention follows them.",
        "Raw paired difference is intervention minus control. Higher completed/throughput and lower waiting/wait are favorable; utilization has no preferred direction.",
        "Nominal 95% paired Student-t intervals use existing three-decimal NIST critical values, n-1 degrees of freedom and at least three complete stochastic pairs. Small-sample skew/discreteness can invalidate coverage.",
        "Distinct seeds select reproducible pseudo-random realizations, not independent real warehouse observations. Deterministic workload intervals are suppressed; zero variance is not a guarantee.",
        "Intervals are conditional on this source scenario, demand model, seed plan, checkpoint, capacities and horizon. Multiple metrics/timing exploration are unadjusted; no optimization or universal causal claim.",
        "End metrics are cumulative. Post-checkpoint completed/throughput use counter differences; mean wait uses wait-sum/started differences and includes pre-checkpoint waiting of newly started shipments. Still-waiting shipments are excluded.",
        "Post-checkpoint utilization uses occupied and available resource-tick differences. Waiting stock and maximum wait have no post-interval aggregate. Zero-duration/zero-denominator values are unavailable.",
        "Source commands are replayed in both arms. Commands on the intervention resource at or after checkpoint are rejected. Reductions below active reservations fail the entire study without publishing partial evidence.",
        "Playback reconstruction requires the frozen simulation/workload versions on a supported reproducible platform; FNV-1a evidence digests detect accidental divergence, not malicious tampering."};
}
Report run(const Request& request,const simulation::ExecutionControl* control){
    validate(request);const auto began=Clock::now();Report report;report.request=request;const auto seeds=replication_seeds(request.plan);
    for(std::size_t i=0;i<seeds.size();++i){if(control)control->update("Replication",std::to_string(seeds[i]),i+1,seeds.size());
        auto branches=reconstruct(request,seeds[i],control,&report.timings);report.outcomes.push_back(std::move(branches.outcome));
        ++report.prefix_executions;++report.control_executions;++report.intervention_executions;
        if(control)control->update("Candidate complete");
        if(control)control->update("Replication complete",{},i+1,seeds.size());
    }
    if(control)control->update("Aggregating checkpoint pairs");const auto started=Clock::now();aggregate(report);report.timings.aggregation_ms=elapsed(started);report.timings.total_ms=elapsed(began);return report;
}
}
