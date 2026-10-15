#include <simforge/analysis/checkpoint_intervention.hpp>
#include "statistical_json_shared.hpp"
#include <charconv>
#include <cmath>
#include <algorithm>

namespace simforge::analysis::checkpoint {
namespace {
using namespace scenario::detail;
Json command_json(const warehouse::WarehouseCommand& c){warehouse::validate_command(c);return {{"tick",c.tick},{"sequence",c.sequence},{"type",static_cast<unsigned>(c.type)},{"resource",warehouse::resource_name(c.resource)},{"capacity",c.capacity}};}
warehouse::WarehouseCommand command_value(const Json& j){fields(j,{"tick","sequence","type","resource","capacity"},{},"command");warehouse::WarehouseCommand c;
    c.tick=integer(j.at("tick"),scenario::max_execution_ticks,"tick");c.sequence=integer(j.at("sequence"),warehouse::max_capacity_changes-1,"sequence");c.type=static_cast<warehouse::WarehouseCommandType>(integer(j.at("type"),1,"type"));c.resource=warehouse::parse_resource_type(string(j.at("resource"),"resource"));c.capacity=static_cast<std::uint32_t>(integer(j.at("capacity"),100000,"capacity"));warehouse::validate_command(c);return c;}
Json request_value(const Request& r){validate(r);Json commands=Json::array();for(const auto& c:r.source_commands)commands.push_back(command_json(c));
    return {{"schema_version",1},{"mode","paired_checkpoint_intervention"},{"experiment_id",r.id},{"source",Json::parse(scenario::serialize_scenario(r.source))},
        {"checkpoint_tick",r.checkpoint_tick},{"boundary","after_due_events_and_source_commands"},{"initialization","fresh_start_per_seed"},
        {"intervention",{{"resource",warehouse::resource_name(r.intervention.resource)},{"action",r.intervention.action==Action::Set?"set":r.intervention.action==Action::Increase?"increase":"decrease"},{"value",r.intervention.value}}},
        {"source_commands",std::move(commands)},{"replications",detail::statistical_plan_json(r.plan)},{"objective",objective_name(r.objective)},
        {"generator_version",scenario::workload::generator_version(std::get<scenario::WarehouseConfig>(r.source.domain))},{"method_version",method_version}};
}
Request request_from(const Json& j){fields(j,{"schema_version","mode","experiment_id","source","checkpoint_tick","boundary","initialization","intervention","source_commands","replications","objective","generator_version","method_version"},{},"checkpoint request");
    Request r;r.version=static_cast<std::uint32_t>(integer(j.at("schema_version"),1,"schema"));r.id=integer(j.at("experiment_id"),UINT64_MAX-1,"experiment_id");r.source=scenario::parse_scenario(j.at("source").dump());r.checkpoint_tick=integer(j.at("checkpoint_tick"),scenario::max_execution_ticks,"checkpoint_tick");r.plan=detail::statistical_plan_value(j.at("replications"));r.objective=parse_objective(string(j.at("objective"),"objective"));
    const auto& a=j.at("intervention");fields(a,{"resource","action","value"},{},"intervention");r.intervention.resource=warehouse::parse_resource_type(string(a.at("resource"),"resource"));const auto action=string(a.at("action"),"action");if(action=="set")r.intervention.action=Action::Set;else if(action=="increase")r.intervention.action=Action::Increase;else if(action=="decrease")r.intervention.action=Action::Decrease;else invalid("action","unsupported action");r.intervention.value=static_cast<std::uint32_t>(integer(a.at("value"),100000,"value"));
    const auto& commands=j.at("source_commands");if(!commands.is_array() || commands.size()>max_source_commands)invalid("source_commands","bounded array required");for(const auto& c:commands)r.source_commands.push_back(command_value(c));
    validate(r);if(request_value(r)!=j)invalid("request","incompatible versions, boundary or provenance");return r;
}
Json optional(std::optional<double> v){return v?Json(*v):Json(nullptr);}
std::array<std::uint64_t,3> expected_available(const Request& r,std::uint64_t end,bool intervention){
    const auto& config=std::get<scenario::WarehouseConfig>(r.source.domain);warehouse::ResourceCounts c{config.loading_bays,config.workers,config.forklifts};
    std::array<std::uint64_t,3> values{};std::uint64_t previous=0;
    const auto add=[&](std::uint64_t tick){for(unsigned i=0;i<3;++i)values[i]+=std::uint64_t{warehouse::capacity_for(c,static_cast<warehouse::ResourceType>(i))}*(tick-previous);previous=tick;};
    bool applied=false;const auto apply=[&]{const auto before=warehouse::capacity_for(c,r.intervention.resource);const auto after=r.intervention.action==Action::Set?r.intervention.value:r.intervention.action==Action::Increase?before+r.intervention.value:before-r.intervention.value;warehouse::set_capacity(c,r.intervention.resource,after);};
    for(const auto& command:r.source_commands){if(command.tick>end)break;if(intervention && !applied && command.tick>r.checkpoint_tick){add(r.checkpoint_tick);apply();applied=true;}add(command.tick);warehouse::set_capacity(c,command.resource,command.capacity);}
    if(intervention && !applied && end>=r.checkpoint_tick){add(r.checkpoint_tick);apply();}add(end);return values;
}
void validate_metrics(const warehouse::WarehouseMetrics& m,std::uint64_t tick,const Request& r,const std::array<std::uint64_t,3>& available){
    const double rate=r.source.simulation.tick_rate;const auto& config=std::get<scenario::WarehouseConfig>(r.source.domain);
    if(m.elapsed_ticks!=tick || m.arrived>scenario::max_warehouse_shipments || m.completed>m.started || m.started>m.arrived || m.waiting!=m.arrived-m.started || m.in_progress!=m.started-m.completed || m.total_wait_ticks>tick*m.started || m.max_wait_ticks>tick || m.max_wait_ticks>m.total_wait_ticks || m.occupied_resource_ticks>tick*m.started || m.completed_processing_ticks>m.occupied_resource_ticks)invalid("metrics","invalid bounded counters");
    const auto ratio=[&](std::uint64_t x,std::uint64_t n)->std::optional<double>{return n?std::optional<double>{static_cast<double>(x)/static_cast<double>(n)/rate}:std::nullopt;};
    if(m.average_wait_seconds!=ratio(m.total_wait_ticks,m.started) || m.maximum_wait_seconds!=(m.started?std::optional<double>{m.max_wait_ticks/rate}:std::nullopt) || m.completed_processing_seconds!=m.completed_processing_ticks/rate || m.average_processing_seconds!=(m.completed?std::optional<double>{m.completed_processing_seconds/static_cast<double>(m.completed)}:std::nullopt) || m.throughput_per_hour!=(tick?std::optional<double>{static_cast<double>(m.completed)*3600*rate/static_cast<double>(tick)}:std::nullopt))invalid("metrics","derived averages conflict with counters");
    const std::array ratios{m.loading_bay_utilization,m.worker_utilization,m.forklift_utilization};for(unsigned i=0;i<3;++i){if(m.occupied_resource_ticks>available[i] || ratios[i]!=(available[i]?std::optional<double>{static_cast<double>(m.occupied_resource_ticks)/static_cast<double>(available[i])}:std::nullopt))invalid("utilization","inconsistent resource-time sufficient statistics");}
    const auto minimum=std::uint64_t{config.workload.processing==scenario::ProcessingMode::Constant?config.processing_duration_seconds:config.workload.processing_min_seconds}*r.source.simulation.tick_rate;
    const auto maximum=std::uint64_t{config.workload.processing==scenario::ProcessingMode::Constant?config.processing_duration_seconds:config.workload.processing_max_seconds}*r.source.simulation.tick_rate;
    if(m.completed_processing_ticks<m.completed*minimum || m.completed_processing_ticks>m.completed*maximum)invalid("processing","outside shipment requirement bounds");
}
Json evidence(const Report& r){
    auto checked=r;aggregate(checked);if(checked.end_metrics!=r.end_metrics || checked.post_metrics!=r.post_metrics || checked.limitations!=r.limitations)invalid("summary","derived evidence conflicts with observations");
    if(r.prefix_executions!=r.outcomes.size() || r.control_executions!=r.outcomes.size() || r.intervention_executions!=r.outcomes.size())invalid("executions","expected N prefixes and N continuations per arm");
    Json rows=Json::array(),end=Json::object(),post=Json::object();const auto horizon=scenario::total_ticks(r.request.source);
    const auto prefix_available=expected_available(r.request,r.request.checkpoint_tick,false),control_available=expected_available(r.request,horizon,false),intervention_available=expected_available(r.request,horizon,true);
    const auto& config=std::get<scenario::WarehouseConfig>(r.request.source.domain);warehouse::ResourceCounts capacity{config.loading_bays,config.workers,config.forklifts};for(const auto& c:r.request.source_commands)if(c.tick<=r.request.checkpoint_tick)warehouse::set_capacity(capacity,c.resource,c.capacity);
    const auto before=warehouse::capacity_for(capacity,r.request.intervention.resource);const auto after=r.request.intervention.action==Action::Set?r.request.intervention.value:r.request.intervention.action==Action::Increase?before+r.request.intervention.value:before-r.request.intervention.value;
    const warehouse::WarehouseCommand expected_command{r.request.checkpoint_tick,0,warehouse::WarehouseCommandType::SetResourceCapacity,after,r.request.intervention.resource};
    for(const auto& row:r.outcomes){
        auto seeded_source=r.request.source;seeded_source.simulation.random_seed=row.seed;
        if(row.control.arrived!=scenario::workload::arrival_count(seeded_source))invalid("arrivals","retained counts differ from the frozen seeded workload");
        if(row.checkpoint_capacity!=capacity || row.applied!=expected_command || row.checkpoint_available!=prefix_available || row.control_available!=control_available || row.intervention_available!=intervention_available)invalid("checkpoint","command/capacity/exposure provenance mismatch");
        const auto hash_valid=[](const std::string& s){return s.starts_with("fnv1a64:") && s.size()>8 && s.size()<=24 && s.find_first_not_of("0123456789abcdef",8)==std::string::npos;};if(!hash_valid(row.checkpoint_digest)||!hash_valid(row.workload_digest))invalid("digest","invalid divergence digest");
        validate_metrics(row.checkpoint,r.request.checkpoint_tick,r.request,row.checkpoint_available);validate_metrics(row.control,horizon,r.request,row.control_available);validate_metrics(row.intervention,horizon,r.request,row.intervention_available);
        if(row.checkpoint_events!=row.checkpoint.arrived+row.checkpoint.completed || row.control.arrived!=row.intervention.arrived)invalid("pairing","event or arrival-count mismatch");
        for(const auto* final:{&row.control,&row.intervention})if(final->arrived<row.checkpoint.arrived || final->started<row.checkpoint.started || final->completed<row.checkpoint.completed || final->total_wait_ticks<row.checkpoint.total_wait_ticks || final->max_wait_ticks<row.checkpoint.max_wait_ticks || final->completed_processing_ticks<row.checkpoint.completed_processing_ticks || final->occupied_resource_ticks<row.checkpoint.occupied_resource_ticks)invalid("metrics","cumulative counters moved backwards");
        Json differences=Json::object(),post_values=Json::object();for(std::size_t m=0;m<metric_count;++m){const auto a=metric_value(row,false,m,false,r.request),b=metric_value(row,true,m,false,r.request),pa=metric_value(row,false,m,true,r.request),pb=metric_value(row,true,m,true,r.request);differences[metric_name(m)]=a&&b?Json(*b-*a):Json(nullptr);post_values[metric_name(m)]={{"control",optional(pa)},{"intervention",optional(pb)},{"difference",pa&&pb?Json(*pb-*pa):Json(nullptr)}};}
        rows.push_back({{"seed",row.seed},{"checkpoint_digest",row.checkpoint_digest},{"workload_digest",row.workload_digest},{"checkpoint_events",row.checkpoint_events},{"checkpoint_capacity",{capacity.loading_bays,capacity.workers,capacity.forklifts}},{"applied",command_json(row.applied)},
            {"checkpoint",detail::statistical_metrics_json(row.checkpoint)},{"control",detail::statistical_metrics_json(row.control)},{"intervention",detail::statistical_metrics_json(row.intervention)},
            {"checkpoint_available_ticks",row.checkpoint_available},{"control_available_ticks",row.control_available},{"intervention_available_ticks",row.intervention_available},{"end_differences",std::move(differences)},{"post_metrics",std::move(post_values)}});
    }
    for(std::size_t m=0;m<metric_count;++m){end[metric_name(m)]=detail::statistical_summary_json(r.end_metrics[m]);post[metric_name(m)]=detail::statistical_summary_json(r.post_metrics[m]);}
    return {{"checkpoint_report_version",1},{"status","completed"},{"experiment_identity",identity(r.request)},{"source_identity",scenario::serialize_scenario(r.request.source)},{"request",request_value(r.request)},
        {"method",{{"name","paired_student_t_nist_table"},{"version",1},{"confidence",.95},{"minimum_interval_pairs",3},{"utilization_direction","descriptive"}}},
        {"prefix_executions",r.prefix_executions},{"control_executions",r.control_executions},{"intervention_executions",r.intervention_executions},{"outcomes",std::move(rows)},{"end_summaries",std::move(end)},{"post_summaries",std::move(post)},{"limitations",r.limitations}};
}
std::string number(std::optional<double> x){if(!x)return {};char b[64];auto result=std::to_chars(b,b+64,*x,std::chars_format::general,17);if(result.ec!=std::errc{})throw std::runtime_error("CSV conversion failed");return {b,result.ptr};}
}
std::string identity(const Request& r){auto j=request_value(r);j.erase("experiment_id");std::uint64_t hash=14695981039346656037ULL;for(unsigned char c:j.dump()){hash^=c;hash*=1099511628211ULL;}char b[16];auto result=std::to_chars(b,b+16,hash,16);return "checkpoint-fnv1a64:"+std::string(b,result.ptr);}
std::string request_json(const Request& r){return request_value(r).dump();}
Request parse_request(std::string_view text){return request_from(scenario::detail::parse_json(text,scenario::max_scenario_bytes+65536));}
std::string report_json(const Report& r){auto bytes=evidence(r).dump();if(bytes.size()>max_report_bytes)throw std::length_error("Checkpoint report exceeds 4 MiB");return bytes;}
void save_report(const std::filesystem::path& path,const Report& r){detail::save_output(path,report_json(r));}
void save_csv(const std::filesystem::path& path,const Report& r,bool seeds){detail::save_output(path,report_csv(r,seeds));}
Report parse_report(std::string_view text){const auto j=scenario::detail::parse_json(text,max_report_bytes);Report r;r.request=request_from(j.at("request"));
    const auto& rows=j.at("outcomes");if(!rows.is_array() || rows.size()!=r.request.plan.count)invalid("outcomes","incomplete paired evidence");
    const auto array=[](const Json& x){if(!x.is_array() || x.size()!=3)invalid("resource_ticks","three counters required");std::array<std::uint64_t,3> a{};for(unsigned i=0;i<3;++i)a[i]=integer(x[i],1'000'000'000'000ULL,"counter");return a;};
    for(const auto& v:rows){Outcome row;row.seed=integer(v.at("seed"),UINT64_MAX,"seed");row.checkpoint_digest=string(v.at("checkpoint_digest"),"digest");row.workload_digest=string(v.at("workload_digest"),"digest");row.checkpoint_events=integer(v.at("checkpoint_events"),200000,"events");const auto counts=array(v.at("checkpoint_capacity"));for(auto n:counts)if(n>100000)invalid("capacity","out of bounds");row.checkpoint_capacity={static_cast<std::uint32_t>(counts[0]),static_cast<std::uint32_t>(counts[1]),static_cast<std::uint32_t>(counts[2])};row.applied=command_value(v.at("applied"));
        row.checkpoint=detail::statistical_metrics_value(v.at("checkpoint"));row.control=detail::statistical_metrics_value(v.at("control"));row.intervention=detail::statistical_metrics_value(v.at("intervention"));row.checkpoint_available=array(v.at("checkpoint_available_ticks"));row.control_available=array(v.at("control_available_ticks"));row.intervention_available=array(v.at("intervention_available_ticks"));r.outcomes.push_back(std::move(row));}
    r.prefix_executions=integer(j.at("prefix_executions"),100,"prefixes");r.control_executions=integer(j.at("control_executions"),100,"controls");r.intervention_executions=integer(j.at("intervention_executions"),100,"interventions");aggregate(r);if(evidence(r)!=j)invalid("report","derived metrics, versions or provenance differ from saved evidence");return r;}
std::string report_csv(const Report& r,bool seeds){(void)evidence(r);std::string csv="experiment_identity,experiment_id,checkpoint_tick,horizon_tick,resource,action,value,scope,seed,metric,unit,control,intervention,difference,n,defined_pairs,sd,se,ci95_lower,ci95_upper,positive,negative,tied,favorable_fraction\r\n";
    const auto prefix=identity(r.request)+','+std::to_string(r.request.id)+','+std::to_string(r.request.checkpoint_tick)+','+std::to_string(scenario::total_ticks(r.request.source))+','+std::string(warehouse::resource_name(r.request.intervention.resource))+','+(r.request.intervention.action==Action::Set?"set":r.request.intervention.action==Action::Increase?"increase":"decrease")+','+std::to_string(r.request.intervention.value)+',';
    for(bool post:{false,true})for(std::size_t m=0;m<metric_count;++m){const auto scope=post?"post_checkpoint":"end_cumulative";const auto suffix=std::string(metric_name(m))+','+std::string(metric_unit(m))+',';
        if(seeds)for(const auto& row:r.outcomes){const auto a=metric_value(row,false,m,post,r.request),b=metric_value(row,true,m,post,r.request);csv+=prefix+scope+','+std::to_string(row.seed)+','+suffix+number(a)+','+number(b)+','+number(a&&b?std::optional<double>{*b-*a}:std::nullopt)+",,,,,,,,,,\r\n";}
        else {const auto& s=(post?r.post_metrics:r.end_metrics)[m];csv+=prefix+scope+",,"+suffix+number(s.baseline_mean)+','+number(s.candidate_mean)+','+number(s.mean_difference)+','+std::to_string(s.count)+','+std::to_string(s.defined)+','+number(s.standard_deviation)+','+number(s.standard_error)+','+number(s.lower)+','+number(s.upper)+','+std::to_string(s.positive)+','+std::to_string(s.negative)+','+std::to_string(s.zero)+','+number(s.favorable_fraction)+"\r\n";}
    }return csv;
}
}
