#include <simforge/analysis/demand_diagnostics.hpp>
#include "../scenario/json_io.hpp"
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>

namespace simforge::analysis::diagnostics {
namespace {
using namespace scenario::detail;
Json value(std::optional<double> v){return v?Json(*v):Json(nullptr);}
std::optional<double> optional(const Json& v){if(v.is_null())return {};if(!v.is_number() || !std::isfinite(v.get<double>()))invalid("diagnostic number","finite number or null required");return v.get<double>();}
Json interval(const std::optional<Interval>& v){return v?Json{{"lower",v->lower},{"upper",v->upper}}:Json(nullptr);}
std::optional<Interval> read_interval(const Json& j) {
    if(j.is_null())return {};fields(j,{"lower","upper"},{},"interval");
    Interval i{integer(j.at("lower"),1'000'000,"lower"),integer(j.at("upper"),1'000'000,"upper")};if(i.lower>i.upper)invalid("interval","reversed bounds");return i;
}
Json prediction(const Prediction& p){return {{"mean",value(p.mean)},{"lower_mean",value(p.lower_mean)},{"upper_mean",value(p.upper_mean)},{"process_only",interval(p.process)},{"parameter_aware_envelope",interval(p.parameter_aware)}};}
Prediction read_prediction(const Json& j){return {optional(j.at("mean")),optional(j.at("lower_mean")),optional(j.at("upper_mean")),read_interval(j.at("process_only")),read_interval(j.at("parameter_aware_envelope"))};}
Json settings(const Settings& s){return {{"partition",s.partition==Partition::Holdout?"holdout":"training"},{"bin_seconds",s.bin_seconds},{"replications",s.replications},{"tail_threshold",s.tail_threshold},{"seed",s.seed},{"nominal_coverage",s.level},{"prediction_start",demand::timestamp(s.prediction_start)},{"prediction_seconds",s.prediction_seconds},{"exploratory",s.exploratory}};}
Settings read_settings(const Json& j) {
    fields(j,{"partition","bin_seconds","replications","tail_threshold","seed","nominal_coverage","prediction_start","prediction_seconds","exploratory"},{},"diagnostic settings");
    Settings s;const auto partition=string(j.at("partition"),"partition");if(partition!="holdout" && partition!="training")invalid("partition","holdout or training required");s.partition=partition=="holdout"?Partition::Holdout:Partition::Training;
    s.bin_seconds=static_cast<std::uint32_t>(integer(j.at("bin_seconds"),604800,"bin width"));
    s.replications=static_cast<std::uint32_t>(integer(j.at("replications"),max_replications,"replications"));
    s.tail_threshold=static_cast<std::uint32_t>(integer(j.at("tail_threshold"),demand::max_records,"threshold"));
    s.seed=integer(j.at("seed"),UINT64_MAX,"seed");s.level=optional(j.at("nominal_coverage")).value_or(0);
    s.prediction_start=demand::parse_timestamp(string(j.at("prediction_start"),"prediction start"));s.prediction_seconds=static_cast<std::uint32_t>(integer(j.at("prediction_seconds"),604800,"prediction duration"));
    if(!j.at("exploratory").is_boolean())invalid("exploratory","boolean required");s.exploratory=j.at("exploratory").get<bool>();
    if(!s.bin_seconds || !s.replications || !s.tail_threshold || !s.prediction_seconds || s.level<.8 || s.level>.99)invalid("diagnostic settings","positive bounds and coverage .80.. .99 required");return s;
}
Json statistic(const Statistics& s){return {{"valid_bins",s.valid_bins},{"positive_bins",s.positive_bins},{"adjacent_pairs",s.adjacent_pairs},{"zero_bins",s.zeros},{"tail_bins",s.tail_bins},{"maximum_count",s.maximum},{"zero_mean_positive_bins",s.zero_mean_positive},{"expected_zero_bins",s.expected_zeros},{"expected_tail_bins",s.expected_tail_bins},{"pearson",value(s.pearson)},{"dispersion",value(s.dispersion)},{"lag_one",value(s.lag_one)},{"residual_degrees_of_freedom",s.residual_degrees_of_freedom}};}
Statistics read_statistic(const Json& j) {
    Statistics s;s.valid_bins=integer(j.at("valid_bins"),max_bins,"valid bins");s.positive_bins=integer(j.at("positive_bins"),s.valid_bins,"positive bins");s.adjacent_pairs=integer(j.at("adjacent_pairs"),max_bins,"pairs");
    s.zeros=integer(j.at("zero_bins"),s.valid_bins,"zeros");s.tail_bins=integer(j.at("tail_bins"),s.valid_bins,"tails");s.maximum=integer(j.at("maximum_count"),demand::max_records,"maximum");s.zero_mean_positive=integer(j.at("zero_mean_positive_bins"),s.valid_bins,"zero mean");
    s.expected_zeros=optional(j.at("expected_zero_bins")).value_or(-1);s.expected_tail_bins=optional(j.at("expected_tail_bins")).value_or(-1);s.pearson=optional(j.at("pearson"));s.dispersion=optional(j.at("dispersion"));s.lag_one=optional(j.at("lag_one"));
    if(!j.at("residual_degrees_of_freedom").is_number_integer())invalid("degrees of freedom","integer required");s.residual_degrees_of_freedom=j.at("residual_degrees_of_freedom").get<std::int64_t>();
    if(s.residual_degrees_of_freedom < -48 || s.residual_degrees_of_freedom>max_bins || s.expected_zeros<0 || s.expected_zeros>s.valid_bins || s.expected_tail_bins<0 || s.expected_tail_bins>s.valid_bins || (s.pearson && *s.pearson<0) || (s.dispersion && *s.dispersion<0) || (s.lag_one && std::abs(*s.lag_one)>1))invalid("statistics","out of bounds");return s;
}
Json coverage(const Coverage& c){return {{"evaluated",c.evaluated},{"inside",c.inside},{"below",c.below},{"above",c.above},{"mean_width",c.mean_width},{"observed_coverage",value(c.fraction)}};}
Coverage read_coverage(const Json& j) {
    Coverage c;c.evaluated=integer(j.at("evaluated"),max_bins,"evaluated");c.inside=integer(j.at("inside"),max_bins,"inside");c.below=integer(j.at("below"),max_bins,"below");c.above=integer(j.at("above"),max_bins,"above");c.mean_width=optional(j.at("mean_width")).value_or(-1);c.fraction=optional(j.at("observed_coverage"));return c;
}
Json reference(const Reference& r){return {{"lower",value(r.lower)},{"median",value(r.median)},{"upper",value(r.upper)},{"inclusive_lower_rank",value(r.lower_rank)},{"inclusive_upper_rank",value(r.upper_rank)},{"upper_tail_mc_standard_error",value(r.mc_standard_error)},{"evaluated_replications",r.evaluated_replications},{"upper_tail_wilson_95_lower",value(r.upper_tail_wilson_lower)},{"upper_tail_wilson_95_upper",value(r.upper_tail_wilson_upper)}};}
Reference read_reference(const Json& j){return {optional(j.at("lower")),optional(j.at("median")),optional(j.at("upper")),optional(j.at("inclusive_lower_rank")),optional(j.at("inclusive_upper_rank")),optional(j.at("upper_tail_mc_standard_error")),static_cast<std::uint32_t>(integer(j.at("evaluated_replications"),max_replications,"reference size")),optional(j.at("upper_tail_wilson_95_lower")),optional(j.at("upper_tail_wilson_95_upper"))};}
Json report(const Report& r) {
    Json j{{"diagnostic_report_version",1},{"method_version",method_version},{"arrival_generator_version",2},{"prng","splitmix64_generator2_fragment_domain_v1"},{"time_grid","integer_UTC_seconds_[start,end)"},{"rate_units","arrivals_per_second"},{"interval_methods",{{"process","poisson_discrete_equal_tail_mode_recurrence_v1"},{"parameter_aware","simultaneous_chernoff_projection_union_bound_v1"},{"reference","fixed_parameter_no_training_refit_v1"}}},{"source_calibration",Json::parse(demand::report_json(*r.request.source))},{"settings",settings(r.request.settings)},{"data_availability",r.data_availability},{"bins",Json::array()},{"observed_statistics",statistic(r.observed)},{"process_interval_coverage",coverage(r.process_coverage)},{"parameter_interval_coverage",coverage(r.parameter_coverage)},{"future_prediction",prediction(r.future)},{"replications",Json::array()},{"references",{{"dispersion",reference(r.dispersion_reference)},{"zeros",reference(r.zero_reference)},{"tail",reference(r.tail_reference)},{"absolute_lag_one",reference(r.lag_reference)}}},{"assessment",r.assessment},{"warnings",r.warnings}};
    for(const auto& b:r.bins) {
        Json v{{"start",demand::timestamp(b.start)},{"end",demand::timestamp(b.end)},{"observed_exposure_seconds",b.exposure_seconds},{"observed_count",b.count},{"available",b.available},{"fully_observed",b.fully_observed},{"prediction",prediction(b.prediction)},{"standardized_residual",value(b.residual)},{"observed_intensity_pieces",Json::array()}};
        for(const auto& piece:b.pieces)v["observed_intensity_pieces"].push_back({{"start",demand::timestamp(piece.start)},{"end",demand::timestamp(piece.end)},{"rate_index",piece.rate_index}});j["bins"].push_back(std::move(v));
    }
    for(const auto& rep:r.replications)j["replications"].push_back({{"seed",rep.seed},{"diagnostic_statistics",statistic(rep.statistics)}});
    // Existing calibration timings are provenance, but do not change statistical identity.
    auto identity=j;identity["source_calibration"].erase("timings_ms");
    const auto id="diagnostic-"+demand::sha256(identity.dump());
    if(!r.id.empty() && r.id!=id)invalid("diagnostic identity","content digest mismatch");
    j["diagnostic_id"]=id;j["title"]=r.title;j["notes"]=r.notes;j["timings_ms"]={{"preparation",r.preparation_ms},{"simulation",r.simulation_ms}};return j;
}
std::string quoted(std::string_view s){std::string v="\"";for(auto c:s){v+=c;if(c=='"')v+=c;}return v+'"';}
}
std::string report_json(const Report& r) {
    if(!r.request.source || r.bins.empty() || r.bins.size()>max_bins || r.replications.size()!=r.request.settings.replications || r.replications.size()>max_replications)
        throw std::invalid_argument("A bounded completed diagnostic report with frozen source is required");
    auto bytes=report(r).dump();if(bytes.size()>max_report_bytes)throw std::length_error("Diagnostic evidence exceeds 8 MiB");return bytes;
}
std::shared_ptr<const Report> parse_report(std::string_view bytes) {
    const auto j=parse_json(bytes,max_report_bytes,500'000);auto r=std::make_shared<Report>();
    r->request.source=demand::parse_report(j.at("source_calibration").dump());r->request.settings=read_settings(j.at("settings"));
    r->id=string(j.at("diagnostic_id"),"diagnostic ID");r->title=string(j.at("title"),"title");r->notes=string(j.at("notes"),"notes");
    if(r->title.empty() || r->title.size()>256 || r->notes.size()>4096 || r->title.find('\0')!=std::string::npos || r->notes.find('\0')!=std::string::npos)invalid("metadata","title 1..256 and notes 0..4096 without NUL required");
    r->data_availability=string(j.at("data_availability"),"availability");if(r->data_availability!="fingerprint_verified_observations" && r->data_availability!="exact_coarsening_of_retained_calibration_counts")invalid("data availability","unsupported provenance");
    const auto& bins=j.at("bins");if(!bins.is_array() || bins.empty() || bins.size()>max_bins)invalid("bins","1..1024 bins required");std::size_t piece_count=0;
    const auto& source=*r->request.source;const auto& s=r->request.settings;
    auto edge=s.partition==Partition::Training?source.provenance.exposure.start:source.settings.split;
    const auto end=s.partition==Partition::Training?source.settings.split:source.provenance.exposure.end;
    for(const auto& v:bins) {
        Bin b;b.start=demand::parse_timestamp(string(v.at("start"),"start"));b.end=demand::parse_timestamp(string(v.at("end"),"end"));
        if(b.start!=edge || b.end!=std::min(end,edge+s.bin_seconds))invalid("bins","ordered fixed-width partition required");edge=b.end;
        b.exposure_seconds=integer(v.at("observed_exposure_seconds"),604800,"exposure");b.count=integer(v.at("observed_count"),demand::max_records,"count");
        if(!v.at("available").is_boolean() || !v.at("fully_observed").is_boolean())invalid("bin status","booleans required");b.available=v.at("available").get<bool>();b.fully_observed=v.at("fully_observed").get<bool>();
        b.prediction=read_prediction(v.at("prediction"));b.residual=optional(v.at("standardized_residual"));
        const auto& spans=v.at("observed_intensity_pieces");if(!spans.is_array() || (piece_count+=spans.size())>max_pieces)invalid("pieces","at most 4096 pieces");
        for(const auto& p:spans)b.pieces.push_back({demand::parse_timestamp(string(p.at("start"),"piece start")),demand::parse_timestamp(string(p.at("end"),"piece end")),static_cast<std::uint32_t>(integer(p.at("rate_index"),source.rates.size()-1,"rate index"))});
        r->bins.push_back(std::move(b));
    }
    if(edge!=end)invalid("bins","complete partition required");
    r->observed=read_statistic(j.at("observed_statistics"));r->process_coverage=read_coverage(j.at("process_interval_coverage"));r->parameter_coverage=read_coverage(j.at("parameter_interval_coverage"));r->future=read_prediction(j.at("future_prediction"));
    const auto& samples=j.at("replications");if(!samples.is_array() || samples.size()!=s.replications)invalid("replications","exact requested count required");
    auto seed_state=s.seed^0x7463e0ef3b19a821ULL;
    for(const auto& rep:samples){const auto seed=integer(rep.at("seed"),UINT64_MAX,"replication seed");if(seed!=scenario::workload::next_random(seed_state))invalid("seed plan","sequence mismatch");r->replications.push_back({seed,read_statistic(rep.at("diagnostic_statistics"))});}
    const auto& refs=j.at("references");r->dispersion_reference=read_reference(refs.at("dispersion"));r->zero_reference=read_reference(refs.at("zeros"));r->tail_reference=read_reference(refs.at("tail"));r->lag_reference=read_reference(refs.at("absolute_lag_one"));
    r->assessment=string(j.at("assessment"),"assessment");if(r->assessment.size()>512)invalid("assessment","too long");
    const auto& warnings=j.at("warnings");if(!warnings.is_array() || warnings.size()>32)invalid("warnings","at most 32");for(const auto& w:warnings){auto text=string(w,"warning");if(text.size()>1024)invalid("warning","too long");r->warnings.push_back(std::move(text));}
    r->preparation_ms=optional(j.at("timings_ms").at("preparation")).value_or(-1);r->simulation_ms=optional(j.at("timings_ms").at("simulation")).value_or(-1);
    if(r->preparation_ms<0 || r->simulation_ms<0)invalid("timings","nonnegative required");
    detail::validate_retained_report(*r);if(report(*r)!=j)invalid("diagnostic report","unsupported fields/version or inconsistent content identity");return r;
}
Request load_request(const std::filesystem::path& path) {
    const auto j=parse_json(load_text(path,65536),65536,10000);
    fields(j,{"diagnostic_request_version","source_calibration","settings"},{"csv"},"diagnostic request");if(integer(j.at("diagnostic_request_version"),1,"version")!=1)invalid("version","1 required");
    const auto resolve=[&](const Json& v){const auto name=string(v,"path");if(name.empty() || name.size()>4096)invalid("path","1..4096 bytes required");auto p=std::filesystem::path(std::u8string(name.begin(),name.end()));return p.is_relative()?path.parent_path()/p:p;};
    Request r;r.source=demand::parse_report(load_text(resolve(j.at("source_calibration")),demand::max_report_bytes));r.settings=read_settings(j.at("settings"));if(j.contains("csv"))r.csv=resolve(j.at("csv"));return r;
}
std::string report_csv(const Report& r) {
    (void)report_json(r);std::ostringstream out;out<<std::setprecision(17);
    out<<"table,diagnostic_id,source_calibration_id,content_sha256,index,partition,start_utc,end_utc,observed_exposure_seconds,observed_count,expected_count,standardized_residual,process_lower,process_upper,parameter_lower,parameter_upper,seed,pearson,dispersion,zero_bins,tail_bins,lag_one,nominal_coverage,status,method_version\n";
    const auto number=[](auto x){std::ostringstream s;s<<std::setprecision(17)<<x;return s.str();};
    const auto row=[&](std::vector<std::string> fields){fields.resize(25);fields[1]=r.id;fields[2]=r.request.source->id;fields[3]=r.request.source->provenance.sha256;fields[5]=r.request.settings.partition==Partition::Training?"training":"holdout";fields[22]=number(r.request.settings.level);fields[24]="1";for(std::size_t i=0;i<fields.size();++i){if(i)out<<',';out<<quoted(fields[i]);}out<<'\n';};
    for(std::size_t i=0;i<r.bins.size();++i){const auto& b=r.bins[i];std::vector<std::string> fields{"bins",{}, {}, {},number(i),{},demand::timestamp(b.start),demand::timestamp(b.end),number(b.exposure_seconds),number(b.count),b.prediction.mean?number(*b.prediction.mean):"",b.residual?number(*b.residual):"",b.prediction.process?number(b.prediction.process->lower):"",b.prediction.process?number(b.prediction.process->upper):"",b.prediction.parameter_aware?number(b.prediction.parameter_aware->lower):"",b.prediction.parameter_aware?number(b.prediction.parameter_aware->upper):""};fields.resize(25);fields[23]=b.available?"available":"unavailable_no_exposure_or_rate";row(std::move(fields));}
    for(std::size_t i=0;i<r.replications.size();++i){const auto& rep=r.replications[i];const auto& s=rep.statistics;std::vector<std::string> fields(25);fields[0]="reference_statistics";fields[4]=number(i);fields[16]=number(rep.seed);fields[17]=s.pearson?number(*s.pearson):"";fields[18]=s.dispersion?number(*s.dispersion):"";fields[19]=number(s.zeros);fields[20]=number(s.tail_bins);fields[21]=s.lag_one?number(*s.lag_one):"";row(std::move(fields));}
    for(bool parameter:{false,true}){const auto& c=parameter?r.parameter_coverage:r.process_coverage;std::vector<std::string> fields(25);fields[0]=parameter?"parameter_aware_coverage":"process_only_coverage";fields[8]=number(c.evaluated);fields[9]=number(c.inside);fields[10]=c.fraction?number(*c.fraction):"";fields[12]=number(c.below);fields[13]=number(c.above);fields[14]=number(c.mean_width);fields[23]="evaluated_inside_fraction_below_above_mean_width";row(std::move(fields));}
    std::vector<std::string> summary(25);summary[0]="summary";summary[17]=r.observed.pearson?number(*r.observed.pearson):"";summary[18]=r.observed.dispersion?number(*r.observed.dispersion):"";summary[19]=number(r.observed.zeros);summary[20]=number(r.observed.tail_bins);summary[21]=r.observed.lag_one?number(*r.observed.lag_one):"";summary[23]=r.assessment;row(std::move(summary));
    const auto bytes=out.str();if(bytes.size()>max_report_bytes)throw std::length_error("Diagnostic CSV exceeds 8 MiB");return bytes;
}
void save_export(const std::filesystem::path& path,std::string_view bytes) {
    if(bytes.size()>max_report_bytes)throw std::length_error("Diagnostic export exceeds 8 MiB");scenario::detail::save_text(path,bytes);
}
}
