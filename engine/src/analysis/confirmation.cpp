#include "report_value.hpp"
#include <simforge/analysis/confirmation.hpp>
#include "../scenario/json_io.hpp"
#include "comparison_json.hpp"
#include <set>
#include <cmath>
#include <sstream>

namespace simforge::analysis {
namespace {
using namespace scenario::detail;
void text_bound(const std::string& s,std::size_t n) {
    if(s.size()>n || s.find('\0')!=std::string::npos) throw std::invalid_argument("Invalid confirmation metadata");
}
Json request_json(const ConfirmationRequest& r) {
    validate_confirmation_request(r);
    return {{"schema_version",5},{"mode","independent_confirmation"},{"version",r.version},
        {"source_study_id",r.source_study_id},{"source_type",r.source_type},{"source_title",r.source_title},
        {"source_completed",r.source_completed},{"source_request_id",r.source_request_id},{"selection_id",r.selection_id},
        {"engine_version",r.engine_version},{"generator_version",r.generator_version},
        {"paired",Json::parse(serialize_statistical_request(r.paired))},{"objective",metric_name(r.objective)},
        {"exploratory_seeds",r.exploratory_seeds},{"confirmation_seeds",replication_seeds(r.paired.plan)},
        {"seed_derivation",r.paired.plan.policy==SeedPolicy::Sequential?"sequential_v1":"explicit"},
        {"disjointness_validated",true},{"exploratory_difference",r.exploratory_difference?Json(*r.exploratory_difference):Json(nullptr)},
        {"changed_inputs",r.changed_inputs},{"additional_analysis",r.additional_analysis},{"confirmation_number",r.confirmation_number}};
}
}
std::string confirmation_changes(const ConfirmationRequest& r) {
    if(r.paired.candidates.size()!=1)throw std::invalid_argument("Freeze one candidate");
    const auto& b=std::get<scenario::WarehouseConfig>(r.paired.baseline.domain);const auto& c=std::get<scenario::WarehouseConfig>(r.paired.candidates[0].definition.domain);std::string result;
    const auto field=[&](const char* name,std::uint64_t before,std::uint64_t after){if(before!=after){if(!result.empty())result+="; ";result+=std::string(name)+" "+std::to_string(before)+" -> "+std::to_string(after);}};
    field("Bays",b.loading_bays,c.loading_bays);field("Workers",b.workers,c.workers);field("Forklifts",b.forklifts,c.forklifts);
    field("Seed",r.paired.baseline.simulation.random_seed,r.paired.candidates[0].definition.simulation.random_seed);
    return result.empty()?"Same operational configuration":result;
}
void validate_confirmation_request(const ConfirmationRequest& r) {
    if(r.version!=1 || r.engine_version!=confirmation_engine_version || r.generator_version!=scenario::workload_generator_version)
        throw std::invalid_argument("Unsupported confirmation provenance version");
    if(r.source_study_id.empty() || r.source_title.empty() || !r.selection_id) throw std::invalid_argument("Missing exploratory study provenance");
    if(r.source_type!="paired_statistical" && r.source_type!="sensitivity" && r.source_type!="interaction" && r.source_type!="single_seed")
        throw std::invalid_argument("Confirmation requires an exploratory study");
    for(const auto* s:{&r.source_study_id,&r.source_type,&r.source_title,&r.source_completed,&r.engine_version}) text_bound(*s,256);
    text_bound(r.changed_inputs,4096);(void)metric_name(r.objective);
    if(!r.confirmation_number || r.confirmation_number>32 || r.additional_analysis!=(r.confirmation_number>1)) throw std::invalid_argument("Invalid confirmation sequence provenance");
    if(r.exploratory_difference && !std::isfinite(*r.exploratory_difference)) throw std::invalid_argument("Nonfinite exploratory estimate");
    validate_statistical_request(r.paired);
    if(r.paired.candidates.size()!=1 || r.paired.candidates.front().id!=r.selection_id) throw std::invalid_argument("Freeze exactly one selected configuration");
    if(r.changed_inputs!=confirmation_changes(r))throw std::invalid_argument("Changed-parameter provenance differs from frozen configurations");
    if(!workload_varies(std::get<scenario::WarehouseConfig>(r.paired.baseline.domain)))
        throw std::invalid_argument("Deterministic workload: independent stochastic confirmation is disabled");
    if(r.exploratory_seeds.empty() || r.exploratory_seeds.size()>max_replications) throw std::invalid_argument("Missing or excessive exploratory seeds");
    std::set<std::uint64_t> used;
    for(auto seed:r.exploratory_seeds) if(!used.insert(seed).second) throw std::invalid_argument("Duplicate exploratory seed");
    for(auto seed:replication_seeds(r.paired.plan)) if(used.contains(seed)) throw std::invalid_argument("Confirmation seed overlaps exploratory seeds: "+std::to_string(seed));
}
ConfirmationReport run_confirmation(const ConfirmationRequest& r,const simulation::ExecutionControl* c) {
    validate_confirmation_request(r);return {r,run_statistical_experiment(r.paired,c)};
}
std::string serialize_confirmation_request(const ConfirmationRequest& r) {return request_json(r).dump();}
ConfirmationRequest parse_confirmation_request(std::string_view bytes) {
    const auto j=parse_json(bytes,max_report_bytes);
    fields(j,{"schema_version","mode","version","source_study_id","source_type","source_title","source_completed","source_request_id","selection_id",
        "engine_version","generator_version","paired","objective","exploratory_seeds","confirmation_seeds","seed_derivation","disjointness_validated",
        "exploratory_difference","changed_inputs","additional_analysis","confirmation_number"},{},"confirmation request");
    if(integer(j.at("schema_version"),5,"schema")!=5 || string(j.at("mode"),"mode")!="independent_confirmation") invalid("confirmation","unsupported schema");
    ConfirmationRequest r;r.version=static_cast<std::uint32_t>(integer(j.at("version"),1,"version"));
    r.source_request_id=integer(j.at("source_request_id"),UINT64_MAX,"source request");r.selection_id=integer(j.at("selection_id"),UINT64_MAX,"selection");
    r.confirmation_number=static_cast<std::uint32_t>(integer(j.at("confirmation_number"),32,"confirmation number"));
    r.generator_version=static_cast<std::uint32_t>(integer(j.at("generator_version"),1,"generator"));
    r.source_study_id=string(j.at("source_study_id"),"study ID");r.source_type=string(j.at("source_type"),"type");
    r.source_title=string(j.at("source_title"),"title");r.source_completed=string(j.at("source_completed"),"completed");
    r.engine_version=string(j.at("engine_version"),"engine");r.changed_inputs=string(j.at("changed_inputs"),"changes");
    r.paired=parse_statistical_request(j.at("paired").dump());
    bool found=false;for(std::size_t m=0;m<statistical_metric_count;++m) if(string(j.at("objective"),"objective")==metric_name(static_cast<StatisticalMetric>(m))) {r.objective=static_cast<StatisticalMetric>(m);found=true;}
    if(!found) invalid("objective","unsupported metric");
    const auto& seeds=j.at("exploratory_seeds");if(!seeds.is_array() || seeds.size()>max_replications) invalid("seeds","invalid exploratory seed list");
    for(const auto& s:seeds) r.exploratory_seeds.push_back(integer(s,UINT64_MAX,"seed"));
    if(!j.at("additional_analysis").is_boolean()) invalid("confirmation","additional analysis must be boolean");r.additional_analysis=j.at("additional_analysis").get<bool>();
    if(!j.at("exploratory_difference").is_null()) {if(!j.at("exploratory_difference").is_number()) invalid("estimate","expected number");r.exploratory_difference=j.at("exploratory_difference").get<double>();}
    validate_confirmation_request(r);if(request_json(r)!=j) invalid("confirmation","inconsistent seed/provenance metadata");return r;
}
std::string serialize_confirmation_report(const ConfirmationReport& r) {
    validate_confirmation_request(r.request);
    if(serialize_statistical_request(r.paired.request)!=serialize_statistical_request(r.request.paired)) throw std::invalid_argument("Confirmation result differs from frozen request");
    Json j{{"confirmation_report_version",1},{"request",request_json(r.request)},{"paired",Json::parse(serialize_statistical_report(r.paired))},
        {"caveats",Json::array({"Disjoint PRNG seeds separate reproducible realizations; statistical independence is a modeling assumption.",
        "Exploratory selection can favor noisy winners. Freeze one choice before inspecting confirmation outcomes.",
        "Nominal 95% intervals do not prove optimality or guarantee future performance. Repeated selection using confirmation reintroduces selection effects.",
        "Additional confirmations are additional analyses, not automatic validation of a revised choice. No exploratory observations are pooled."})}};
    auto bytes=j.dump();if(bytes.size()>max_report_bytes) throw std::length_error("Confirmation report exceeds byte limit");return bytes;
}
ConfirmationReport parse_confirmation_report(std::string_view bytes) {return detail::confirmation_report_value(parse_json(bytes,max_report_bytes));}
ConfirmationReport detail::confirmation_report_value(const Json& j) {
    fields(j,{"confirmation_report_version","request","paired","caveats"},{},"confirmation report");
    if(integer(j.at("confirmation_report_version"),1,"version")!=1) invalid("confirmation","unsupported version");
    ConfirmationReport r{parse_confirmation_request(j.at("request").dump()),detail::statistical_report_value(j.at("paired"))};
    if(Json::parse(serialize_confirmation_report(r))!=j) invalid("confirmation","report provenance mismatch");return r;
}
std::string serialize_confirmation_csv(const ConfirmationReport& r,bool seeds,std::string_view study) {
    (void)serialize_confirmation_report(r);std::istringstream in(serialize_statistical_csv(r.paired,seeds));std::ostringstream out;std::string line;
    const auto quote=[](std::string_view s) {std::string t="\"";for(char c:s) {if(c=='\"')t+='\"';t+=c;}return t+'\"';};
    std::getline(in,line);out<<"study_id,source_study_id,selection_id,"<<line<<'\n';
    while(std::getline(in,line)) if(!line.empty()) out<<quote(study)<<','<<quote(r.request.source_study_id)<<','<<r.request.selection_id<<','<<line<<'\n';return out.str();
}
void save_confirmation_report(const std::filesystem::path& p,const ConfirmationReport& r) {detail::save_output(p,serialize_confirmation_report(r));}
void save_confirmation_csv(const std::filesystem::path& p,const ConfirmationReport& r,bool seeds) {detail::save_output(p,serialize_confirmation_csv(r,seeds));}
}
