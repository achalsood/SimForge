#pragma once
#include <simforge/analysis/checkpoint_intervention.hpp>
#include <array>

inline std::array<simforge::analysis::checkpoint::Request,5> checkpoint_examples(){
    using namespace simforge;namespace cp=analysis::checkpoint;std::array<cp::Request,5> examples;
    for(std::size_t i=0;i<examples.size();++i){auto& r=examples[i];r.source={1,"Checkpoint example",{10,28800,42},scenario::WarehouseConfig{6,10,2,0,600}};r.checkpoint_tick=72000;r.plan.count=20;r.plan.base_seed=330001+i*1000;
        auto& c=std::get<scenario::WarehouseConfig>(r.source.domain);c.workload.arrivals=scenario::ArrivalMode::Poisson;c.workload.rate_per_second=1./120.;}
    examples[0].source.name="Add a third forklift after two hours";
    examples[1].source.name="Add workers after congestion";{auto& c=std::get<scenario::WarehouseConfig>(examples[1].source.domain);c.workers=2;c.forklifts=6;examples[1].intervention={warehouse::ResourceType::Worker,cp::Action::Increase,2};}
    examples[2].source.name="Add loading bay capacity";{auto& c=std::get<scenario::WarehouseConfig>(examples[2].source.domain);c.loading_bays=1;c.forklifts=8;examples[2].intervention={warehouse::ResourceType::LoadingBay,cp::Action::Increase,1};}
    examples[3].source.name="Extra forklift with no measurable benefit";{auto& c=std::get<scenario::WarehouseConfig>(examples[3].source.domain);c.workers=1;c.forklifts=5;}
    examples[4].source.name="Intervene at a seeded Poisson surge";{auto& c=std::get<scenario::WarehouseConfig>(examples[4].source.domain);c.workload.arrivals=scenario::ArrivalMode::PiecewisePoisson;c.workload.rate_per_second=0;c.workload.intensities={{0,7200,.002},{7200,18000,.035},{18000,28800,.004}};}
    return examples;
}
