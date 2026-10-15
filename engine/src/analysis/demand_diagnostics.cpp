#include <simforge/analysis/demand_diagnostics.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <set>

namespace simforge::analysis::diagnostics {
namespace {
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
void require(bool ok,const char* message){if(!ok)throw std::invalid_argument(std::string("Demand diagnostics: ")+message);}
void progress(const simulation::ExecutionControl* c,const char* phase,std::uint64_t n=0,std::uint64_t total=0){if(c)c->update(phase,{},n,total);}
struct Mass {
    std::uint64_t first;
    std::vector<double> weights;
    double total;
};
Mass mass(double mean) {
    require(std::isfinite(mean) && mean>=0 && mean<=max_expected_count,"Poisson mean must be finite and in 0..80000");
    if(mean==0)return {0,{1},1};
    // Relative masses about the mode avoid exp(-mean) underflow. At the
    // supported means, omitted tails beyond this window have Chernoff bounds
    // below 1e-30; arithmetic remains binary64, not symbolic exact quantiles.
    const auto radius=14*std::sqrt(mean)+100;
    const auto first=static_cast<std::uint64_t>(std::max(0.0,std::floor(mean-radius)));
    const auto last=static_cast<std::uint64_t>(std::ceil(mean+radius));
    const auto mode=static_cast<std::uint64_t>(std::floor(mean));
    Mass m{first,std::vector<double>(static_cast<std::size_t>(last-first+1)),0};
    m.weights[mode-first]=1;
    for(auto k=mode;k>first;--k)m.weights[k-1-first]=m.weights[k-first]*static_cast<double>(k)/mean;
    for(auto k=mode;k<last;++k)m.weights[k+1-first]=m.weights[k-first]*mean/static_cast<double>(k+1);
    m.total=std::accumulate(m.weights.begin(),m.weights.end(),0.0);return m;
}
std::pair<double,double> rate_bounds(const demand::Rate& rate,double alpha,std::size_t parameters) {
    const auto n=static_cast<double>(rate.count),target=std::log(4*static_cast<double>(parameters)/alpha);
    const auto divergence=[n](double mu){return mu-n+n*std::log(n/mu);};
    double lower=0,upper=target;
    if(rate.count) {
        double lo=0,hi=n;
        for(int i=0;i<80;++i){const auto mid=(lo+hi)/2;if(divergence(mid)>target)lo=mid;else hi=mid;}lower=(lo+hi)/2;
        lo=n;hi=n+2*std::sqrt(2*n*target)+2*target;
        for(int i=0;i<80;++i){const auto mid=(lo+hi)/2;if(divergence(mid)>target)hi=mid;else lo=mid;}upper=(lo+hi)/2;
    }
    return {lower/static_cast<double>(rate.exposure_seconds),upper/static_cast<double>(rate.exposure_seconds)};
}
std::vector<Piece> pieces(const demand::Report& source,std::int64_t start,std::int64_t end,bool observed_only) {
    std::vector<Piece> result;
    for(auto t=start;t<end;) {
        const auto index=source.settings.model==demand::Model::Constant?0U:static_cast<std::uint32_t>((t%86400)/source.settings.daily_bin_seconds);
        auto edge=std::min(end,t+604800); // Actual generator supports at most seven days per invocation.
        if(source.settings.model==demand::Model::DailyProfile)edge=std::min(edge,(t/source.settings.daily_bin_seconds+1)*source.settings.daily_bin_seconds);
        bool observed=true;
        if(observed_only && !source.provenance.exposure.windows.empty()) {
            const auto& windows=source.provenance.exposure.windows;
            const auto it=std::upper_bound(windows.begin(),windows.end(),t,[](auto value,const auto& w){return value<w.end;});
            require(it!=windows.end(),"coverage ownership failure");edge=std::min(edge,it->end);observed=it->status==demand::CoverageStatus::Observed;
        }
        if(observed)result.push_back({t,edge,index});t=edge;
    }
    return result;
}
Prediction prediction(const demand::Report& source,std::span<const Piece> spans,double level) {
    Prediction p;double mean=0,lower=0,upper=0;
    const auto parameters=static_cast<std::size_t>(std::count_if(source.rates.begin(),source.rates.end(),[](const auto& r){return r.available;}));
    for(const auto& piece:spans) {
        const auto& rate=source.rates.at(piece.rate_index);if(!rate.available)return p;
        const auto exposure=static_cast<double>(piece.end-piece.start);
        mean+=rate.per_second*exposure;
        const auto bounds=rate_bounds(rate,1-level,parameters);lower+=bounds.first*exposure;upper+=bounds.second*exposure;
    }
    require(std::isfinite(mean) && mean<=max_expected_count,"integrated bin/prediction mean exceeds 80000");
    p.mean=mean;p.process=process_interval(mean,level);p.lower_mean=lower;p.upper_mean=upper;
    if(upper<=max_expected_count)p.parameter_aware=Interval{poisson_quantile(lower,(1-level)/4),poisson_quantile(upper,1-(1-level)/4)};
    return p;
}
Coverage coverage(std::span<const Bin> bins,bool parameter) {
    Coverage c;double width=0;
    for(const auto& b:bins) {
        const auto& interval=parameter?b.prediction.parameter_aware:b.prediction.process;
        if(!b.available || !interval)continue;
        ++c.evaluated;width+=static_cast<double>(interval->upper-interval->lower);
        if(b.count<interval->lower)++c.below;else if(b.count>interval->upper)++c.above;else ++c.inside;
    }
    if(c.evaluated){c.mean_width=width/static_cast<double>(c.evaluated);c.fraction=static_cast<double>(c.inside)/static_cast<double>(c.evaluated);}return c;
}
Reference reference(std::optional<double> observed,std::vector<double> samples,double level) {
    Reference r;if(!observed || samples.empty())return r;
    const auto n=static_cast<double>(samples.size());std::size_t below=0,above=0;
    r.evaluated_replications=static_cast<std::uint32_t>(samples.size());
    for(auto x:samples){below+=x<=*observed;above+=x>=*observed;}
    r.lower_rank=(1+static_cast<double>(below))/(n+1);r.upper_rank=(1+static_cast<double>(above))/(n+1);
    const auto fraction=static_cast<double>(above)/n;r.mc_standard_error=std::sqrt(fraction*(1-fraction)/n);
    constexpr double z=1.959963984540054;const auto divisor=1+z*z/n,center=(fraction+z*z/(2*n))/divisor;
    const auto half=z*std::sqrt(fraction*(1-fraction)/n+z*z/(4*n*n))/divisor;
    r.upper_tail_wilson_lower=std::max(0.0,center-half);r.upper_tail_wilson_upper=std::min(1.0,center+half);
    std::sort(samples.begin(),samples.end());
    const auto quantile=[&](double q){return samples[static_cast<std::size_t>(std::ceil(q*n)-1)];};
    r.lower=quantile((1-level)/2);r.median=quantile(.5);r.upper=quantile(1-(1-level)/2);return r;
}
}
double poisson_cdf(double mean,std::int64_t count) {
    const auto m=mass(mean);if(count<0 || static_cast<std::uint64_t>(count)<m.first)return 0;
    if(static_cast<std::uint64_t>(count)>=m.first+m.weights.size()-1)return 1;
    return std::accumulate(m.weights.begin(),m.weights.begin()+static_cast<std::ptrdiff_t>(static_cast<std::uint64_t>(count)-m.first+1),0.0)/m.total;
}
std::uint64_t poisson_quantile(double mean,double probability) {
    require(std::isfinite(probability) && probability>=1e-8 && probability<=1-1e-8,"quantile probability must be in [1e-8,1-1e-8]");
    const auto m=mass(mean);double cumulative=0;
    for(std::size_t i=0;i<m.weights.size();++i){cumulative+=m.weights[i];if(cumulative>=probability*m.total)return m.first+i;}
    throw std::runtime_error("Poisson quantile exhausted bounded numerical support");
}
Interval process_interval(double mean,double level) {
    require(std::isfinite(level) && level>=.8 && level<=.99,"nominal coverage must be .80.. .99");
    return {poisson_quantile(mean,(1-level)/2),poisson_quantile(mean,1-(1-level)/2)};
}
Prediction predict(const demand::Report& source,std::int64_t start,std::int64_t end,double level) {
    require(start>=source.provenance.exposure.end,"future prediction must start at or after the complete original observation horizon");
    require(start>=0 && end<=4102444800LL && end>start && end-start<=604800,"prediction UTC horizon must be 1..604800 seconds inside supported UTC range");
    return prediction(source,pieces(source,start,end,false),level);
}
Statistics statistics(std::span<const Bin> bins,std::span<const std::uint64_t> counts,const Settings& s,std::size_t fitted_parameters) {
    require(bins.size()==counts.size(),"bin/count mismatch");
    Statistics st;double pearson=0;std::vector<std::pair<double,double>> pairs;std::optional<double> previous;
    std::int64_t previous_end=-1;
    for(std::size_t i=0;i<bins.size();++i) {
        const auto& b=bins[i];const auto n=counts[i];
        if(!b.available || !b.prediction.mean){previous.reset();continue;}
        const auto mu=*b.prediction.mean;++st.valid_bins;st.zeros+=n==0;st.tail_bins+=n>=s.tail_threshold;st.maximum=std::max(st.maximum,n);
        st.expected_zeros+=std::exp(-mu);st.expected_tail_bins+=1-poisson_cdf(mu,static_cast<std::int64_t>(s.tail_threshold)-1);
        if(mu==0){st.zero_mean_positive+=n>0;previous.reset();continue;}
        ++st.positive_bins;const auto residual=(static_cast<double>(n)-mu)/std::sqrt(mu);pearson+=residual*residual;
        if(b.fully_observed && previous && previous_end==b.start)pairs.emplace_back(*previous,residual);
        previous=b.fully_observed?std::optional(residual):std::nullopt;previous_end=b.end;
    }
    st.residual_degrees_of_freedom=static_cast<std::int64_t>(st.positive_bins)-static_cast<std::int64_t>(s.partition==Partition::Training?fitted_parameters:0);
    if(st.positive_bins && !st.zero_mean_positive){st.pearson=pearson;if(st.residual_degrees_of_freedom>0)st.dispersion=pearson/static_cast<double>(st.residual_degrees_of_freedom);}
    st.adjacent_pairs=pairs.size();
    if(pairs.size()>=8) {
        double a=0,b=0;for(const auto& [x,y]:pairs){a+=x;b+=y;}a/=static_cast<double>(pairs.size());b/=static_cast<double>(pairs.size());
        double xx=0,yy=0,xy=0;for(const auto& [x,y]:pairs){xx+=(x-a)*(x-a);yy+=(y-b)*(y-b);xy+=(x-a)*(y-b);}
        if(xx>0 && yy>0)st.lag_one=std::clamp(xy/std::sqrt(xx*yy),-1.0,1.0);
    }
    return st;
}
std::shared_ptr<const Report> run(Request request,std::shared_ptr<const demand::Dataset> data,const simulation::ExecutionControl* control) {
    const auto began=Clock::now();progress(control,"Validating frozen calibration");
    require(bool(request.source),"completed calibration required");request.source=demand::parse_report(demand::report_json(*request.source));
    const auto& source=*request.source;const auto& s=request.settings;
    require(s.partition==Partition::Training || s.partition==Partition::Holdout,"invalid partition");
    require(s.bin_seconds>=1 && s.bin_seconds<=604800,"bin width must be 1..604800 seconds");
    require(s.replications>=1 && s.replications<=max_replications,"replications must be 1..1000");
    require(s.tail_threshold>=1 && s.tail_threshold<=demand::max_records,"tail threshold must be 1..1000000");
    require(std::isfinite(s.level) && s.level>=.8 && s.level<=.99,"coverage level must be .80.. .99");
    require(s.prediction_seconds>=1 && s.prediction_seconds<=604800,"prediction duration must be 1..604800 seconds");
    require(s.prediction_start>=source.provenance.exposure.end && s.prediction_start<=4102444800LL-s.prediction_seconds,"future UTC horizon must follow the original observations and fit the supported calendar range");
    if(!request.csv.empty())data=demand::load_csv(request.csv,source.provenance.exposure,control);
    if(data) {
        progress(control,"Verifying original CSV fingerprint");
        require(data->provenance.sha256==source.provenance.sha256 && data->provenance.dataset_id==source.provenance.dataset_id && data->provenance.exposure==source.provenance.exposure,"reimport fingerprint/coverage differs from source calibration; no model was refitted");
        require(data->timestamps.size()==source.provenance.accepted && std::is_sorted(data->timestamps.begin(),data->timestamps.end()),"validated reimport counts/order mismatch");
        std::size_t pos=0;
        for(const auto& b:source.bins){const auto first=pos;while(pos<data->timestamps.size() && data->timestamps[pos]<b.end)++pos;require(pos-first==b.observed,"reimport counts differ from frozen calibration evidence");if(control)control->check();}
    }
    auto r=std::make_shared<Report>();r->request=std::move(request);r->request.csv.clear();
    r->data_availability=data?"fingerprint_verified_observations":"exact_coarsening_of_retained_calibration_counts";
    const auto start=s.partition==Partition::Training?source.provenance.exposure.start:source.settings.split;
    const auto end=s.partition==Partition::Training?source.settings.split:source.provenance.exposure.end;
    require((end-start+s.bin_seconds-1)/s.bin_seconds<=static_cast<std::int64_t>(max_bins),"at most 1024 diagnostic bins; increase width");
    std::size_t total_pieces=0;double expected=0;std::set<std::uint32_t> parameters;
    for(auto t=start;t<end;) {
        progress(control,"Preparing observed bins",r->bins.size(),static_cast<std::uint64_t>((end-start+s.bin_seconds-1)/s.bin_seconds));
        Bin b;b.start=t;b.end=std::min(end,t+s.bin_seconds);b.pieces=pieces(source,b.start,b.end,true);total_pieces+=b.pieces.size();
        require(total_pieces<=max_pieces,"at most 4096 observed intensity pieces");
        b.exposure_seconds=demand::observed_exposure(source.provenance.exposure,b.start,b.end);b.fully_observed=b.exposure_seconds==static_cast<std::uint64_t>(b.end-b.start);
        if(data)b.count=static_cast<std::uint64_t>(std::lower_bound(data->timestamps.begin(),data->timestamps.end(),b.end)-std::lower_bound(data->timestamps.begin(),data->timestamps.end(),b.start));
        else for(const auto& old:source.bins)if(old.start<b.end && old.end>b.start) {
            require(old.start>=b.start && old.end<=b.end,"new bin edges require original CSV reimport; retained counts cannot be subdivided");b.count+=old.observed;
        }
        if(b.exposure_seconds) {
            b.prediction=prediction(source,b.pieces,s.level);b.available=b.prediction.mean.has_value();
            if(b.available){expected+=*b.prediction.mean;if(*b.prediction.mean>0)b.residual=(static_cast<double>(b.count)-*b.prediction.mean)/std::sqrt(*b.prediction.mean);}
            for(const auto& piece:b.pieces)if(source.rates[piece.rate_index].available && source.rates[piece.rate_index].per_second>0)parameters.insert(piece.rate_index);
        }
        r->bins.push_back(std::move(b));t=r->bins.back().end;
    }
    require(expected<=max_expected_count && expected*s.replications<=max_expected_work,"synthetic expected-work limit: 80000 per replication and 5000000 total");
    require(total_pieces*s.replications<=1'000'000,"synthetic invocation limit of 1000000 exceeded");
    for(const auto& bin:r->bins)if(bin.available)for(const auto& piece:bin.pieces) {
        const auto rate=source.rates[piece.rate_index].per_second,mean=rate*static_cast<double>(piece.end-piece.start);
        require(rate<=scenario::max_poisson_rate && (mean==0 || mean>=scenario::min_positive_poisson_mean),"fitted intensity outside generator-2 numerical support");
    }
    std::vector<std::uint64_t> counts;for(const auto& b:r->bins)counts.push_back(b.count);
    r->observed=statistics(r->bins,counts,s,parameters.size());r->process_coverage=coverage(r->bins,false);r->parameter_coverage=coverage(r->bins,true);
    r->future=predict(source,s.prediction_start,s.prediction_start+s.prediction_seconds,s.level);
    r->preparation_ms=elapsed(began);const auto simulation_began=Clock::now();
    scenario::ScenarioDefinition synthetic;synthetic.name="Frozen demand diagnostic exposure";synthetic.simulation.tick_rate=1;
    scenario::WarehouseConfig config;config.shipment_interval_seconds=0;config.workload.arrivals=scenario::ArrivalMode::Poisson;synthetic.domain=config;
    auto seed_state=s.seed^0x7463e0ef3b19a821ULL;
    for(std::uint32_t rep=0;rep<s.replications;++rep) {
        progress(control,"Simulating fixed-parameter reference",rep,s.replications);
        Replication sample;sample.seed=scenario::workload::next_random(seed_state);
        auto piece_state=sample.seed^0x199acb4470bbdc21ULL;std::fill(counts.begin(),counts.end(),0);
        std::uint64_t realized=0;
        for(std::size_t i=0;i<r->bins.size();++i) {
            if(control)control->check();const auto& bin=r->bins[i];if(!bin.available)continue;
            for(const auto& piece:bin.pieces) {
                synthetic.simulation.duration_seconds=static_cast<std::uint32_t>(piece.end-piece.start);
                synthetic.simulation.random_seed=scenario::workload::next_random(piece_state);
                std::get<scenario::WarehouseConfig>(synthetic.domain).workload.rate_per_second=source.rates[piece.rate_index].per_second;
                const simulation::ExecutionControl inner{control?control->stop:std::stop_token{}, {}};
                const auto arrivals=scenario::workload::poisson_ticks(synthetic,&inner,static_cast<std::uint32_t>(scenario::max_warehouse_shipments-realized));
                counts[i]+=arrivals.size();realized+=arrivals.size();
            }
        }
        sample.statistics=statistics(r->bins,counts,s,parameters.size());r->replications.push_back(std::move(sample));
        progress(control,"Aggregating reference statistics",rep+1,s.replications);
    }
    std::vector<double> dispersion,zeros,tails,lags;
    for(const auto& rep:r->replications){if(rep.statistics.dispersion)dispersion.push_back(*rep.statistics.dispersion);zeros.push_back(static_cast<double>(rep.statistics.zeros));tails.push_back(static_cast<double>(rep.statistics.tail_bins));if(rep.statistics.lag_one)lags.push_back(std::abs(*rep.statistics.lag_one));}
    r->dispersion_reference=reference(r->observed.dispersion,std::move(dispersion),s.level);
    r->zero_reference=reference(r->observed.valid_bins?std::optional(static_cast<double>(r->observed.zeros)):std::nullopt,std::move(zeros),s.level);
    r->tail_reference=reference(r->observed.valid_bins?std::optional(static_cast<double>(r->observed.tail_bins)):std::nullopt,std::move(tails),s.level);
    r->lag_reference=reference(r->observed.lag_one?std::optional(std::abs(*r->observed.lag_one)):std::nullopt,std::move(lags),s.level);
    r->assessment="Insufficient comparable observations for a reliable dispersion assessment.";
    if(r->observed.zero_mean_positive)r->assessment="Positive counts occur where the frozen model predicts zero. Model mismatch is visible.";
    else if(r->observed.positive_bins>=8 && s.replications>=100 && r->dispersion_reference.lower) {
        if(*r->observed.dispersion>*r->dispersion_reference.upper)r->assessment="Observed count variability exceeds the fixed-parameter Poisson reference range.";
        else if(*r->observed.dispersion<*r->dispersion_reference.lower)r->assessment="Observed count variability is below the fixed-parameter Poisson reference range.";
        else r->assessment="Observed count variability is broadly compatible with the selected fixed-parameter Poisson check.";
    }
    r->warnings={"Fixed-parameter simulation checks condition on estimated rates; they do not include parameter or model uncertainty.",
        "Training reference checks are descriptive: synthetic samples are not refitted. No calibrated training goodness-of-fit p-value is claimed.",
        "Tail ranks use (1 + inclusive ties)/(replications + 1). Finite Monte Carlo resolution and multiplicity preclude proof of model adequacy.",
        "Lag-one correlation uses at least eight fully observed adjacent pairs and is descriptive; it does not diagnose individual-arrival clustering.",
        "Process-only intervals condition on frozen fitted rates. Parameter-aware envelopes allocate half the error budget to simultaneous Chernoff rate bounds and half to future process tails.",
        "Parameter-aware coverage assumes independent Poisson training counts and an unchanged future intensity profile. Model misspecification is not included.",
        "Validation bins are disjoint. Marginal interval coverage is not simultaneous coverage or an untouched independent final-test guarantee.",
        "Synthetic observations use generator 2 at 1 Hz on integer UTC seconds, only inside observed [start,end) pieces. Windows/MSVC bitwise support is unchanged."};
    if(s.exploratory)r->warnings.push_back("Exploratory/manual settings; thresholds may have been chosen after inspection. Repeated holdout use is not independent confirmation.");
    else r->warnings.push_back("Settings asserted predefined by the requester; the software cannot verify they preceded observation inspection. Repeated holdout use remains untracked.");
    if(r->observed.valid_bins<r->bins.size())r->warnings.push_back("Some bins have no observed exposure or unavailable fitted rates; excluded bins are retained and not treated as observed zero demand.");
    if(!r->future.mean)r->warnings.push_back("Future horizon includes unestimated rate bins; predictive bounds are unavailable.");
    if(r->future.mean && !r->future.parameter_aware)r->warnings.push_back("Parameter-aware future envelope is unavailable: its upper integrated rate bound exceeds the 80000-mean numerical support limit.");
    if(r->observed.residual_degrees_of_freedom<=0)r->warnings.push_back("Nonpositive residual degrees of freedom; dispersion ratio is unavailable.");
    if(s.replications<100)r->warnings.push_back("Fewer than 100 replications: reference assessment is inconclusive.");
    r->simulation_ms=elapsed(simulation_began);progress(control,"Constructing immutable diagnostic report",s.replications,s.replications);
    // The codec computes a content identity over frozen settings, observed and simulated evidence.
    return parse_report(report_json(*r));
}
void detail::validate_retained_report(const Report& r) {
    const auto& source=*r.request.source;const auto& s=r.request.settings;
    std::vector<std::uint64_t> counts;std::set<std::uint32_t> parameters;std::uint64_t total=0;double expected=0;std::size_t total_pieces=0;
    for(const auto& b:r.bins) {
        const auto spans=pieces(source,b.start,b.end,true);
        require(b.pieces==spans && b.exposure_seconds==demand::observed_exposure(source.provenance.exposure,b.start,b.end),"retained coverage/pieces mismatch");
        require(b.fully_observed==(b.exposure_seconds==static_cast<std::uint64_t>(b.end-b.start)),"retained coverage status mismatch");
        const auto p=b.exposure_seconds?prediction(source,spans,s.level):Prediction{};
        require(p==b.prediction && b.available==(b.exposure_seconds && p.mean.has_value()),"retained prediction mismatch");
        const auto residual=p.mean && *p.mean>0?std::optional((static_cast<double>(b.count)-*p.mean)/std::sqrt(*p.mean)):std::nullopt;
        require(b.residual==residual && (b.exposure_seconds || b.count==0),"retained residual/count mismatch");
        counts.push_back(b.count);total+=b.count;total_pieces+=spans.size();if(b.available)expected+=*p.mean;
        for(const auto& span:spans)if(source.rates[span.rate_index].available && source.rates[span.rate_index].per_second>0)parameters.insert(span.rate_index);
    }
    require(total==(s.partition==Partition::Training?source.training.observed:source.holdout.observed),"retained count total differs from calibration partition");
    require(expected<=max_expected_count && expected*s.replications<=max_expected_work && total_pieces*s.replications<=1'000'000,"retained synthetic work exceeds limits");
    const auto st=statistics(r.bins,counts,s,parameters.size());require(st==r.observed,"retained observed statistics mismatch");
    require(coverage(r.bins,false)==r.process_coverage && coverage(r.bins,true)==r.parameter_coverage,"retained interval coverage mismatch");
    require(predict(source,s.prediction_start,s.prediction_start+s.prediction_seconds,s.level)==r.future,"retained future prediction mismatch");
    std::vector<double> dispersion,zeros,tails,lags;
    for(const auto& sample:r.replications) {
        const auto& v=sample.statistics;
        require(v.valid_bins==st.valid_bins && v.positive_bins==st.positive_bins && v.adjacent_pairs==st.adjacent_pairs && v.expected_zeros==st.expected_zeros && v.expected_tail_bins==st.expected_tail_bins && v.residual_degrees_of_freedom==st.residual_degrees_of_freedom && !v.zero_mean_positive,"retained reference model structure mismatch");
        require((!v.dispersion && v.residual_degrees_of_freedom<=0) || (v.pearson && v.dispersion && *v.dispersion==*v.pearson/static_cast<double>(v.residual_degrees_of_freedom)),"retained reference dispersion mismatch");
        if(v.dispersion)dispersion.push_back(*v.dispersion);zeros.push_back(static_cast<double>(v.zeros));tails.push_back(static_cast<double>(v.tail_bins));if(v.lag_one)lags.push_back(std::abs(*v.lag_one));
    }
    require(reference(st.dispersion,std::move(dispersion),s.level)==r.dispersion_reference,"retained dispersion reference mismatch");
    require(reference(st.valid_bins?std::optional(static_cast<double>(st.zeros)):std::nullopt,std::move(zeros),s.level)==r.zero_reference,"retained zero reference mismatch");
    require(reference(st.valid_bins?std::optional(static_cast<double>(st.tail_bins)):std::nullopt,std::move(tails),s.level)==r.tail_reference,"retained tail reference mismatch");
    require(reference(st.lag_one?std::optional(std::abs(*st.lag_one)):std::nullopt,std::move(lags),s.level)==r.lag_reference,"retained lag reference mismatch");
}
}
