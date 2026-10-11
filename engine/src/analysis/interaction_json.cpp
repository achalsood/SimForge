#include <simforge/analysis/interaction.hpp>
#include "statistical_json_shared.hpp"
#include <charconv>
#include <limits>
#include <tuple>

namespace simforge::analysis {
namespace {
using namespace scenario::detail;
Json opt(std::optional<double> v) {return v?Json(*v):Json(nullptr);}
StatisticalMetric metric(std::string_view name) {for(std::size_t i=0;i<statistical_metric_count;++i)if(metric_name(static_cast<StatisticalMetric>(i))==name)return static_cast<StatisticalMetric>(i);throw std::invalid_argument("Unsupported interaction objective");}
Json request_json(const InteractionRequest& r) {
    validate_interaction_request(r);return {{"schema_version",1},{"experiment_id",r.id},{"source_id",r.source_id},{"source",Json::parse(scenario::serialize_scenario(r.source))},
        {"x_parameter",parameter_name(r.x)},{"y_parameter",parameter_name(r.y)},{"x_values",r.x_values},{"y_values",r.y_values},
        {"original_x",parameter_value(r.source,r.x)},{"original_y",parameter_value(r.source,r.y)},
        {"objective",metric_name(r.objective)},{"higher_is_better",higher_is_better(r.objective)},{"multi_seed",r.multi_seed},{"replications",detail::statistical_plan_json(r.plan)},
        {"comparison_mode","fresh_start"},{"engine_version","warehouse-workload-v1"},{"report_version",1},{"method","local-additive-paired-t-v1"},
        {"limits",{{"axis",r.limits.axis},{"cells",r.limits.cells},{"executions",r.limits.executions},{"arrival_work",r.limits.arrival_work}}}};
}
InteractionRequest request_value(const Json& j) {
    fields(j,{"schema_version","experiment_id","source_id","source","x_parameter","y_parameter","x_values","y_values","original_x","original_y","objective","higher_is_better","multi_seed","replications","comparison_mode","engine_version","report_version","method","limits"},{},"interaction request");
    InteractionRequest r;r.schema_version=static_cast<std::uint32_t>(integer(j.at("schema_version"),1,"schema_version"));r.id=integer(j.at("experiment_id"),UINT64_MAX,"experiment_id");r.source_id=integer(j.at("source_id"),UINT64_MAX,"source_id");
    r.source=scenario::parse_scenario(j.at("source").dump());r.x=parse_parameter(string(j.at("x_parameter"),"x_parameter"));r.y=parse_parameter(string(j.at("y_parameter"),"y_parameter"));
    for(const auto& [name,axis]:{std::pair{"x_values",&r.x_values},std::pair{"y_values",&r.y_values}}) {const auto& values=j.at(name);if(!values.is_array()||values.size()>max_interaction_axis)invalid(name,"expected bounded array");for(const auto& v:values)axis->push_back(static_cast<std::uint32_t>(integer(v,UINT32_MAX,"factor value")));}
    r.objective=metric(string(j.at("objective"),"objective"));if(!j.at("multi_seed").is_boolean())invalid("multi_seed","expected boolean");r.multi_seed=j.at("multi_seed").get<bool>();r.plan=detail::statistical_plan_value(j.at("replications"));
    const auto& limits=j.at("limits");fields(limits,{"axis","cells","executions","arrival_work"},{},"interaction limits");r.limits.axis=static_cast<std::uint32_t>(integer(limits.at("axis"),10,"axis"));r.limits.cells=static_cast<std::uint32_t>(integer(limits.at("cells"),100,"cells"));r.limits.executions=integer(limits.at("executions"),max_interaction_executions,"executions");r.limits.arrival_work=integer(limits.at("arrival_work"),max_statistical_arrival_work,"arrival_work");
    validate_interaction_request(r);if(request_json(r)!=j)invalid("request","unsupported provenance, baseline values or direction");return r;
}
Json report_json(const InteractionReport& input) {
    auto r=input;aggregate_interaction_report(r);Json rows=Json::array(),cells=Json::array(),rectangles=Json::array();
    if(r.simulation_executions!=interaction_execution_count(r.request))invalid("executions","inconsistent with distinct configurations and baseline reuse");
    for(const auto& row:r.replications) {
        auto baseline=r.request.source;baseline.simulation.random_seed=row.seed;detail::validate_statistical_metrics(row.baseline,baseline);Json outcomes=Json::array(),deltas=Json::array();
        for(std::size_t i=0;i<r.cells.size();++i) {const auto& c=r.cells[i];detail::validate_statistical_metrics(row.cells[i],tested_definition(r.request,c.x,c.y,row.seed));outcomes.push_back(detail::statistical_metrics_json(row.cells[i]));Json delta=Json::object();
            for(std::size_t m=0;m<statistical_metric_count;++m) {auto metric=static_cast<StatisticalMetric>(m);auto a=metric_value(row.baseline,metric),b=metric_value(row.cells[i],metric);delta[metric_name(metric)]=opt(a&&b?std::optional<double>{*b-*a}:std::nullopt);}deltas.push_back(delta);}
        rows.push_back({{"seed",row.seed},{"baseline",detail::statistical_metrics_json(row.baseline)},{"cells",outcomes},{"paired_differences",deltas}});
    }
    Json baseline=Json::object();for(std::size_t m=0;m<statistical_metric_count;++m)baseline[metric_name(static_cast<StatisticalMetric>(m))]=opt(r.baseline_means[m]);
    for(const auto& c:r.cells) {Json means=Json::object(),effects=Json::object();for(std::size_t m=0;m<statistical_metric_count;++m){const auto name=metric_name(static_cast<StatisticalMetric>(m));means[name]=opt(c.means[m]);effects[name]=detail::statistical_summary_json(c.effects[m]);}
        cells.push_back({{"id",c.id},{"x_index",c.x_index},{"y_index",c.y_index},{"x",c.x},{"y",c.y},{"is_baseline",c.baseline},{"means",means},{"baseline_effects",effects}});}
    for(const auto& rectangle:r.interactions) {Json summaries=Json::object(),contrasts=Json::array();for(std::size_t m=0;m<statistical_metric_count;++m)summaries[metric_name(static_cast<StatisticalMetric>(m))]=detail::statistical_summary_json(rectangle.effects[m]);
        for(std::size_t s=0;s<r.replications.size();++s) {Json raw=Json::object(),values=Json::object();for(std::size_t m=0;m<statistical_metric_count;++m){const auto metric=static_cast<StatisticalMetric>(m);raw[metric_name(metric)]=opt(rectangle.per_seed[s][m]);Json four=Json::array();for(auto index:rectangle.cells)four.push_back(opt(metric_value(r.replications[s].cells[index],metric)));values[metric_name(metric)]=four;}
            contrasts.push_back({{"seed",r.replications[s].seed},{"four_values_00_10_01_11",values},{"contrasts",raw}});}
        rectangles.push_back({{"x_index",rectangle.x_index},{"y_index",rectangle.y_index},{"x0",rectangle.x0},{"x1",rectangle.x1},{"y0",rectangle.y0},{"y1",rectangle.y1},{"source_cells",rectangle.cells},{"effects",summaries},{"per_seed",contrasts},{"interpretation",rectangle.interpretation}});}
    return {{"interaction_report_version",1},{"status","completed"},{"request",request_json(r.request)},{"grid_order","y-major-x-first"},{"simulation_executions",r.simulation_executions},{"replications",rows},{"baseline_means",baseline},{"cells",cells},{"interactions",rectangles},{"best_cells",r.best_cells},{"limitations",r.limitations}};
}
std::string number(std::optional<double> v) {if(!v)return {};char b[64];const auto result=std::to_chars(b,b+64,*v,std::chars_format::general,std::numeric_limits<double>::max_digits10);if(result.ec!=std::errc{})throw std::runtime_error("CSV conversion failed");return {b,result.ptr};}
}
std::string serialize_interaction_request(const InteractionRequest& r) {return request_json(r).dump();}
InteractionRequest parse_interaction_request(std::string_view text) {return request_value(parse_json(text,max_report_bytes));}
std::string serialize_interaction_report(const InteractionReport& r) {auto text=report_json(r).dump();if(text.size()>max_report_bytes)throw std::length_error("Interaction report byte budget exceeded");return text;}
InteractionReport parse_interaction_report(std::string_view text) {
    const auto j=parse_json(text,max_report_bytes);fields(j,{"interaction_report_version","status","request","grid_order","simulation_executions","replications","baseline_means","cells","interactions","best_cells","limitations"},{},"interaction report");
    InteractionReport r;r.request=request_value(j.at("request"));r.simulation_executions=integer(j.at("simulation_executions"),max_interaction_executions,"executions");const auto& rows=j.at("replications");if(!rows.is_array()||rows.size()!=r.request.plan.count)invalid("replications","invalid count");
    for(const auto& row:rows){fields(row,{"seed","baseline","cells","paired_differences"},{},"interaction replication");InteractionReplication out;out.seed=integer(row.at("seed"),UINT64_MAX,"seed");out.baseline=detail::statistical_metrics_value(row.at("baseline"));const auto& cells=row.at("cells");if(!cells.is_array()||cells.size()!=r.request.x_values.size()*r.request.y_values.size())invalid("cells","invalid grid shape");for(const auto& c:cells)out.cells.push_back(detail::statistical_metrics_value(c));r.replications.push_back(std::move(out));}
    aggregate_interaction_report(r);if(report_json(r)!=j)invalid("interaction report","raw metrics, provenance or derived data mismatch");return r;
}
InteractionRequest load_interaction_experiment(const std::filesystem::path& path) {
    auto j=parse_json(load_text(path,max_experiment_bytes),max_experiment_bytes);
    fields(j,{"schema_version","mode","scenario","x_parameter","y_parameter","objective","multi_seed","replications"},{"x_values","y_values","x_range","y_range","limits"},"interaction specification");
    if(integer(j.at("schema_version"),4,"schema_version")!=4||string(j.at("mode"),"mode")!="interaction")invalid("mode","expected interaction schema 4");
    ExperimentDefinition spec;spec.scenario=string(j.at("scenario"),"scenario");spec.candidates={{"Path validation",{0,0,0}}};validate_experiment(spec);
    const auto directory=std::filesystem::canonical(path).parent_path(),input=std::filesystem::canonical(directory/spec.scenario),relative=input.lexically_relative(directory);
    if(relative.empty()||relative.is_absolute()||*relative.begin()==".."||!std::filesystem::is_regular_file(input))invalid("scenario","must resolve inside specification directory");
    InteractionRequest r;r.source=scenario::load_scenario(input);r.x=parse_parameter(string(j.at("x_parameter"),"x"));r.y=parse_parameter(string(j.at("y_parameter"),"y"));r.objective=metric(string(j.at("objective"),"objective"));
    if(!j.at("multi_seed").is_boolean())invalid("multi_seed","expected boolean");r.multi_seed=j.at("multi_seed").get<bool>();r.plan=detail::statistical_plan_value(j.at("replications"));
    for(const auto& [axis,values_name,range_name]:{std::tuple{&r.x_values,"x_values","x_range"},std::tuple{&r.y_values,"y_values","y_range"}}) {
        if(j.contains(values_name)==j.contains(range_name))invalid(values_name,"choose values or range");
        if(j.contains(range_name)){const auto& range=j.at(range_name);fields(range,{"minimum","maximum","step"},{},"axis range");*axis=sweep_range(static_cast<std::uint32_t>(integer(range.at("minimum"),UINT32_MAX,"minimum")),static_cast<std::uint32_t>(integer(range.at("maximum"),UINT32_MAX,"maximum")),static_cast<std::uint32_t>(integer(range.at("step"),UINT32_MAX,"step")));}
        else {const auto& values=j.at(values_name);if(!values.is_array()||values.size()>max_interaction_axis)invalid(values_name,"bounded array required");for(const auto& v:values)axis->push_back(static_cast<std::uint32_t>(integer(v,UINT32_MAX,"value")));}}
    if(j.contains("limits")){auto canonical=request_json(r);canonical["limits"]=j.at("limits");r=request_value(canonical);}validate_interaction_request(r);return r;
}
std::string serialize_interaction_csv(const InteractionReport& input,InteractionCsv kind) {
    // Validate authoritative raw measurements and execution counts as for JSON export.
    (void)report_json(input);auto r=input;aggregate_interaction_report(r);std::string text;
    const bool rectangles=kind==InteractionCsv::Interactions||kind==InteractionCsv::InteractionSeeds;
    const bool seeds=kind==InteractionCsv::Seeds||kind==InteractionCsv::InteractionSeeds;
    text=rectangles?"experiment_id,x_index,y_index,x0,x1,y0,y1,metric,":"experiment_id,cell_id,x_index,y_index,x,y,metric,";
    text+=seeds?(rectangles?"seed,value00,value10,value01,value11,contrast\r\n":"seed,baseline,tested,delta\r\n"):
        (rectangles?"mean_interaction,lower_95,upper_95,n,defined,sd,se,positive,negative,zero\r\n":"mean_baseline,mean_tested,delta,lower_95,upper_95,n,defined,sd,se,favorable,unfavorable,zero\r\n");
    for(std::size_t i=0;i<(rectangles?r.interactions.size():r.cells.size());++i)for(std::size_t m=0;m<statistical_metric_count;++m) {
        const auto metric=static_cast<StatisticalMetric>(m);std::string prefix=std::to_string(r.request.id)+',';
        if(rectangles){const auto& a=r.interactions[i];prefix+=std::to_string(a.x_index)+','+std::to_string(a.y_index)+','+std::to_string(a.x0)+','+std::to_string(a.x1)+','+std::to_string(a.y0)+','+std::to_string(a.y1)+',';}
        else {const auto& c=r.cells[i];prefix+=std::to_string(c.id)+','+std::to_string(c.x_index)+','+std::to_string(c.y_index)+','+std::to_string(c.x)+','+std::to_string(c.y)+',';}
        prefix+=std::string(metric_name(metric))+',';
        if(seeds)for(std::size_t s=0;s<r.replications.size();++s) {const auto& row=r.replications[s];text+=prefix+std::to_string(row.seed)+',';
            if(rectangles){const auto& a=r.interactions[i];for(auto c:a.cells)text+=number(metric_value(row.cells[c],metric))+',';text+=number(a.per_seed[s][m]);}
            else {auto a=metric_value(row.baseline,metric),b=metric_value(row.cells[i],metric);text+=number(a)+','+number(b)+','+number(a&&b?std::optional<double>{*b-*a}:std::nullopt);}text+="\r\n";}
        else {const auto& s=rectangles?r.interactions[i].effects[m]:r.cells[i].effects[m];text+=prefix;if(!rectangles)text+=number(r.baseline_means[m])+','+number(r.cells[i].means[m])+',';
            text+=number(s.mean_difference)+','+number(s.lower)+','+number(s.upper)+','+std::to_string(s.count)+','+std::to_string(s.defined)+','+number(s.standard_deviation)+','+number(s.standard_error)+','+std::to_string(rectangles||higher_is_better(metric)?s.positive:s.negative)+','+std::to_string(rectangles||higher_is_better(metric)?s.negative:s.positive)+','+std::to_string(s.zero)+"\r\n";}
    }return text;
}
void save_interaction_report(const std::filesystem::path& path,const InteractionReport& r) {detail::save_output(path,serialize_interaction_report(r));}
void save_interaction_csv(const std::filesystem::path& path,const InteractionReport& r,InteractionCsv kind) {detail::save_output(path,serialize_interaction_csv(r,kind));}
}
