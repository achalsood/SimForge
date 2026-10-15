#include "demand_internal.hpp"
#include "../scenario/json_io.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace simforge::analysis::demand {
namespace {
using namespace scenario::detail;
Json rate(const Rate& r){return {{"count",r.count},{"exposure_seconds",r.exposure_seconds},{"rate_per_second",r.per_second},{"lower_per_second",r.lower_per_second},{"upper_per_second",r.upper_per_second}};}
Json metric(const Metrics& m) {
    return {{"observed",m.observed},{"exposure_seconds",m.exposure_seconds},{"expected",m.expected},{"signed_error_observed_minus_expected",m.signed_error},
        {"absolute_error",m.absolute_error},{"signed_error_per_hour",m.error_per_hour},{"poisson_deviance",m.deviance?Json(*m.deviance):Json(nullptr)},
        {"poisson_log_likelihood",m.log_likelihood?Json(*m.log_likelihood):Json(nullptr)},{"likelihood_status",m.deviance?"finite":"zero_prediction_positive_count"}};
}
Json conversion(const Conversion& c) {
    Json j{{"calibration_id",c.calibration_id},{"source",Json::parse(scenario::serialize_scenario(c.source))},{"scenario",Json::parse(scenario::serialize_scenario(c.scenario))},
        {"semantics",c.semantics},{"utc_start",timestamp(c.utc_start)},{"explicit_seed",c.seed},{"periodic_approximation_accepted",true},
        {"fitted_rates_per_second",c.fitted_per_second},{"represented_rates_per_second",c.represented_per_second},{"generator_version",scenario::workload_generator_version}};
    if(scenario::workload::poisson(std::get<scenario::WarehouseConfig>(c.scenario.domain))) {
        j["periodic_approximation_accepted"]=false;j["generator_version"]=scenario::poisson_generator_version;
        j["unavailable_policy"]=c.exclude_unavailable?"explicitly_exclude_unestimated_hours":"reject_unestimated_hours";
        j["time_grid"]="uniform_engine_ticks_[start,end)";j["prng"]="splitmix64_domain_separated_v2";
        j["expected_count"]=c.expected_count;j["realized_count"]=c.realized_count;j["fitted_available"]=c.fitted_available;
        j["realized_counts_64_bins"]=c.realized_bins;j["expected_counts_64_bins"]=c.expected_bins;
        for(std::size_t i=0;i<c.fitted_available.size();++i)if(!c.fitted_available[i])j["fitted_rates_per_second"][i]=nullptr;
    }
    return j;
}
Json report(const Report& r) {
    const auto& p=r.provenance;const auto& s=r.settings;
    Json j{{"calibration_report_version",schema_version},{"algorithm_version",algorithm_version},{"software_version",software_version},{"calibration_id",r.id},
        {"dataset",{{"id",p.dataset_id},{"source_filename",p.source_filename},{"content_sha256",p.sha256},{"import_schema_version",1},{"timestamp_format","iso8601_utc_seconds"},
        {"observation_start",timestamp(p.exposure.start)},{"observation_end",timestamp(p.exposure.end)},{"coverage","continuous"},{"interval_convention","[start,end)"},
        {"accepted_count",p.accepted},{"rejected_count",0},{"duplicate_count",p.duplicates},{"input_reordered",p.reordered},{"validation_policy","strict_all_or_nothing_sort_retain_duplicates"}}},
        {"split",{{"utc_boundary",timestamp(s.split)},{"model",s.model==Model::Constant?"constant":"daily_profile"},{"daily_bin_seconds",s.daily_bin_seconds},{"diagnostic_bin_seconds",s.diagnostic_bin_seconds}}},
        {"rate_units","arrivals_per_second"},{"uncertainty",{{"method","poisson_chernoff_inversion"},{"nominal_confidence",0.95},{"coverage","conservative_marginal"},{"tail_probability",0.025}}},
        {"model_selection_policy","manual; holdout is validation/model-selection data, not independent final-test confirmation"},
        {"constant_baseline",rate(r.baseline)},{"rates",Json::array()},{"bins",Json::array()},{"training",metric(r.training)},{"holdout",metric(r.holdout)},
        {"baseline_training",metric(r.baseline_training)},{"baseline_holdout",metric(r.baseline_holdout)},{"warnings",r.warnings},{"limitations",r.limitations},
        {"generator_version",scenario::workload_generator_version},{"scenario_conversions",Json::array()},
        {"timings_ms",{{"import",r.timings.import_ms},{"fit",r.timings.fit_ms},{"validation",r.timings.validation_ms}}}};
    const bool coverage=!p.exposure.windows.empty();
    if(coverage) {
        j["calibration_report_version"]=2;j["algorithm_version"]=2;j["software_version"]="SimForge-demand-2";
        auto& d=j["dataset"];d["coverage"]="explicit_windows";d["import_schema_version"]=2;d["coverage_windows"]=Json::array();
        for(std::size_t i=0;i<p.exposure.windows.size();++i){const auto& w=p.exposure.windows[i];d["coverage_windows"].push_back({{"index",i},{"start",timestamp(w.start)},{"end",timestamp(w.end)},{"status",coverage_name(w.status)}});}
        d["observed_exposure_seconds"]=observed_exposure(p.exposure,p.exposure.start,p.exposure.end);
        d["excluded_seconds"]=p.exposure.end-p.exposure.start-observed_exposure(p.exposure,p.exposure.start,p.exposure.end);
        for(const auto name:{"training","holdout","baseline_training","baseline_holdout"}) {
            const auto& m=std::string_view(name)=="training"?r.training:std::string_view(name)=="holdout"?r.holdout:std::string_view(name)=="baseline_training"?r.baseline_training:r.baseline_holdout;
            j[name]["unavailable_exposure_seconds"]=m.unavailable_exposure_seconds;j[name]["unavailable_count"]=m.unavailable_count;
            if(m.unavailable_exposure_seconds) {for(auto field:{"expected","signed_error_observed_minus_expected","absolute_error","signed_error_per_hour"})j[name][field]=nullptr;j[name]["likelihood_status"]="unavailable_training_rate";}
        }
    }
    if(r.rates.size()>48 || r.bins.size()>max_bins || r.conversions.size()>8 || r.warnings.size()>max_bins+64)invalid("calibration","report collections exceed limits");
    for(const auto& v:r.rates){auto x=rate(v);if(coverage){x["available"]=v.available;if(!v.available)for(auto name:{"rate_per_second","lower_per_second","upper_per_second"})x[name]=nullptr;}j["rates"].push_back(std::move(x));}
    for(const auto& b:r.bins){Json x{{"start",timestamp(b.start)},{"end",timestamp(b.end)},{"partition",b.holdout?"holdout":"training"},
        {"observed",b.observed},{"expected",b.expected},{"baseline_expected",b.baseline_expected},{"residual_observed_minus_expected",b.residual}};
        if(coverage){x["coverage_status"]=coverage_name(coverage_at(p.exposure,b.start));x["observed_exposure_seconds"]=observed_exposure(p.exposure,b.start,b.end);x["prediction_available"]=b.prediction_available;if(!b.prediction_available){x["expected"]=nullptr;x["residual_observed_minus_expected"]=nullptr;}}
        j["bins"].push_back(std::move(x));}
    for(const auto& c:r.conversions)j["scenario_conversions"].push_back(conversion(c));return j;
}
std::string text(const Json& j,std::size_t bound,const char* label) {
    auto s=string(j,label);if(s.size()>bound || s.find('\0')!=std::string::npos)invalid(label,"invalid string length/NUL");return s;
}
bool boolean(const Json& j,const char* label){if(!j.is_boolean())invalid(label,"boolean required");return j.get<bool>();}
double timing(const Json& j){if(!j.is_number())invalid("timings","number required");auto v=j.get<double>();if(!std::isfinite(v)||v<0||v>86400000)invalid("timings","invalid duration");return v;}
std::string quote(std::string_view s){std::string out="\"";for(auto ch:s){if(ch=='"')out+='"';out+=ch;}return out+'"';}
}
std::string report_json(const Report& r) {auto out=report(r).dump();if(out.size()>max_report_bytes)throw std::length_error("Calibration report exceeds 1 MiB");return out;}
std::string conversion_json(const Conversion& c){return conversion(c).dump(2);}
std::shared_ptr<const Report> parse_report(std::string_view bytes) {
    const auto j=parse_json(bytes,max_report_bytes,100'000);
    fields(j,{"calibration_report_version","algorithm_version","software_version","calibration_id","dataset","split","rate_units","uncertainty","model_selection_policy",
        "constant_baseline","rates","bins","training","holdout","baseline_training","baseline_holdout","warnings","limitations","generator_version","scenario_conversions","timings_ms"},{},"calibration report");
    auto r=std::make_shared<Report>();const auto& p=j.at("dataset");
    fields(p,{"id","source_filename","content_sha256","import_schema_version","timestamp_format","observation_start","observation_end","coverage","interval_convention","accepted_count","rejected_count","duplicate_count","input_reordered","validation_policy"},{"coverage_windows","observed_exposure_seconds","excluded_seconds"},"dataset");
    auto& d=r->provenance;d.source_filename=text(p.at("source_filename"),256,"source filename");if(d.source_filename.empty() || d.source_filename.find_first_of("/\\:")!=std::string::npos)invalid("filename","basename required");
    d.sha256=text(p.at("content_sha256"),64,"SHA-256");if(d.sha256.size()!=64 || d.sha256.find_first_not_of("0123456789abcdef")!=std::string::npos)invalid("SHA-256","canonical digest required");
    d.exposure={parse_timestamp(text(p.at("observation_start"),20,"start")),parse_timestamp(text(p.at("observation_end"),20,"end")),true};
    if(p.contains("coverage_windows")) {
        d.exposure.continuous=false;const auto& windows=p.at("coverage_windows");if(!windows.is_array()||windows.size()>max_coverage_windows)invalid("coverage","at most 256 windows");
        for(const auto& w:windows){fields(w,{"index","start","end","status"},{},"coverage window");const auto status=text(w.at("status"),32,"status");CoverageStatus value=CoverageStatus::Observed;bool found=false;for(auto candidate:{CoverageStatus::Observed,CoverageStatus::Unobserved,CoverageStatus::Closed,CoverageStatus::Interrupted})if(status==coverage_name(candidate)){value=candidate;found=true;}if(!found)invalid("status","unsupported coverage status");d.exposure.windows.push_back({parse_timestamp(text(w.at("start"),20,"window start")),parse_timestamp(text(w.at("end"),20,"window end")),value});}
    }
    validate_exposure(d.exposure);
    d.dataset_id=detail::dataset_identity(d.sha256,d.exposure);d.accepted=integer(p.at("accepted_count"),max_records,"accepted count");d.duplicates=integer(p.at("duplicate_count"),d.accepted?d.accepted-1:0,"duplicates");d.reordered=boolean(p.at("input_reordered"),"reordered");
    const auto& s=j.at("split");fields(s,{"utc_boundary","model","daily_bin_seconds","diagnostic_bin_seconds"},{},"split");auto& f=r->settings;
    f.split=parse_timestamp(text(s.at("utc_boundary"),20,"split"));const auto model=text(s.at("model"),32,"model");if(model!="constant"&&model!="daily_profile")invalid("model","unsupported model");f.model=model=="constant"?Model::Constant:Model::DailyProfile;
    f.daily_bin_seconds=static_cast<std::uint32_t>(integer(s.at("daily_bin_seconds"),86400,"daily bin"));f.diagnostic_bin_seconds=static_cast<std::uint32_t>(integer(s.at("diagnostic_bin_seconds"),max_exposure_seconds,"diagnostic bin"));
    const auto& bins=j.at("bins");if(!bins.is_array()||bins.size()>max_bins)invalid("bins","bounded array required");
    for(const auto& b:bins) {
        fields(b,{"start","end","partition","observed","expected","baseline_expected","residual_observed_minus_expected"},{"coverage_status","observed_exposure_seconds","prediction_available"},"bin");
        const auto partition=text(b.at("partition"),16,"partition");if(partition!="training" && partition!="holdout")invalid("partition","unsupported value");
        r->bins.push_back({parse_timestamp(text(b.at("start"),20,"bin start")),parse_timestamp(text(b.at("end"),20,"bin end")),partition=="holdout",integer(b.at("observed"),max_records,"bin count")});
    }
    detail::derive(*r);
    const auto& t=j.at("timings_ms");fields(t,{"import","fit","validation"},{},"timings");r->timings={timing(t.at("import")),timing(t.at("fit")),timing(t.at("validation"))};
    const auto& conversions=j.at("scenario_conversions");if(!conversions.is_array()||conversions.size()>8)invalid("conversions","at most 8 conversions");
    // Derive each conversion from validated training parameters and an authoritative source.
    for(const auto& c:conversions) {
        fields(c,{"calibration_id","source","scenario","semantics","utc_start","explicit_seed","periodic_approximation_accepted","fitted_rates_per_second","represented_rates_per_second","generator_version"},{"unavailable_policy","time_grid","prng","expected_count","realized_count","fitted_available","realized_counts_64_bins","expected_counts_64_bins"},"conversion");
        auto source=scenario::parse_scenario(c.at("source").dump());auto output=scenario::parse_scenario(c.at("scenario").dump());
        Report base=*r;base.conversions.clear();
        const auto seed=integer(c.at("explicit_seed"),UINT64_MAX,"seed");const auto start=parse_timestamp(text(c.at("utc_start"),20,"UTC start"));
        auto expected=scenario::workload::poisson(std::get<scenario::WarehouseConfig>(output.domain))?
            convert_stochastic(base,source,output.name,seed,start,c.value("unavailable_policy",std::string{})=="explicitly_exclude_unestimated_hours"):
            convert(base,source,output.name,seed,start,boolean(c.at("periodic_approximation_accepted"),"approximation acceptance"));
        if(conversion(expected)!=c)invalid("conversion","scenario or metadata differs from validated fitted rates");r->conversions.push_back(std::move(expected));
    }
    if(report(*r)!=j)invalid("calibration report","unsupported schema/provenance or inconsistent derived statistics; evidence rejected transactionally");return r;
}
FileRequest load_request(const std::filesystem::path& path) {
    const auto j=parse_json(load_text(path,65536),65536,10000);
    fields(j,{"calibration_request_version","csv","timestamp_format","observation_start","observation_end","split","model","daily_bin_seconds","diagnostic_bin_seconds"},{"continuous","coverage_windows"},"calibration request");
    const auto version=integer(j.at("calibration_request_version"),2,"request version");
    if((version!=1 && version!=2) || text(j.at("timestamp_format"),64,"timestamp format")!="iso8601_utc_seconds")invalid("request","version 1 or 2 / iso8601_utc_seconds required");
    FileRequest r;const auto csv=text(j.at("csv"),4096,"CSV path");r.csv=std::filesystem::path(std::u8string(csv.begin(),csv.end()));if(r.csv.empty())invalid("csv","filename required");if(r.csv.is_relative())r.csv=path.parent_path()/r.csv;
    r.exposure={parse_timestamp(text(j.at("observation_start"),20,"start")),parse_timestamp(text(j.at("observation_end"),20,"end")),false};
    if(version==1){if(!j.contains("continuous") || j.contains("coverage_windows"))invalid("request","version 1 requires continuous and forbids coverage_windows");r.exposure.continuous=boolean(j.at("continuous"),"continuous");}
    else {
        if(j.contains("continuous") || !j.contains("coverage_windows"))invalid("request","version 2 requires coverage_windows and forbids continuous");
        const auto& windows=j.at("coverage_windows");if(!windows.is_array() || windows.empty() || windows.size()>max_coverage_windows)invalid("coverage","1..256 windows required");
        for(const auto& w:windows) {
            fields(w,{"start","end","status"},{},"coverage window");const auto status=text(w.at("status"),32,"status");bool found=false;CoverageStatus value{};
            for(auto candidate:{CoverageStatus::Observed,CoverageStatus::Unobserved,CoverageStatus::Closed,CoverageStatus::Interrupted})if(status==coverage_name(candidate)){value=candidate;found=true;}
            if(!found)invalid("status","observed, unobserved, closed or interrupted required");
            r.exposure.windows.push_back({parse_timestamp(text(w.at("start"),20,"window start")),parse_timestamp(text(w.at("end"),20,"window end")),value});
        }
        r.exposure=normalize_exposure(std::move(r.exposure));
    }
    const auto model=text(j.at("model"),32,"model");if(model!="constant"&&model!="daily_profile")invalid("model","constant or daily_profile required");
    r.fit={parse_timestamp(text(j.at("split"),20,"split")),model=="constant"?Model::Constant:Model::DailyProfile,
        static_cast<std::uint32_t>(integer(j.at("daily_bin_seconds"),86400,"daily bin")),static_cast<std::uint32_t>(integer(j.at("diagnostic_bin_seconds"),max_exposure_seconds,"diagnostic bin"))};
    detail::validate_settings(r.exposure,r.fit);return r;
}
void save_export(const std::filesystem::path& path,std::string_view bytes){if(bytes.size()>max_report_bytes*2)throw std::length_error("Calibration export exceeds 2 MiB");scenario::detail::save_text(path,bytes);}
std::string report_csv(const Report& r) {
    if(!r.provenance.exposure.windows.empty() || std::any_of(r.conversions.begin(),r.conversions.end(),[](const auto& c){return scenario::workload::poisson(std::get<scenario::WarehouseConfig>(c.scenario.domain));})) {
        std::ostringstream out;out<<std::setprecision(17);
        out<<"record_type,calibration_id,dataset_id,content_sha256,index,partition,start_utc,end_utc,status,observed,observed_exposure_seconds,expected_count,baseline_expected_count,residual_count,rate_per_second,lower_per_second,upper_per_second,seed,generator_version,realized_count,time_grid,unavailable_policy,algorithm_version,uncertainty_method,nominal_confidence,prng,daily_bin_seconds,diagnostic_bin_seconds,model\n";
        const auto number=[](auto value){std::ostringstream s;s<<std::setprecision(17)<<value;return s.str();};
        const auto row=[&](std::vector<std::string> fields){fields.resize(29);fields[1]=r.id;fields[2]=r.provenance.dataset_id;fields[3]=r.provenance.sha256;fields[22]=r.provenance.exposure.windows.empty()?"1":"2";fields[23]="poisson_chernoff_inversion_conservative_marginal";fields[24]="0.95";if(fields[18]=="2")fields[25]="splitmix64_domain_separated_v2";fields[26]=number(r.settings.daily_bin_seconds);fields[27]=number(r.settings.diagnostic_bin_seconds);if(fields[28].empty())fields[28]=r.settings.model==Model::Constant?"constant_intensity":"utc_daily_profile";for(std::size_t i=0;i<fields.size();++i){if(i)out<<',';out<<quote(fields[i]);}out<<'\n';};
        for(std::size_t i=0;i<r.provenance.exposure.windows.size();++i){const auto& w=r.provenance.exposure.windows[i];row({"coverage_window","","","",number(i),"",timestamp(w.start),timestamp(w.end),coverage_name(w.status),"",number(w.status==CoverageStatus::Observed?w.end-w.start:0)});}
        for(std::size_t i=0;i<r.rates.size();++i){const auto& v=r.rates[i];row({"parameter","","","",number(i),"training","","",v.available?"available":"unavailable_no_training_exposure",number(v.count),number(v.exposure_seconds),"","","",v.available?number(v.per_second):"",v.available?number(v.lower_per_second):"",v.available?number(v.upper_per_second):""});}
        for(std::size_t i=0;i<r.bins.size();++i){const auto& b=r.bins[i];row({"observed_bin","","","",number(i),b.holdout?"holdout":"training",timestamp(b.start),timestamp(b.end),std::string(coverage_name(coverage_at(r.provenance.exposure,b.start)))+(b.prediction_available?"":";prediction_unavailable"),number(b.observed),number(observed_exposure(r.provenance.exposure,b.start,b.end)),b.prediction_available?number(b.expected):"",number(b.baseline_expected),b.prediction_available?number(b.residual):""});}
        for(bool holdout:{false,true}){const auto& m=holdout?r.holdout:r.training;row({"metrics","","","","",holdout?"holdout":"training","","",m.unavailable_exposure_seconds?"unavailable_training_rate":"available",number(m.observed),number(m.exposure_seconds),m.unavailable_exposure_seconds?"":number(m.expected),"",m.unavailable_exposure_seconds?"":number(m.signed_error)});}
        for(std::size_t i=0;i<r.conversions.size();++i){const auto& c=r.conversions[i];const auto& cfg=std::get<scenario::WarehouseConfig>(c.scenario.domain);if(!scenario::workload::poisson(cfg))continue;
            const auto model=scenario::workload::arrival_name(cfg.workload.arrivals);const auto policy=c.exclude_unavailable?"explicitly_exclude_unestimated_hours":"reject_unestimated_hours";
            std::vector<std::string> conversion_row{"stochastic_conversion","","","",number(i),"",timestamp(c.utc_start),timestamp(c.utc_start+c.scenario.simulation.duration_seconds),"seeded_realization","","",number(c.expected_count),"","","","","",number(c.seed),"2",number(c.realized_count),"uniform_engine_ticks_[start,end)",policy};conversion_row.resize(29);conversion_row[28]=model;row(std::move(conversion_row));
            auto segments=cfg.workload.intensities;if(cfg.workload.arrivals==scenario::ArrivalMode::Poisson)segments={{0,c.scenario.simulation.duration_seconds,cfg.workload.rate_per_second}};
            for(std::size_t j=0;j<segments.size();++j){const auto& s=segments[j];std::vector<std::string> segment{"arrival_intensity_segment","","","",number(j),"conversion_"+number(i),timestamp(c.utc_start+s.start_seconds),timestamp(c.utc_start+s.end_seconds),"explicit_simulated_arrival_schedule","","",number(s.rate_per_second*(s.end_seconds-s.start_seconds)),"","",number(s.rate_per_second),"","",number(c.seed),"2","","uniform_engine_ticks_[start,end)",policy};segment.resize(29);segment[28]=model;row(std::move(segment));}
        }
        return out.str();
    }
    std::ostringstream out;out<<std::setprecision(17);
    out<<"record_type,calibration_id,dataset_id,content_sha256,index,partition,start_utc,end_utc,observed,exposure_seconds,expected_count,baseline_expected_count,residual_count,rate_per_second,lower_per_second,upper_per_second,poisson_deviance,poisson_log_likelihood,likelihood_status\n";
    const auto prefix=[&](const char* type){out<<type<<','<<quote(r.id)<<','<<quote(r.provenance.dataset_id)<<','<<r.provenance.sha256<<',';};
    for(std::size_t i=0;i<r.rates.size();++i){const auto& v=r.rates[i];prefix("parameter");out<<i<<",training,,,"<<v.count<<','<<v.exposure_seconds<<",,,,"<<v.per_second<<','<<v.lower_per_second<<','<<v.upper_per_second<<",,,\n";}
    for(std::size_t i=0;i<r.bins.size();++i){const auto& b=r.bins[i];prefix("observed_bin");out<<i<<','<<(b.holdout?"holdout":"training")<<','<<timestamp(b.start)<<','<<timestamp(b.end)<<','<<b.observed<<','<<b.end-b.start<<','<<b.expected<<','<<b.baseline_expected<<','<<b.residual<<",,,,,,\n";}
    for(bool holdout:{false,true}){const auto& m=holdout?r.holdout:r.training;prefix("metrics");out<<','<<(holdout?"holdout":"training")<<",,,"<<m.observed<<','<<m.exposure_seconds<<','<<m.expected<<",,"<<m.signed_error<<",,,,";if(m.deviance)out<<*m.deviance;out<<',';if(m.log_likelihood)out<<*m.log_likelihood;out<<','<<(m.deviance?"finite":"zero_prediction_positive_count")<<'\n';}
    auto bytes=out.str();if(bytes.size()>max_report_bytes*2)throw std::length_error("CSV report exceeds limit");return bytes;
}
}
