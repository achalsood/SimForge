#include <simforge/workspace/scenario_workspace.hpp>
#include <simforge/workspace/experiment_execution.hpp>
#include <algorithm>

namespace simforge::workspace {
void ScenarioWorkspace::start_checkpoint(analysis::checkpoint::Request request,ExperimentExecution::Observer observer){
    if(pending_study_)throw std::logic_error("Retain or discard pending evidence before starting another study");
    ExperimentRequest input;input.baseline=request.source;input.checkpoint_study=std::move(request);execution_->start(std::move(input),std::move(observer));
}
void ScenarioWorkspace::simulate_checkpoint(std::uint64_t seed,bool intervention){
    if(!report_ || !report_->checkpoint_study)throw std::logic_error("Select a checkpoint intervention study");
    const auto& r=*report_->checkpoint_study;auto branches=analysis::checkpoint::reconstruct(r.request,seed);
    const auto found=std::find_if(r.outcomes.begin(),r.outcomes.end(),[&](const auto& row){return row.seed==seed;});
    if(found==r.outcomes.end() || *found!=branches.outcome)throw std::runtime_error("Selected-seed reconstruction differs from retained evidence");
    auto definition=r.request.source;definition.simulation.random_seed=seed;
    auto prepared=std::make_unique<viewer::ViewerController>(definition,speed_);
    auto initial=prepared->runtime().capture_snapshot();auto clock=prepared->runtime().clock().capture_snapshot();clock.paused=true;
    auto commands=(intervention?branches.intervention:branches.control).capacity_changes;
    prepared->restore_session(initial,clock,prepared->orbit_camera().pose(),{},std::move(commands));
    prepared->seek(r.request.checkpoint_tick);prepared->set_viewport(view().viewport());
    view().input({viewer::InputKind::FocusLost},{});preview_view_=std::move(prepared);
    viewed_identity_="CHECKPOINT PLAYBACK | seed "+std::to_string(seed)+(intervention?" | Intervention":" | Control")+" | checkpoint tick "+std::to_string(r.request.checkpoint_tick);
    preview_report_generation_=report_generation_;
}
}
