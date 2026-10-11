#include <simforge/analysis/statistical_experiment.hpp>
#include "comparison_json.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace simforge::analysis {
namespace {
using namespace scenario::detail;
Json optional(std::optional<double> x) {return x?Json(*x):Json(nullptr);}
Json plan_json(const ReplicationPlan& p) {
    (void)replication_seeds(p);
    Json j{{"count",p.count},{"policy",p.policy==SeedPolicy::Sequential?"sequential_v1":"explicit"},{"generator_version",p.generator_version},
        {"method_version",p.method_version},{"confidence",p.confidence}};
    if(p.policy==SeedPolicy::Sequential) j["base_seed"]=p.base_seed;else j["seeds"]=p.explicit_seeds;return j;
}
ReplicationPlan plan_value(const Json& j) {
    fields(j,{"count","policy","generator_version","method_version","confidence"},{"base_seed","seeds"},"replication plan");ReplicationPlan p;
    p.count=static_cast<std::uint32_t>(integer(j.at("count"),max_replications,"replications"));
    p.generator_version=static_cast<std::uint32_t>(integer(j.at("generator_version"),1,"generator version"));
    p.method_version=static_cast<std::uint32_t>(integer(j.at("method_version"),1,"method version"));
    if(!j.at("confidence").is_number()) invalid("confidence","expected 0.95");p.confidence=j.at("confidence").get<double>();
    const auto policy=string(j.at("policy"),"seed policy");
    if(policy=="sequential_v1") {if(!j.contains("base_seed") || j.contains("seeds")) invalid("seeds","sequential requires base_seed only");p.base_seed=integer(j.at("base_seed"),UINT64_MAX,"base seed");}
    else if(policy=="explicit") {p.policy=SeedPolicy::Explicit;p.base_seed=0;if(j.contains("base_seed") || !j.contains("seeds")) invalid("seeds","explicit requires seed list only");
        const auto& seeds=j.at("seeds");if(!seeds.is_array() || seeds.size()>max_replications) invalid("seeds","invalid list");for(const auto& s:seeds) p.explicit_seeds.push_back(integer(s,UINT64_MAX,"seed"));}
    else invalid("policy","unsupported seed policy");(void)replication_seeds(p);return p;
}
Json request_json(const StatisticalRequest& r) {
    validate_statistical_request(r);Json candidates=Json::array();
    for(const auto& c:r.candidates) candidates.push_back({{"id",c.id},{"definition",Json::parse(scenario::serialize_scenario(c.definition))}});
    return {{"schema_version",1},{"experiment_id",r.id},{"mode","paired_initial_capacity"},{"simulation_schema",1},
        {"baseline",Json::parse(scenario::serialize_scenario(r.baseline))},{"candidates",std::move(candidates)},{"objective",objective_name(r.objective)},{"replications",plan_json(r.plan)}};
}
StatisticalRequest request_value(const Json& j) {
    fields(j,{"schema_version","experiment_id","mode","simulation_schema","baseline","candidates","objective","replications"},{},"statistical request");
    if(integer(j.at("schema_version"),1,"schema")!=1 || integer(j.at("simulation_schema"),1,"simulation schema")!=1 ||
        string(j.at("mode"),"mode")!="paired_initial_capacity") invalid("mode","only version 1 paired fresh-start capacity statistics supported");
    StatisticalRequest r;r.id=integer(j.at("experiment_id"),UINT64_MAX,"experiment ID");r.baseline=scenario::parse_scenario(j.at("baseline").dump());
    r.objective=parse_objective(string(j.at("objective"),"objective"));r.plan=plan_value(j.at("replications"));
    const auto& candidates=j.at("candidates");if(!candidates.is_array() || candidates.empty() || candidates.size()>max_statistical_candidates) invalid("candidates","count 1..20 required");
    for(const auto& c:candidates) {fields(c,{"id","definition"},{},"candidate");r.candidates.push_back({integer(c.at("id"),UINT64_MAX,"candidate ID"),scenario::parse_scenario(c.at("definition").dump())});}
    validate_statistical_request(r);return r;
}
Json metrics_json(const warehouse::WarehouseMetrics& m) {
    Json j;
#define RAW(f) j[#f]=m.f
    RAW(elapsed_ticks);RAW(arrived);RAW(started);RAW(completed);RAW(waiting);RAW(in_progress);RAW(total_wait_ticks);RAW(max_wait_ticks);RAW(completed_processing_ticks);RAW(occupied_resource_ticks);RAW(completed_processing_seconds);
#undef RAW
#define RATIO(f) j[#f]=optional(m.f)
    RATIO(throughput_per_hour);RATIO(average_wait_seconds);RATIO(maximum_wait_seconds);RATIO(average_processing_seconds);RATIO(loading_bay_utilization);RATIO(worker_utilization);RATIO(forklift_utilization);
#undef RATIO
    return j;
}
warehouse::WarehouseMetrics metrics_value(const Json& j) {
    fields(j,{"elapsed_ticks","arrived","started","completed","waiting","in_progress","total_wait_ticks","max_wait_ticks","completed_processing_ticks","occupied_resource_ticks","completed_processing_seconds",
        "throughput_per_hour","average_wait_seconds","maximum_wait_seconds","average_processing_seconds","loading_bay_utilization","worker_utilization","forklift_utilization"},{},"replication metrics");
    warehouse::WarehouseMetrics m;
#define RAW(f) m.f=integer(j.at(#f),100000000000000ULL,#f)
    RAW(elapsed_ticks);RAW(arrived);RAW(started);RAW(completed);RAW(waiting);RAW(in_progress);RAW(total_wait_ticks);RAW(max_wait_ticks);RAW(completed_processing_ticks);RAW(occupied_resource_ticks);
#undef RAW
    const auto number=[](const Json& x) {if(!x.is_number()) invalid("metric","expected finite number");const double v=x.get<double>();if(!std::isfinite(v) || v<0) invalid("metric","expected finite nonnegative number");return v;};
#define RATIO(f) if(!j.at(#f).is_null()) m.f=number(j.at(#f))
    RATIO(throughput_per_hour);RATIO(average_wait_seconds);RATIO(maximum_wait_seconds);RATIO(average_processing_seconds);RATIO(loading_bay_utilization);RATIO(worker_utilization);RATIO(forklift_utilization);
#undef RATIO
    m.completed_processing_seconds=number(j.at("completed_processing_seconds"));return m;
}
Json summary_json(const PairedSummary& s) {
    Json j{{"count",s.count},{"defined_pairs",s.defined},{"positive",s.positive},{"negative",s.negative},{"zero",s.zero},{"diagnostic",s.diagnostic}};
#define VALUE(f) j[#f]=optional(s.f)
    VALUE(baseline_mean);VALUE(candidate_mean);VALUE(mean_difference);VALUE(standard_deviation);VALUE(standard_error);VALUE(lower);VALUE(upper);VALUE(minimum);VALUE(maximum);VALUE(median);VALUE(favorable_fraction);VALUE(relative_change_percent);
#undef VALUE
    return j;
}
warehouse::ResourceCounts counts(const scenario::ScenarioDefinition& d) {const auto& c=std::get<scenario::WarehouseConfig>(d.domain);return {c.loading_bays,c.workers,c.forklifts};}
void validate_metrics(const warehouse::WarehouseMetrics& m,const scenario::ScenarioDefinition& d,std::uint32_t arrivals,warehouse::ResourceCounts resources) {
    const auto horizon=scenario::total_ticks(d);const double rate=d.simulation.tick_rate;
    if(m.elapsed_ticks!=horizon || m.arrived!=arrivals || m.completed>m.started || m.started>m.arrived ||
        m.waiting!=m.arrived-m.started || m.in_progress!=m.started-m.completed || m.in_progress>std::min({resources.loading_bays,resources.workers,resources.forklifts}) ||
        m.total_wait_ticks>horizon*m.started || m.max_wait_ticks>horizon || m.max_wait_ticks>m.total_wait_ticks ||
        m.occupied_resource_ticks>horizon*m.started || m.completed_processing_ticks>m.occupied_resource_ticks)
        invalid("metrics","counts or cumulative values conflict with simulation bounds");
    const auto ratio=[&](std::uint64_t x,std::uint64_t n)->std::optional<double>{return n?std::optional<double>{static_cast<double>(x)/static_cast<double>(n)/rate}:std::nullopt;};
    if(m.average_wait_seconds!=ratio(m.total_wait_ticks,m.started) || m.maximum_wait_seconds!=(m.started?std::optional<double>{m.max_wait_ticks/rate}:std::nullopt) ||
        m.completed_processing_seconds!=m.completed_processing_ticks/rate || m.average_processing_seconds!=(m.completed?std::optional<double>{m.completed_processing_seconds/static_cast<double>(m.completed)}:std::nullopt) ||
        m.throughput_per_hour!=std::optional<double>{static_cast<double>(m.completed)*3600.0*rate/horizon}) invalid("metrics","derived ratios conflict with raw values");
    const auto utilization=[&](unsigned capacity)->std::optional<double> {const auto available=horizon*capacity;if(m.occupied_resource_ticks>available) invalid("metrics","occupied time exceeds available capacity");return available?std::optional<double>{static_cast<double>(m.occupied_resource_ticks)/static_cast<double>(available)}:std::nullopt;};
    if(m.loading_bay_utilization!=utilization(resources.loading_bays) || m.worker_utilization!=utilization(resources.workers) || m.forklift_utilization!=utilization(resources.forklifts)) invalid("metrics","utilization conflicts with capacities");
    const auto& c=std::get<scenario::WarehouseConfig>(d.domain);
    const auto minimum=std::uint64_t{c.workload.processing==scenario::ProcessingMode::Constant?c.processing_duration_seconds:c.workload.processing_min_seconds}*d.simulation.tick_rate;
    const auto maximum=std::uint64_t{c.workload.processing==scenario::ProcessingMode::Constant?c.processing_duration_seconds:c.workload.processing_max_seconds}*d.simulation.tick_rate;
    if(m.completed_processing_ticks<m.completed*minimum || m.completed_processing_ticks>m.completed*maximum) invalid("metrics","processing sum outside workload support");
}
Json report_json(const StatisticalReport& r) {
    auto computed=r;aggregate_statistical_report(computed);
    if(computed.candidates!=r.candidates || computed.probes!=r.probes || computed.limitations!=r.limitations) invalid("statistics","derived summaries conflict with raw pairs");
    Json rows=Json::array();std::uint64_t executions=0;
    for(const auto& row:r.replications) {
        auto d=r.request.baseline;d.simulation.random_seed=row.seed;const auto arrivals=scenario::workload::arrival_count(d);
        validate_metrics(row.baseline,d,arrivals,counts(d));Json candidates=Json::array(),probes=Json::array(),differences=Json::array();++executions;
        for(std::size_t i=0;i<row.candidates.size();++i) {
            validate_metrics(row.candidates[i],d,arrivals,counts(r.request.candidates[i].definition));candidates.push_back(metrics_json(row.candidates[i]));++executions;
            Json delta;for(std::size_t m=0;m<statistical_metric_count;++m) {const auto metric=static_cast<StatisticalMetric>(m);const auto b=metric_value(row.baseline,metric),c=metric_value(row.candidates[i],metric);delta[metric_name(metric)]=b&&c?Json(*c-*b):Json(nullptr);}differences.push_back(std::move(delta));
        }
        for(std::size_t p=0;p<3;++p) {
            auto resources=counts(d);const auto resource=static_cast<warehouse::ResourceType>(p);warehouse::set_capacity(resources,resource,warehouse::capacity_for(resources,resource)+1);
            const bool available=std::uint64_t{resources.loading_bays}+resources.workers+resources.forklifts<=100000;
            if(row.probes[p].has_value()!=available) invalid("probes","availability mismatch");
            if(row.probes[p]) {validate_metrics(*row.probes[p],d,arrivals,resources);probes.push_back(metrics_json(*row.probes[p]));
                const auto match=std::find_if(r.request.candidates.begin(),r.request.candidates.end(),[&](const auto& c){return counts(c.definition)==resources;});
                if(match==r.request.candidates.end()) ++executions;
                else if(*row.probes[p]!=row.candidates[static_cast<std::size_t>(match-r.request.candidates.begin())]) invalid("probes","reused candidate metrics mismatch");
            } else probes.push_back(nullptr);
        }
        rows.push_back({{"seed",row.seed},{"baseline",metrics_json(row.baseline)},{"candidates",std::move(candidates)},{"probes",std::move(probes)},{"paired_differences",std::move(differences)}});
    }
    if(executions!=r.simulation_executions) invalid("executions","simulation count mismatch");
    Json summaries=Json::array(),probe_summaries=Json::array();
    for(const auto& c:r.candidates) {Json metrics;for(std::size_t i=0;i<statistical_metric_count;++i) metrics[metric_name(static_cast<StatisticalMetric>(i))]=summary_json(c.metrics[i]);
        summaries.push_back({{"id",c.candidate_id},{"name",c.name},{"rank",c.rank?Json(*c.rank):Json(nullptr)},{"metrics",std::move(metrics)}});}
    for(const auto& p:r.probes) {Json metrics;for(std::size_t i=0;i<statistical_metric_count;++i) metrics[metric_name(static_cast<StatisticalMetric>(i))]=summary_json(p[i]);probe_summaries.push_back(std::move(metrics));}
    auto identity_inputs=request_json(r.request);identity_inputs.erase("experiment_id");std::uint64_t digest=14695981039346656037ULL;
    for(unsigned char c:identity_inputs.dump()) {digest^=c;digest*=1099511628211ULL;}
    char key[17]{};const auto end=std::to_chars(key,key+16,digest,16);const std::string identity{"fnv1a64:"+std::string(key,end.ptr)};
    return {{"statistical_report_version",1},{"status","completed"},{"engine_version","warehouse-workload-v1"},{"experiment_identity",identity},
        {"method",{{"name","paired_student_t_nist_table"},{"version",1},{"confidence",0.95},{"critical_value_decimals",3},{"minimum_interval_pairs",3}}},
        {"request",request_json(r.request)},{"replication_seeds",replication_seeds(r.request.plan)},
        {"simulation_executions",executions},{"replications",std::move(rows)},{"summaries",std::move(summaries)},{"probe_summaries",std::move(probe_summaries)},{"limitations",r.limitations}};
}
std::string number(std::optional<double> x) {if(!x) return {};char buffer[64];const auto result=std::to_chars(buffer,buffer+64,*x,std::chars_format::general,std::numeric_limits<double>::max_digits10);if(result.ec!=std::errc{}) throw std::runtime_error("CSV number conversion");return {buffer,result.ptr};}
std::string escape(std::string_view x) {std::string out="\"";for(char c:x) {if(c=='"') out+='"';out+=c;}return out+'"';}
}
std::string serialize_statistical_request(const StatisticalRequest& r) {return request_json(r).dump();}
StatisticalRequest parse_statistical_request(std::string_view text) {return request_value(parse_json(text,max_report_bytes));}
std::string serialize_statistical_report(const StatisticalReport& r) {auto text=report_json(r).dump();if(text.size()>max_report_bytes) throw std::length_error("Statistical report exceeds byte budget");return text;}
StatisticalReport parse_statistical_report(std::string_view text) {
    const auto j=parse_json(text,max_report_bytes);fields(j,{"statistical_report_version","status","engine_version","experiment_identity","method","request","replication_seeds","simulation_executions","replications","summaries","probe_summaries","limitations"},{},"statistical report");
    if(integer(j.at("statistical_report_version"),1,"report version")!=1 || string(j.at("status"),"status")!="completed") invalid("report","only completed version 1 reports supported");
    StatisticalReport r;r.request=request_value(j.at("request"));r.simulation_executions=integer(j.at("simulation_executions"),2400,"simulation executions");
    const auto& rows=j.at("replications");if(!rows.is_array() || rows.size()!=r.request.plan.count) invalid("replications","missing paired runs");
    for(const auto& row:rows) {fields(row,{"seed","baseline","candidates","probes","paired_differences"},{},"replication");ReplicationOutcome out;out.seed=integer(row.at("seed"),UINT64_MAX,"seed");out.baseline=metrics_value(row.at("baseline"));
        const auto& candidates=row.at("candidates");const auto& probes=row.at("probes");if(!candidates.is_array() || candidates.size()!=r.request.candidates.size() || !probes.is_array() || probes.size()!=3) invalid("replication","invalid candidate/probe count");
        for(const auto& c:candidates) out.candidates.push_back(metrics_value(c));for(std::size_t p=0;p<3;++p) if(!probes[p].is_null()) out.probes[p]=metrics_value(probes[p]);r.replications.push_back(std::move(out));}
    aggregate_statistical_report(r);if(report_json(r)!=j) invalid("report","saved statistics or provenance conflict with raw paired observations");return r;
}
StatisticalReport replay_statistical_report(std::string_view text) {const auto saved=parse_statistical_report(text);auto replayed=run_statistical_experiment(saved.request);if(serialize_statistical_report(saved)!=serialize_statistical_report(replayed)) throw std::runtime_error("Statistical replay diverged");return replayed;}
void save_statistical_report(const std::filesystem::path& path,const StatisticalReport& report) {detail::save_output(path,serialize_statistical_report(report));}
void save_statistical_csv(const std::filesystem::path& path,const StatisticalReport& report,bool per_seed) {detail::save_output(path,serialize_statistical_csv(report,per_seed));}
std::string serialize_statistical_csv(const StatisticalReport& r,bool per_seed) {
    const auto identity=report_json(r).at("experiment_identity").get<std::string>();
    std::string text=per_seed?"experiment_identity,experiment_id,candidate_id,candidate,seed,metric,unit,baseline,candidate_value,paired_difference\r\n":
        "experiment_identity,experiment_id,candidate_id,candidate,metric,unit,n,defined_pairs,baseline_mean,candidate_mean,mean_paired_difference,sd_paired_difference,se_paired_difference,ci95_lower,ci95_upper,min_difference,max_difference,median_difference,positive,negative,zero,favorable_fraction,relative_change_in_means_percent,point_rank,diagnostic\r\n";
    for(std::size_t c=0;c<r.candidates.size();++c) for(std::size_t m=0;m<statistical_metric_count;++m) {
        const auto metric=static_cast<StatisticalMetric>(m);const auto& candidate=r.candidates[c];
        const auto prefix=identity+','+std::to_string(r.request.id)+','+std::to_string(candidate.candidate_id)+','+escape(candidate.name)+',';
        if(per_seed) for(const auto& row:r.replications) {const auto b=metric_value(row.baseline,metric),v=metric_value(row.candidates[c],metric);text+=prefix+std::to_string(row.seed)+','+std::string(metric_name(metric))+','+std::string(metric_unit(metric))+','+number(b)+','+number(v)+','+number(b&&v?std::optional<double>{*v-*b}:std::nullopt)+"\r\n";}
        else {const auto& s=candidate.metrics[m];text+=prefix+std::string(metric_name(metric))+','+std::string(metric_unit(metric))+','+std::to_string(s.count)+','+std::to_string(s.defined);
            for(const auto& x:{s.baseline_mean,s.candidate_mean,s.mean_difference,s.standard_deviation,s.standard_error,s.lower,s.upper,s.minimum,s.maximum,s.median}) text+=','+number(x);
            text+=','+std::to_string(s.positive)+','+std::to_string(s.negative)+','+std::to_string(s.zero)+','+number(s.favorable_fraction)+','+number(s.relative_change_percent)+','+(candidate.rank?std::to_string(*candidate.rank):"")+','+escape(s.diagnostic)+"\r\n";}
    }return text;
}
StatisticalRequest load_statistical_experiment(const std::filesystem::path& path) {
    auto j=parse_json(load_text(path,max_experiment_bytes),max_experiment_bytes);
    fields(j,{"schema_version","mode","scenario","objective","candidates","replications"},{},"statistical experiment");
    if(integer(j.at("schema_version"),2,"schema version")!=2 || string(j.at("mode"),"mode")!="paired_statistical") invalid("mode","unsupported statistical experiment mode");
    const auto plan=plan_value(j.at("replications"));j.erase("replications");j.erase("mode");j["schema_version"]=1;
    const auto spec=parse_experiment(j.dump());const auto directory=std::filesystem::canonical(path).parent_path();const auto input=std::filesystem::canonical(directory/spec.scenario);const auto relative=input.lexically_relative(directory);
    if(relative.empty() || relative.is_absolute() || *relative.begin()==".." || !std::filesystem::is_regular_file(input)) invalid("scenario","must resolve inside experiment directory");
    StatisticalRequest r;r.baseline=scenario::load_scenario(input);r.objective=spec.objective;r.plan=plan;
    for(std::size_t i=0;i<spec.candidates.size();++i) {auto d=r.baseline;d.name=spec.candidates[i].name;auto& c=std::get<scenario::WarehouseConfig>(d.domain);const auto resources=spec.candidates[i].resources;
        c.loading_bays=resources.loading_bays;c.workers=resources.workers;c.forklifts=resources.forklifts;r.candidates.push_back({i+1,std::move(d)});}
    validate_statistical_request(r);return r;
}
}
