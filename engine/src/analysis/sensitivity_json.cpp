#include "report_value.hpp"
#include <simforge/analysis/sensitivity.hpp>
#include "statistical_json_shared.hpp"
#include <algorithm>
#include <charconv>
#include <limits>

namespace simforge::analysis {
namespace {
using namespace scenario::detail;
Json opt(std::optional<double> v) {return v?Json(*v):Json(nullptr);}
StatisticalMetric metric(std::string_view name) {for(std::size_t i=0;i<statistical_metric_count;++i)if(metric_name(static_cast<StatisticalMetric>(i))==name)return static_cast<StatisticalMetric>(i);throw std::invalid_argument("Unsupported sensitivity objective");}
Json request_json(const SensitivityRequest& r) {
    validate_sensitivity_request(r);return {{"schema_version",1},{"experiment_id",r.id},{"source_id",r.source_id},{"source",Json::parse(scenario::serialize_scenario(r.source))},
        {"parameter",parameter_name(r.parameter)},{"values",r.values},{"objective",metric_name(r.objective)},{"multi_seed",r.multi_seed},
        {"replications",detail::statistical_plan_json(r.plan)},{"practical_threshold",r.practical_threshold}};
}
SensitivityRequest request_value(const Json& j) {
    fields(j,{"schema_version","experiment_id","source_id","source","parameter","values","objective","multi_seed","replications","practical_threshold"},{},"sensitivity request");
    SensitivityRequest r;r.schema_version=static_cast<std::uint32_t>(integer(j.at("schema_version"),1,"schema_version"));r.id=integer(j.at("experiment_id"),UINT64_MAX,"experiment_id");r.source_id=integer(j.at("source_id"),UINT64_MAX,"source_id");
    r.source=scenario::parse_scenario(j.at("source").dump());r.parameter=parse_parameter(string(j.at("parameter"),"parameter"));
    const auto& values=j.at("values");if(!values.is_array()||values.size()>max_sweep_points)invalid("values","expected bounded array");for(const auto& v:values)r.values.push_back(static_cast<std::uint32_t>(integer(v,UINT32_MAX,"sweep value")));
    r.objective=metric(string(j.at("objective"),"objective"));if(!j.at("multi_seed").is_boolean())invalid("multi_seed","expected boolean");r.multi_seed=j.at("multi_seed").get<bool>();
    r.plan=detail::statistical_plan_value(j.at("replications"));if(!j.at("practical_threshold").is_number())invalid("threshold","expected number");r.practical_threshold=j.at("practical_threshold").get<double>();validate_sensitivity_request(r);return r;
}
Json report_json(const SensitivityReport& input,bool aggregated=false) {
    std::optional<SensitivityReport> computed;if(!aggregated){computed=input;aggregate_sensitivity_report(*computed);}const auto& r=aggregated?input:*computed;Json rows=Json::array(),points=Json::array(),probes=Json::array();std::uint64_t executions=0;
    for(const auto& row:r.replications) {
        auto source=r.request.source;source.simulation.random_seed=row.seed;detail::validate_statistical_metrics(row.baseline,source);++executions;
        Json outcomes=Json::array(),probe_values=Json::array();
        for(std::size_t i=0;i<r.points.size();++i) {detail::validate_statistical_metrics(row.points[i],tested_definition(r.request,r.points[i].value,row.seed));
            if(r.points[i].baseline) {if(row.points[i]!=row.baseline)invalid("baseline point","must exactly equal source measurement");}else ++executions;outcomes.push_back(detail::statistical_metrics_json(row.points[i]));}
        const auto& c=std::get<scenario::WarehouseConfig>(source.domain);
        for(std::size_t p=0;p<3;++p) {
            const auto parameter=p==0?SweepParameter::LoadingBays:p==1?SweepParameter::Workers:SweepParameter::Forklifts;
            const bool possible=std::uint64_t{c.loading_bays}+c.workers+c.forklifts<100000;
            if(row.probes[p].has_value()!=possible)invalid("probe","availability conflicts with resource bounds");
            if(row.probes[p]) {auto probe_request=r.request;probe_request.parameter=parameter;const auto v=parameter_value(source,parameter)+1;
                detail::validate_statistical_metrics(*row.probes[p],tested_definition(probe_request,v,row.seed));
                auto existing=std::find(r.request.values.begin(),r.request.values.end(),v);
                if(parameter==r.request.parameter&&existing!=r.request.values.end()) {if(*row.probes[p]!=row.points[static_cast<std::size_t>(existing-r.request.values.begin())])invalid("probe","reused point mismatch");}else ++executions;
                probe_values.push_back(detail::statistical_metrics_json(*row.probes[p]));}else probe_values.push_back(nullptr);
        }
        rows.push_back({{"seed",row.seed},{"baseline",detail::statistical_metrics_json(row.baseline)},{"points",std::move(outcomes)},{"probes",std::move(probe_values)}});
    }
    if(executions!=r.simulation_executions)invalid("execution count","inconsistent with distinct configurations");
    Json baseline_means=Json::object();for(std::size_t m=0;m<statistical_metric_count;++m)baseline_means[metric_name(static_cast<StatisticalMetric>(m))]=opt(r.baseline_means[m]);
    for(const auto& p:r.points) {Json baseline=Json::object(),adjacent=Json::object(),means=Json::object(),mean_intervals=Json::object();for(std::size_t m=0;m<statistical_metric_count;++m) {
        means[metric_name(static_cast<StatisticalMetric>(m))]=opt(p.means[m]);mean_intervals[metric_name(static_cast<StatisticalMetric>(m))]={{"lower_95",opt(p.mean_lower[m])},{"upper_95",opt(p.mean_upper[m])}};
        baseline[metric_name(static_cast<StatisticalMetric>(m))]=detail::statistical_summary_json(p.against_baseline[m]);if(p.value!=r.points.front().value)adjacent[metric_name(static_cast<StatisticalMetric>(m))]=detail::statistical_summary_json(p.adjacent[m]);}
        points.push_back({{"id",p.id},{"value",p.value},{"is_baseline",p.baseline},{"means",std::move(means)},{"mean_intervals",std::move(mean_intervals)},{"baseline_effects",std::move(baseline)},{"adjacent_effects",std::move(adjacent)},{"marginal_per_unit",opt(p.marginal_per_unit)},{"interpretation",p.marginal_interpretation}});}
    for(const auto& p:r.probes) {Json summaries=Json::object();for(std::size_t m=0;m<statistical_metric_count;++m)summaries[metric_name(static_cast<StatisticalMetric>(m))]=detail::statistical_summary_json(p[m]);probes.push_back(std::move(summaries));}
    return {{"sensitivity_report_version",1},{"status","completed"},{"engine_version","warehouse-workload-v1"},{"request",request_json(r.request)},
        {"simulation_executions",r.simulation_executions},{"replications",std::move(rows)},{"baseline_means",std::move(baseline_means)},{"points",std::move(points)},{"probes",std::move(probes)},{"best_point",r.best_point?Json(*r.best_point):Json(nullptr)},{"limitations",r.limitations}};
}
std::string number(std::optional<double> x) {if(!x)return {};char b[64];auto result=std::to_chars(b,b+64,*x,std::chars_format::general,std::numeric_limits<double>::max_digits10);if(result.ec!=std::errc{})throw std::runtime_error("CSV conversion failed");return {b,result.ptr};}
}
std::string serialize_sensitivity_request(const SensitivityRequest& r) {return request_json(r).dump();}
void save_sensitivity_report(const std::filesystem::path& path,const SensitivityReport& r) {detail::save_output(path,serialize_sensitivity_report(r));}
void save_sensitivity_csv(const std::filesystem::path& path,const SensitivityReport& r,SensitivityCsv kind) {detail::save_output(path,serialize_sensitivity_csv(r,kind));}
SensitivityRequest parse_sensitivity_request(std::string_view text) {return request_value(parse_json(text,max_report_bytes));}
std::string serialize_sensitivity_report(const SensitivityReport& r) {auto text=report_json(r).dump();if(text.size()>max_report_bytes)throw std::length_error("Sensitivity report byte limit");return text;}
SensitivityReport parse_sensitivity_report(std::string_view text) {return detail::sensitivity_report_value(parse_json(text,max_report_bytes));}
SensitivityReport detail::sensitivity_report_value(const Json& j) {
    fields(j,{"sensitivity_report_version","status","engine_version","request","simulation_executions","replications","baseline_means","points","probes","best_point","limitations"},{},"sensitivity report");
    SensitivityReport r;r.request=request_value(j.at("request"));r.simulation_executions=integer(j.at("simulation_executions"),100000,"executions");
    const auto& rows=j.at("replications");if(!rows.is_array()||rows.size()!=r.request.plan.count)invalid("replications","incomplete report");
    for(const auto& row:rows) {fields(row,{"seed","baseline","points","probes"},{},"sensitivity replication");SensitivityReplication out;out.seed=integer(row.at("seed"),UINT64_MAX,"seed");out.baseline=detail::statistical_metrics_value(row.at("baseline"));
        if(!row.at("points").is_array()||row.at("points").size()!=r.request.values.size()||!row.at("probes").is_array()||row.at("probes").size()!=3)invalid("outcomes","invalid shape");
        for(const auto& p:row.at("points"))out.points.push_back(detail::statistical_metrics_value(p));for(std::size_t p=0;p<3;++p)if(!row.at("probes")[p].is_null())out.probes[p]=detail::statistical_metrics_value(row.at("probes")[p]);r.replications.push_back(std::move(out));}
    aggregate_sensitivity_report(r);if(report_json(r,true)!=j)invalid("sensitivity report","provenance or derived data mismatch");return r;
}
SensitivityRequest load_sensitivity_experiment(const std::filesystem::path& path) {
    auto j=parse_json(load_text(path,max_experiment_bytes),max_experiment_bytes);
    fields(j,{"schema_version","mode","scenario","parameter","objective","multi_seed","replications","practical_threshold"},{"values","range"},"sensitivity specification");
    if(integer(j.at("schema_version"),3,"schema_version")!=3||string(j.at("mode"),"mode")!="sensitivity")invalid("mode","expected sensitivity schema 3");
    ExperimentDefinition spec;spec.scenario=string(j.at("scenario"),"scenario");spec.candidates={{"Path validation",{0,0,0}}};validate_experiment(spec);
    const auto directory=std::filesystem::canonical(path).parent_path(),input=std::filesystem::canonical(directory/spec.scenario);const auto relative=input.lexically_relative(directory);
    if(relative.empty()||relative.is_absolute()||*relative.begin()==".."||!std::filesystem::is_regular_file(input))invalid("scenario","must resolve inside specification directory");
    if(j.contains("values")==j.contains("range"))invalid("values","choose values or range");
    if(j.contains("range")) {const auto& range=j.at("range");fields(range,{"minimum","maximum","step"},{},"range");j["values"]=sweep_range(static_cast<std::uint32_t>(integer(range.at("minimum"),UINT32_MAX,"minimum")),static_cast<std::uint32_t>(integer(range.at("maximum"),UINT32_MAX,"maximum")),static_cast<std::uint32_t>(integer(range.at("step"),UINT32_MAX,"step")));j.erase("range");}
    j.erase("mode");j.erase("scenario");j["schema_version"]=1;j["experiment_id"]=0;j["source_id"]=0;j["source"]=Json::parse(scenario::serialize_scenario(scenario::load_scenario(input)));return request_value(j);
}
std::string serialize_sensitivity_csv(const SensitivityReport& report,SensitivityCsv kind) {
    auto r=report;aggregate_sensitivity_report(r);std::string text;
    if(kind==SensitivityCsv::Seeds) {text="value,seed,metric,baseline,tested,delta\r\n";for(std::size_t i=0;i<r.points.size();++i)for(const auto& row:r.replications)for(std::size_t m=0;m<statistical_metric_count;++m) {
        const auto metric=static_cast<StatisticalMetric>(m);auto a=metric_value(row.baseline,metric),b=metric_value(row.points[i],metric);text+=std::to_string(r.points[i].value)+','+std::to_string(row.seed)+','+std::string(metric_name(metric))+','+number(a)+','+number(b)+','+number(a&&b?std::optional<double>{*b-*a}:std::nullopt)+"\r\n";}}
    else {text=kind==SensitivityCsv::Summary?"value,metric,mean_baseline,mean_tested,mean_lower_95,mean_upper_95,delta,lower_95,upper_95,n,defined,favorable,unfavorable,zero\r\n":"from,to,metric,delta,per_parameter_unit,lower_95,upper_95,n,defined,favorable,unfavorable,zero\r\n";
        for(std::size_t i=0;i<r.points.size();++i) {if(kind==SensitivityCsv::Marginal&&!i)continue;const auto& p=r.points[i];for(std::size_t m=0;m<statistical_metric_count;++m) {
            const auto metric=static_cast<StatisticalMetric>(m);const auto& s=kind==SensitivityCsv::Summary?p.against_baseline[m]:p.adjacent[m];
            text+=kind==SensitivityCsv::Summary?std::to_string(p.value)+','+std::string(metric_name(metric))+','+number(r.baseline_means[m])+','+number(p.means[m]):std::to_string(r.points[i-1].value)+','+std::to_string(p.value)+','+std::string(metric_name(metric));
            if(kind==SensitivityCsv::Summary)text+=','+number(p.mean_lower[m])+','+number(p.mean_upper[m]);text+=','+number(s.mean_difference);if(kind==SensitivityCsv::Marginal)text+=','+number(s.mean_difference?std::optional<double>{*s.mean_difference/(p.value-r.points[i-1].value)}:std::nullopt);
            text+=','+number(s.lower)+','+number(s.upper)+','+std::to_string(s.count)+','+std::to_string(s.defined)+','+std::to_string(higher_is_better(metric)?s.positive:s.negative)+','+std::to_string(higher_is_better(metric)?s.negative:s.positive)+','+std::to_string(s.zero)+"\r\n";
        }}
    }return text;
}
}
