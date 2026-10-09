#include <simforge/workspace/presentation.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace simforge::workspace
{
const analysis::CandidateResult* best_tested(const Comparison& report)
{
    if(report.ranking.empty()) return nullptr;
    const auto found=std::find_if(report.candidates.begin(),report.candidates.end(),[&](const auto& c){return c.candidate.name==report.ranking.front();});
    if(found==report.candidates.end() || !found->rank || *found->rank!=1) throw std::invalid_argument("Inconsistent comparison ranking");
    return &*found;
}
MetricBars metric_bars(std::optional<double> baseline, std::optional<double> candidate)
{
    for(const auto v:{baseline,candidate}) if(v && (!std::isfinite(*v) || *v<0)) throw std::invalid_argument("Chart metrics must be finite and nonnegative");
    if(!baseline || !candidate) return {};
    const auto maximum=std::max(*baseline,*candidate);
    return {maximum>0?*baseline/maximum:0,maximum>0?*candidate/maximum:0,true};
}
}
