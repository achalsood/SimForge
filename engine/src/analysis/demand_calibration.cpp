#include "demand_internal.hpp"
#include "../scenario/json_io.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace simforge::analysis::demand {
namespace {
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
void phase(const simulation::ExecutionControl* c,const char* name){if(c)c->update(name);}
void require(bool condition,std::string_view message){if(!condition)throw std::invalid_argument("Demand calibration: "+std::string(message));}
constexpr std::int64_t first_second=0,last_second=4102444800LL; // 1970-01-01 .. 2100-01-01 exclusive arrivals
std::int64_t next_edge(std::int64_t t,const Provenance& p,const FitSettings& s) {
    return std::min({p.exposure.end,t<s.split?s.split:p.exposure.end,
        (t/s.daily_bin_seconds+1)*s.daily_bin_seconds,
        p.exposure.start+((t-p.exposure.start)/s.diagnostic_bin_seconds+1)*s.diagnostic_bin_seconds});
}
std::string filename(std::string name) {
    // Accept only the basename in evidence, never a private absolute path.
    const auto slash=name.find_last_of("/\\");if(slash!=std::string::npos)name.erase(0,slash+1);
    require(!name.empty() && name.size()<=256 && name.find_first_of(std::string("\0:",2))==std::string::npos,"source filename must contain 1..256 bytes without NUL or colon");return name;
}
void add_metric(Metrics& m,std::uint64_t count,double expected,std::uint64_t exposure) {
    m.observed+=count;m.exposure_seconds+=exposure;m.expected+=expected;
    if(expected==0 && count>0){m.deviance.reset();m.log_likelihood.reset();return;}
    const auto n=static_cast<double>(count);
    if(m.deviance)*m.deviance+=count?std::max(0.0,2*(n*std::log(n/expected)-(n-expected))):2*expected;
    if(m.log_likelihood)*m.log_likelihood+=count?n*std::log(expected)-expected-std::lgamma(n+1):-expected;
}
void finish(Metrics& m) {
    m.signed_error=static_cast<double>(m.observed)-m.expected;m.absolute_error=std::abs(m.signed_error);
    m.error_per_hour=m.signed_error*3600/static_cast<double>(m.exposure_seconds);
}
}
std::int64_t parse_timestamp(std::string_view value) {
    require(value.size()==20 && value[4]=='-' && value[7]=='-' && value[10]=='T' && value[13]==':' && value[16]==':' && value[19]=='Z',
        "timestamp must be exactly YYYY-MM-DDTHH:MM:SSZ (UTC seconds; offsets, fractions and leap seconds unsupported)");
    const auto digits=[&](std::size_t pos,std::size_t n){int result=0;for(std::size_t i=pos;i<pos+n;++i){require(value[i]>='0'&&value[i]<='9',"timestamp contains nondecimal fields");result=result*10+value[i]-'0';}return result;};
    const auto year=digits(0,4),month=digits(5,2),day=digits(8,2),hour=digits(11,2),minute=digits(14,2),second=digits(17,2);
    const std::chrono::year_month_day date{std::chrono::year{year},std::chrono::month{static_cast<unsigned>(month)},std::chrono::day{static_cast<unsigned>(day)}};
    require(date.ok() && year>=1970 && year<=2100 && hour<24 && minute<60 && second<60,"invalid calendar date or supported UTC range (1970 through 2099)");
    const auto result=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::sys_days{date}.time_since_epoch()).count()+hour*3600LL+minute*60+second;
    require(result>=first_second && result<=last_second,"timestamp outside supported UTC range");return result;
}
std::string timestamp(std::int64_t value) {
    require(value>=first_second && value<=last_second,"timestamp outside supported UTC range");
    const auto days=value/86400,seconds=value%86400;
    const std::chrono::year_month_day date{std::chrono::sys_days{std::chrono::days{days}}};
    std::ostringstream out;out<<std::setfill('0')<<std::setw(4)<<int(date.year())<<'-'<<std::setw(2)<<unsigned(date.month())<<'-'<<std::setw(2)<<unsigned(date.day())
        <<'T'<<std::setw(2)<<seconds/3600<<':'<<std::setw(2)<<(seconds%3600)/60<<':'<<std::setw(2)<<seconds%60<<'Z';return out.str();
}
void validate_exposure(const ImportSettings& s) {
    require(s.continuous,"coverage must explicitly be continuous; unknown coverage, gaps, interruptions and closed shifts are unsupported");
    require(s.start>=first_second && s.end<=last_second && s.start<s.end,"exposure must be nonempty [start,end) inside supported UTC range");
    require(s.end-s.start<=max_exposure_seconds,"continuous observation duration exceeds 366 days");
}
std::string detail::dataset_identity(std::string_view digest,const ImportSettings& s) {
    return "dataset-"+sha256(std::string(digest)+"|utc-second|continuous|retain-duplicates|sort|1|"+std::to_string(s.start)+"|"+std::to_string(s.end));
}
std::shared_ptr<const Dataset> import_csv(std::string_view bytes,std::string source,const ImportSettings& settings,const simulation::ExecutionControl* control) {
    const auto began=Clock::now();validate_exposure(settings);phase(control,"Parsing");
    require(bytes.size()<=max_csv_bytes,"CSV exceeds 32 MiB; no records were truncated");
    auto data=std::make_shared<Dataset>();data->provenance.source_filename=filename(std::move(source));data->provenance.exposure=settings;
    data->provenance.sha256=sha256(bytes,control);data->provenance.dataset_id=detail::dataset_identity(data->provenance.sha256,settings);
    if(bytes.starts_with("\xef\xbb\xbf"))bytes.remove_prefix(3);
    std::size_t row=0;
    while(!bytes.empty()) {
        if(control && row%1024==0)control->check();
        const auto newline=bytes.find('\n');auto line=bytes.substr(0,newline);
        bytes=newline==std::string_view::npos?std::string_view{}:bytes.substr(newline+1);++row;
        if(line.ends_with('\r'))line.remove_suffix(1);
        const auto error=[&](const std::string& reason){return "CSV row "+std::to_string(row)+": "+reason;};
        const auto row_check=[&](bool condition,const char* reason){if(!condition)throw std::invalid_argument(error(reason));};
        row_check(line.size()<=128,"field exceeds 128 bytes");
        row_check(std::all_of(line.begin(),line.end(),[](unsigned char ch){return ch>=32 && ch<=126;}),"unsupported encoding or control character; ASCII timestamps / optional UTF-8 BOM required");
        if(line.starts_with('"')) {
            row_check(line.size()>=2 && line.ends_with('"'),"malformed quoted field");line=line.substr(1,line.size()-2);
        }
        row_check(line.find_first_of(",\";\t")==std::string_view::npos,"exactly one timestamp column required; malformed quotes or unsupported delimiter/extra columns");
        if(row==1){row_check(line=="timestamp","required header is timestamp");continue;}
        row_check(data->timestamps.size()<max_records,"record limit of 1000000 exceeded; no truncation");
        std::int64_t time{};try{time=parse_timestamp(line);}catch(const std::exception& e){throw std::invalid_argument(error(e.what()));}
        row_check(time>=settings.start && time<settings.end,"arrival outside declared [start,end) exposure");
        data->timestamps.push_back(time);
        const auto overview=static_cast<std::size_t>((time-settings.start)*64/(settings.end-settings.start));
        ++data->overview_counts[overview];
    }
    require(row>0,"CSV is empty; timestamp header required (header-only zero-arrival data is valid)");
    phase(control,"Validating");data->provenance.reordered=!std::is_sorted(data->timestamps.begin(),data->timestamps.end());
    if(data->provenance.reordered) {
        // Cooperative stable merge sort: bounded work between cancellation checks.
        std::vector<std::int64_t> scratch(data->timestamps.size());
        for(std::size_t width=1;width<data->timestamps.size();width*=2) {
            for(std::size_t i=0;i<data->timestamps.size();i+=2*width) {
                if(control)control->check();const auto mid=std::min(i+width,data->timestamps.size()),end=std::min(i+2*width,data->timestamps.size());
                std::merge(data->timestamps.begin()+i,data->timestamps.begin()+mid,data->timestamps.begin()+mid,data->timestamps.begin()+end,scratch.begin()+i);
            }data->timestamps.swap(scratch);
        }
    }
    data->provenance.accepted=data->timestamps.size();
    for(std::size_t i=1;i<data->timestamps.size();++i){if(control && i%1024==0)control->check();if(data->timestamps[i]==data->timestamps[i-1])++data->provenance.duplicates;}
    data->import_ms=elapsed(began);return data;
}
Rate estimate_rate(std::uint64_t count,std::uint64_t exposure) {
    require(count<=max_records && exposure>0 && exposure<=static_cast<std::uint64_t>(max_exposure_seconds),"count/exposure out of bounds or zero exposure");
    // Invert two Poisson Chernoff tails at alpha/2 = .025. Conservative >=95%
    // marginal coverage under independent Poisson counts, including zero/low counts.
    const auto n=static_cast<double>(count),target=std::log(40.0);
    const auto divergence=[n](double mean){return mean-n+n*std::log(n/mean);};
    double lower=0,upper=target;
    if(count) {
        double lo=0,hi=n;for(int i=0;i<80;++i){auto mid=(lo+hi)/2;if(divergence(mid)>target)lo=mid;else hi=mid;}lower=(lo+hi)/2;
        lo=n;hi=n+2*std::sqrt(2*n*target)+2*target;
        for(int i=0;i<80;++i){auto mid=(lo+hi)/2;if(divergence(mid)>target)hi=mid;else lo=mid;}upper=(lo+hi)/2;
    }
    const auto seconds=static_cast<double>(exposure);return {count,exposure,n/seconds,lower/seconds,upper/seconds};
}
void detail::validate_settings(const ImportSettings& exposure,const FitSettings& s) {
    validate_exposure(exposure);require(s.split>exposure.start && s.split<exposure.end,"split must be strictly inside exposure; chronological nonempty training and holdout required");
    require(s.model==Model::Constant || s.model==Model::DailyProfile,"unsupported calibration model");
    require(s.daily_bin_seconds>=1800 && s.daily_bin_seconds<=86400 && 86400%s.daily_bin_seconds==0,"daily bin width must divide 86400 and be 1800..86400 seconds");
    require(s.diagnostic_bin_seconds>=1 && s.diagnostic_bin_seconds<=static_cast<std::uint64_t>(max_exposure_seconds),"invalid diagnostic bin seconds");
    if(s.model==Model::DailyProfile)require(s.split-exposure.start>=86400,"daily profile needs at least one complete day of training exposure");
}
void detail::derive(Report& r,const simulation::ExecutionControl* control) {
    validate_settings(r.provenance.exposure,r.settings);require(!r.bins.empty() && r.bins.size()<=max_bins,"diagnostic report bin limit exceeded; increase bin widths or shorten exposure");
    const auto& s=r.settings;const auto count=s.model==Model::Constant?1:86400/s.daily_bin_seconds;
    std::vector<std::uint64_t> counts(count),exposures(count);std::uint64_t total=0,train=0,train_exposure=0;
    auto edge=r.provenance.exposure.start;
    for(const auto& b:r.bins) {
        require(b.start==edge && b.end==next_edge(edge,r.provenance,s) && b.holdout==(edge>=s.split),"diagnostic exposure partition or boundary ownership mismatch");
        require(b.observed<=max_records-total,"accepted arrival count exceeds dataset limit");total+=b.observed;edge=b.end;
        if(!b.holdout) {const auto i=count==1?0:static_cast<std::size_t>((b.start%86400)/s.daily_bin_seconds);counts[i]+=b.observed;exposures[i]+=b.end-b.start;train+=b.observed;train_exposure+=b.end-b.start;}
    }
    require(edge==r.provenance.exposure.end && total==r.provenance.accepted,"counts or exposure do not match dataset provenance");
    phase(control,"Fitting");auto began=Clock::now();r.baseline=estimate_rate(train,train_exposure);r.rates.clear();r.warnings.clear();r.limitations.clear();
    for(std::size_t i=0;i<count;++i){require(exposures[i]>0,"no training exposure for a daily rate bin");r.rates.push_back(estimate_rate(counts[i],exposures[i]));if(counts[i]<5)r.warnings.push_back("Rate bin "+std::to_string(i)+" has fewer than 5 training arrivals; uncertainty is large.");}
    if(r.provenance.reordered)r.warnings.push_back("Input was out of chronological order and stably sorted.");
    if(r.provenance.duplicates)r.warnings.push_back(std::to_string(r.provenance.duplicates)+" simultaneous arrivals retained; no automatic deduplication.");
    if(train_exposure<86400)r.warnings.push_back("Training exposure is shorter than one UTC day; daily variation is unassessed.");
    r.timings.fit_ms=elapsed(began);phase(control,"Holdout evaluation");began=Clock::now();
    r.training={};r.holdout={};r.baseline_training={};r.baseline_holdout={};
    for(auto* m:{&r.training,&r.holdout,&r.baseline_training,&r.baseline_holdout}){m->deviance=0;m->log_likelihood=0;}
    std::size_t zero=0;double pearson=0;
    for(auto& b:r.bins) {
        if(control)control->check();const auto exposure=static_cast<std::uint64_t>(b.end-b.start);
        const auto i=count==1?0:static_cast<std::size_t>((b.start%86400)/s.daily_bin_seconds);
        b.expected=r.rates[i].per_second*static_cast<double>(exposure);b.baseline_expected=r.baseline.per_second*static_cast<double>(exposure);b.residual=static_cast<double>(b.observed)-b.expected;
        add_metric(b.holdout?r.holdout:r.training,b.observed,b.expected,exposure);
        add_metric(b.holdout?r.baseline_holdout:r.baseline_training,b.observed,b.baseline_expected,exposure);
        if(b.holdout && b.expected>0)pearson+=b.residual*b.residual/b.expected;
        if(!b.observed)++zero;
        if(b.holdout && b.observed>=5 && static_cast<double>(b.observed)>3*std::max(1.0,b.expected))r.warnings.push_back("Descriptive spike: holdout interval "+timestamp(b.start)+" observed "+std::to_string(b.observed)+" arrivals, exceeding three times max(1, expected). No significance test.");
    }
    for(auto* m:{&r.training,&r.holdout,&r.baseline_training,&r.baseline_holdout})finish(*m);
    if(zero)r.warnings.push_back(std::to_string(zero)+" continuously observed diagnostic intervals had zero arrivals.");
    if(r.holdout.signed_error>0)r.warnings.push_back("Fitted model underpredicted the total holdout arrival count.");
    if(r.holdout.signed_error<0)r.warnings.push_back("Fitted model overpredicted the total holdout arrival count.");
    if(pearson>3*static_cast<double>(r.bins.size()))r.warnings.push_back("Large descriptive holdout Pearson residual sum; variability or distribution shift may exceed model assumptions. No hypothesis test.");
    r.limitations={"Continuous UTC observation only; gaps, closures, offsets, DST, leap seconds and subsecond timestamps are unsupported.",
        "Rates use training count / observed exposure only. Daily bins and diagnostic width are manually predefined.",
        "Conservative 95% Poisson Chernoff intervals quantify parameter uncertainty under independent Poisson counts, not future count variability. Per-bin intervals are marginal, not simultaneous.",
        "Manual model selection. Holdout is validation/model-selection data; repeated inspection is not untouched independent final-test evidence.",
        "Poisson likelihood/deviance describe count-model assumptions. A good total count prediction does not establish a good temporal pattern.",
        "Existing simulation arrivals are periodic or uniform interarrival. Periodic conversion requires explicit approximation acceptance and does not reproduce Poisson variability.",
        "Arrival records do not identify service durations. Source service and resource settings are preserved during conversion.",
        "Zero expected count with positive observations has infinite Poisson deviance and negative-infinite likelihood; exported null with explicit unavailable status."};
    r.id="calibration-"+sha256(r.provenance.dataset_id+"|1|"+std::to_string(s.split)+"|"+std::to_string(static_cast<int>(s.model))+"|"+std::to_string(s.daily_bin_seconds)+"|"+std::to_string(s.diagnostic_bin_seconds));
    r.timings.validation_ms=elapsed(began);phase(control,"Report construction");
}
std::shared_ptr<const Report> fit(std::shared_ptr<const Dataset> data,const FitSettings& settings,const simulation::ExecutionControl* control) {
    const auto began=Clock::now();
    require(bool(data),"owned validated dataset required");detail::validate_settings(data->provenance.exposure,settings);phase(control,"Partitioning");
    require(data->timestamps.size()==data->provenance.accepted && data->timestamps.size()<=max_records,"dataset count mismatch");
    for(std::size_t i=0;i<data->timestamps.size();++i){if(control && i%1024==0)control->check();const auto t=data->timestamps[i];require(t>=data->provenance.exposure.start && t<data->provenance.exposure.end && (!i || t>=data->timestamps[i-1]),"dataset must be validated and sorted inside exposure");}
    auto r=std::make_shared<Report>();r->provenance=data->provenance;r->settings=settings;std::size_t position=0;
    for(auto edge=data->provenance.exposure.start;edge<data->provenance.exposure.end;) {
        require(r->bins.size()<max_bins,"more than 1024 diagnostic intervals; increase daily/diagnostic widths or shorten exposure");
        const auto end=next_edge(edge,r->provenance,settings);const auto begin=position;
        while(position<data->timestamps.size() && data->timestamps[position]<end){if(control && position%1024==0)control->check();++position;}
        r->bins.push_back({edge,end,edge>=settings.split,position-begin});edge=end;
    }
    detail::derive(*r,control);r->timings.fit_ms=std::max(0.0,elapsed(began)-r->timings.validation_ms);r->timings.import_ms=data->import_ms;return r;
}
std::shared_ptr<const Dataset> load_csv(const std::filesystem::path& path,const ImportSettings& exposure,const simulation::ExecutionControl* control) {
    validate_exposure(exposure);phase(control,"Reading");const auto began=Clock::now();
    require(std::filesystem::is_regular_file(path),"arrival CSV must be a regular local file");
    require(std::filesystem::file_size(path)<=max_csv_bytes,"CSV exceeds 32 MiB");
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot open arrival CSV");
    std::string bytes;std::array<char,65536> chunk{};
    while(file){if(control)control->check();file.read(chunk.data(),chunk.size());const auto n=static_cast<std::size_t>(file.gcount());require(n<=max_csv_bytes-bytes.size(),"CSV exceeds 32 MiB");bytes.append(chunk.data(),n);}
    if(!file.eof())throw std::runtime_error("Arrival CSV read failed");
    const auto basename=path.filename().u8string();
    auto imported=std::const_pointer_cast<Dataset>(import_csv(bytes,std::string(basename.begin(),basename.end()),exposure,control));
    // This newly created object has not been published/shared yet. Record full I/O time
    // before returning its immutable snapshot, without copying the observation array.
    imported->import_ms=elapsed(began);phase(control,"Publication");return imported;
}
std::shared_ptr<const Report> calibrate(const FileRequest& request,const simulation::ExecutionControl* control) {
    detail::validate_settings(request.exposure,request.fit);const auto began=Clock::now();
    auto data=load_csv(request.csv,request.exposure,control);const auto imported=elapsed(began);
    auto r=std::make_shared<Report>(*fit(std::move(data),request.fit,control));r->timings.import_ms=imported;phase(control,"Publication");return r;
}
Conversion convert(const Report& r,const scenario::ScenarioDefinition& source,std::string name,std::uint64_t seed,std::int64_t start,bool accepted) {
    require(accepted,"explicitly accept periodic approximation after reviewing fitted versus represented rates; Poisson simulation is unsupported");
    scenario::validate(source);require(std::holds_alternative<scenario::WarehouseConfig>(source.domain),"warehouse source scenario required");
    require(start>=first_second && start<=last_second-source.simulation.duration_seconds,"simulation UTC start/horizon outside supported range");
    // Validate report evidence before trusting supplied rates (including callers outside the UI).
    auto validated=parse_report(report_json(r));const auto& model=*validated;
    Conversion c;c.source=source;c.scenario=source;c.scenario.name=std::move(name);c.scenario.simulation.random_seed=seed;c.calibration_id=r.id;c.utc_start=start;c.seed=seed;
    c.semantics="Periodic surrogate v1: nearest integer-second reciprocal rate (half up), arrivals at each segment start, [start,end), profile resets phase per segment. Not a Poisson process.";
    auto& w=std::get<scenario::WarehouseConfig>(c.scenario.domain);w.workload.arrivals=scenario::ArrivalMode::Profile;w.workload.arrival_min_seconds=w.workload.arrival_max_seconds=0;w.workload.segments.clear();w.shipment_interval_seconds=0;
    std::vector<std::uint32_t> intervals;
    for(const auto& rate:model.rates) {
        c.fitted_per_second.push_back(rate.per_second);
        if(rate.per_second==0){intervals.push_back(0);c.represented_per_second.push_back(0);continue;}
        const auto reciprocal=1/rate.per_second;
        require(reciprocal>=1 && reciprocal<=604800,"fitted nonzero reciprocal rate outside supported 1..604800 seconds; cannot create periodic scenario");
        const auto interval=static_cast<std::uint32_t>(std::floor(reciprocal+0.5));intervals.push_back(interval);c.represented_per_second.push_back(1.0/interval);
    }
    for(std::uint32_t t=0;t<source.simulation.duration_seconds;) {
        const auto utc=start+t;const auto i=model.settings.model==Model::Constant?0:static_cast<std::size_t>((utc%86400)/model.settings.daily_bin_seconds);
        const auto end=model.settings.model==Model::Constant?source.simulation.duration_seconds:
            std::min(source.simulation.duration_seconds,t+model.settings.daily_bin_seconds-static_cast<std::uint32_t>(utc%model.settings.daily_bin_seconds));
        if(intervals[i]){require(w.workload.segments.size()<scenario::max_demand_segments,"conversion exceeds 64 profile segments; shorten source simulation horizon");w.workload.segments.push_back({t,end,intervals[i]});}t=end;
    }
    scenario::validate(c.scenario);return c;
}
}
