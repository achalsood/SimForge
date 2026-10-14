#include "demand_internal.hpp"
#include "../scenario/json_io.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>

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
    return {{"calibration_id",c.calibration_id},{"source",Json::parse(scenario::serialize_scenario(c.source))},{"scenario",Json::parse(scenario::serialize_scenario(c.scenario))},
        {"semantics",c.semantics},{"utc_start",timestamp(c.utc_start)},{"explicit_seed",c.seed},{"periodic_approximation_accepted",true},
        {"fitted_rates_per_second",c.fitted_per_second},{"represented_rates_per_second",c.represented_per_second},{"generator_version",scenario::workload_generator_version}};
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
    if(r.rates.size()>48 || r.bins.size()>max_bins || r.conversions.size()>8 || r.warnings.size()>max_bins+64)invalid("calibration","report collections exceed limits");
    for(const auto& v:r.rates)j["rates"].push_back(rate(v));
    for(const auto& b:r.bins)j["bins"].push_back({{"start",timestamp(b.start)},{"end",timestamp(b.end)},{"partition",b.holdout?"holdout":"training"},
        {"observed",b.observed},{"expected",b.expected},{"baseline_expected",b.baseline_expected},{"residual_observed_minus_expected",b.residual}});
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
    fields(p,{"id","source_filename","content_sha256","import_schema_version","timestamp_format","observation_start","observation_end","coverage","interval_convention","accepted_count","rejected_count","duplicate_count","input_reordered","validation_policy"},{},"dataset");
    auto& d=r->provenance;d.source_filename=text(p.at("source_filename"),256,"source filename");if(d.source_filename.empty() || d.source_filename.find_first_of("/\\:")!=std::string::npos)invalid("filename","basename required");
    d.sha256=text(p.at("content_sha256"),64,"SHA-256");if(d.sha256.size()!=64 || d.sha256.find_first_not_of("0123456789abcdef")!=std::string::npos)invalid("SHA-256","canonical digest required");
    d.exposure={parse_timestamp(text(p.at("observation_start"),20,"start")),parse_timestamp(text(p.at("observation_end"),20,"end")),true};validate_exposure(d.exposure);
    d.dataset_id=detail::dataset_identity(d.sha256,d.exposure);d.accepted=integer(p.at("accepted_count"),max_records,"accepted count");d.duplicates=integer(p.at("duplicate_count"),d.accepted?d.accepted-1:0,"duplicates");d.reordered=boolean(p.at("input_reordered"),"reordered");
    const auto& s=j.at("split");fields(s,{"utc_boundary","model","daily_bin_seconds","diagnostic_bin_seconds"},{},"split");auto& f=r->settings;
    f.split=parse_timestamp(text(s.at("utc_boundary"),20,"split"));const auto model=text(s.at("model"),32,"model");if(model!="constant"&&model!="daily_profile")invalid("model","unsupported model");f.model=model=="constant"?Model::Constant:Model::DailyProfile;
    f.daily_bin_seconds=static_cast<std::uint32_t>(integer(s.at("daily_bin_seconds"),86400,"daily bin"));f.diagnostic_bin_seconds=static_cast<std::uint32_t>(integer(s.at("diagnostic_bin_seconds"),max_exposure_seconds,"diagnostic bin"));
    const auto& bins=j.at("bins");if(!bins.is_array()||bins.size()>max_bins)invalid("bins","bounded array required");
    for(const auto& b:bins) {
        fields(b,{"start","end","partition","observed","expected","baseline_expected","residual_observed_minus_expected"},{},"bin");
        const auto partition=text(b.at("partition"),16,"partition");if(partition!="training" && partition!="holdout")invalid("partition","unsupported value");
        r->bins.push_back({parse_timestamp(text(b.at("start"),20,"bin start")),parse_timestamp(text(b.at("end"),20,"bin end")),partition=="holdout",integer(b.at("observed"),max_records,"bin count")});
    }
    detail::derive(*r);
    const auto& t=j.at("timings_ms");fields(t,{"import","fit","validation"},{},"timings");r->timings={timing(t.at("import")),timing(t.at("fit")),timing(t.at("validation"))};
    const auto& conversions=j.at("scenario_conversions");if(!conversions.is_array()||conversions.size()>8)invalid("conversions","at most 8 conversions");
    // Derive each conversion from validated training parameters and an authoritative source.
    for(const auto& c:conversions) {
        fields(c,{"calibration_id","source","scenario","semantics","utc_start","explicit_seed","periodic_approximation_accepted","fitted_rates_per_second","represented_rates_per_second","generator_version"},{},"conversion");
        auto source=scenario::parse_scenario(c.at("source").dump());auto output=scenario::parse_scenario(c.at("scenario").dump());
        Report base=*r;base.conversions.clear();
        auto expected=convert(base,source,output.name,integer(c.at("explicit_seed"),UINT64_MAX,"seed"),parse_timestamp(text(c.at("utc_start"),20,"UTC start")),boolean(c.at("periodic_approximation_accepted"),"approximation acceptance"));
        if(conversion(expected)!=c)invalid("conversion","scenario or metadata differs from validated fitted rates");r->conversions.push_back(std::move(expected));
    }
    if(report(*r)!=j)invalid("calibration report","unsupported schema/provenance or inconsistent derived statistics; evidence rejected transactionally");return r;
}
FileRequest load_request(const std::filesystem::path& path) {
    const auto j=parse_json(load_text(path,65536),65536,10000);
    fields(j,{"calibration_request_version","csv","timestamp_format","observation_start","observation_end","continuous","split","model","daily_bin_seconds","diagnostic_bin_seconds"},{},"calibration request");
    if(integer(j.at("calibration_request_version"),1,"request version")!=1 || text(j.at("timestamp_format"),64,"timestamp format")!="iso8601_utc_seconds")invalid("request","version 1 / iso8601_utc_seconds required");
    FileRequest r;const auto csv=text(j.at("csv"),4096,"CSV path");r.csv=std::filesystem::path(std::u8string(csv.begin(),csv.end()));if(r.csv.empty())invalid("csv","filename required");if(r.csv.is_relative())r.csv=path.parent_path()/r.csv;
    r.exposure={parse_timestamp(text(j.at("observation_start"),20,"start")),parse_timestamp(text(j.at("observation_end"),20,"end")),boolean(j.at("continuous"),"continuous")};
    const auto model=text(j.at("model"),32,"model");if(model!="constant"&&model!="daily_profile")invalid("model","constant or daily_profile required");
    r.fit={parse_timestamp(text(j.at("split"),20,"split")),model=="constant"?Model::Constant:Model::DailyProfile,
        static_cast<std::uint32_t>(integer(j.at("daily_bin_seconds"),86400,"daily bin")),static_cast<std::uint32_t>(integer(j.at("diagnostic_bin_seconds"),max_exposure_seconds,"diagnostic bin"))};
    detail::validate_settings(r.exposure,r.fit);return r;
}
void save_export(const std::filesystem::path& path,std::string_view bytes){if(bytes.size()>max_report_bytes*2)throw std::length_error("Calibration export exceeds 2 MiB");scenario::detail::save_text(path,bytes);}
std::string report_csv(const Report& r) {
    std::ostringstream out;out<<std::setprecision(17);
    out<<"record_type,calibration_id,dataset_id,content_sha256,index,partition,start_utc,end_utc,observed,exposure_seconds,expected_count,baseline_expected_count,residual_count,rate_per_second,lower_per_second,upper_per_second,poisson_deviance,poisson_log_likelihood,likelihood_status\n";
    const auto prefix=[&](const char* type){out<<type<<','<<quote(r.id)<<','<<quote(r.provenance.dataset_id)<<','<<r.provenance.sha256<<',';};
    for(std::size_t i=0;i<r.rates.size();++i){const auto& v=r.rates[i];prefix("parameter");out<<i<<",training,,,"<<v.count<<','<<v.exposure_seconds<<",,,,"<<v.per_second<<','<<v.lower_per_second<<','<<v.upper_per_second<<",,,\n";}
    for(std::size_t i=0;i<r.bins.size();++i){const auto& b=r.bins[i];prefix("observed_bin");out<<i<<','<<(b.holdout?"holdout":"training")<<','<<timestamp(b.start)<<','<<timestamp(b.end)<<','<<b.observed<<','<<b.end-b.start<<','<<b.expected<<','<<b.baseline_expected<<','<<b.residual<<",,,,,,\n";}
    for(bool holdout:{false,true}){const auto& m=holdout?r.holdout:r.training;prefix("metrics");out<<','<<(holdout?"holdout":"training")<<",,,"<<m.observed<<','<<m.exposure_seconds<<','<<m.expected<<",,"<<m.signed_error<<",,,,";if(m.deviance)out<<*m.deviance;out<<',';if(m.log_likelihood)out<<*m.log_likelihood;out<<','<<(m.deviance?"finite":"zero_prediction_positive_count")<<'\n';}
    auto bytes=out.str();if(bytes.size()>max_report_bytes*2)throw std::length_error("CSV report exceeds limit");return bytes;
}
}
