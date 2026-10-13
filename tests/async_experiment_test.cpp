#include <simforge/workspace/experiment_execution.hpp>
#include <condition_variable>
#include <iostream>
#include <source_location>
#include <set>

using namespace simforge;
using namespace simforge::workspace;
using Clock = std::chrono::steady_clock;
namespace {
void check(bool value, std::source_location p = std::source_location::current())
{ if (!value) throw std::runtime_error("Async check failed at line "+std::to_string(p.line())); }
template<class F> void rejects(F f) { bool threw=false; try { f(); } catch (const std::exception&) { threw=true; } check(threw); }
scenario::ScenarioDefinition definition(bool large = false)
{
    scenario::ScenarioDefinition d; d.name="Async baseline"; d.simulation={60,large ? 100000u : 120u,42};
    d.domain=scenario::WarehouseConfig{4,8,2,1,4}; return d;
}
void add(ScenarioWorkspace& w, std::string name="Candidate")
{ w.edit(Field::Name,std::move(name)); w.edit(Field::Forklifts,"3"); w.add_candidate(); }
struct Gate {
    std::mutex mutex; std::condition_variable cv; bool reached=false, released=false;
    ExperimentExecution::Observer observer(std::string phase, bool fail=false) {
        return [this,phase=std::move(phase),fail,candidate=false,probe=false](auto p,auto,auto tick,auto) mutable {
            if (p=="Evaluating candidate") candidate=true;
            if (p=="Bottleneck probes") probe=true;
            if (phase=="Probe simulation") {if (!probe || p!="Simulating" || tick==0) return;}
            else {
                if (p != phase) return;
                if (phase=="Simulating" && (!candidate || tick==0)) return;
            }
            std::unique_lock lock{mutex}; if (reached) return; reached=true; cv.notify_all();
            if (!cv.wait_for(lock,std::chrono::seconds{10},[&]{return released;})) throw std::runtime_error("Gate release timeout");
            if (fail) throw std::runtime_error("Injected candidate failure");
        };
    }
    void wait() { std::unique_lock lock{mutex}; check(cv.wait_for(lock,std::chrono::seconds{10},[&]{return reached;})); }
    void release() { std::lock_guard lock{mutex}; released=true; cv.notify_all(); }
    ~Gate() { release(); }
};
ExperimentProgress finish(ExperimentExecution& service) {
    const auto deadline=Clock::now()+std::chrono::seconds{20}; std::uint64_t completed=0;
    for (;;) {
        auto p=service.progress(); check(p.completed_candidates>=completed && p.completed_candidates<=p.total_candidates);
        completed=p.completed_candidates;
        if (!p.active()) return p;
        check(Clock::now()<deadline); std::this_thread::yield();
    }
}
void equivalence(ComparisonMode mode, analysis::Objective objective, bool explicit_tick) {
    ScenarioWorkspace w{definition()}; add(w,"Zulu"); add(w,"Alpha");
    w.view().seek(30*60);
    const auto viewer=w.baseline_viewer().runtime().capture_snapshot();
    auto request=w.capture_request(mode,objective,explicit_tick ? std::optional<std::uint64_t>{30*60} : std::nullopt);
    if (mode==ComparisonMode::Checkpoint && !explicit_tick) {
        auto historical=warehouse::WarehouseSimulation::create_from(*request.checkpoint);
        historical.set_forklift_capacity(3); request.checkpoint=historical.capture_snapshot();
    }
    const auto expected=execute_experiment(request);
    ExperimentExecution service; auto id=service.start(request); check(finish(service).state==ExecutionState::Completed);
    check(!service.take_result(id+1)); auto actual=service.take_result(id); check(actual.has_value() && actual->request_id==id);
    check(actual->candidate_ids==expected.candidate_ids && actual->executions==expected.executions && actual->ranking==expected.ranking && actual->candidates==expected.candidates && actual->bottlenecks==expected.bottlenecks);
    check(actual->final_snapshots.size()==3); check(!service.take_result(id));
    for (std::size_t i=0;i<actual->final_snapshots.size();++i) {
        auto branch=warehouse::WarehouseSimulation::create_from(actual->final_snapshots[i]);
        check(branch.metrics()==(i==0 ? actual->baseline.metrics : actual->candidates[i-1].metrics));
        const auto m=branch.metrics(); check(m.arrived==m.completed+m.waiting+m.in_progress);
    }
    check(w.baseline_viewer().runtime().capture_snapshot()==viewer);
    w.execution().start(request); check(finish(w.execution()).state==ExecutionState::Completed); if(!w.poll_experiment()) throw std::runtime_error(w.message());
    w.view_result({}); check(w.view().observation().metrics==expected.baseline.metrics);
    w.view_result("Alpha"); check(w.view().observation().metrics==expected.candidates.front().metrics);
    w.return_to_baseline(); check(w.view().runtime().capture_snapshot()==viewer);
}
void run(std::string_view test) {
    if (test=="equivalence") {
        for (auto objective:{analysis::Objective::MaximizeCompleted,analysis::Objective::MinimizeAverageWait,analysis::Objective::MinimizeWaiting}) {
            equivalence(ComparisonMode::Initial,objective,false); equivalence(ComparisonMode::Checkpoint,objective,false); equivalence(ComparisonMode::Checkpoint,objective,true);
        }
        return;
    }
    ScenarioWorkspace w{definition(test=="long_cancel")}; add(w);
    if (test=="capture") {
        auto request=w.capture_request(ComparisonMode::Checkpoint,analysis::Objective::MaximizeCompleted);
        auto copy=request; w.edit(Field::Name,"Edited draft"); w.rename_candidate(w.candidates()[0].id,"Renamed"); w.view().seek(60);
        check(request.candidates[0].definition.name=="Candidate" && request.candidates[0].id==copy.candidates[0].id && request.checkpoint==copy.checkpoint);
        rejects([&]{ auto bad=request; bad.candidates.clear(); validate_experiment_request(bad); });
        rejects([&]{ auto bad=request; bad.checkpoint_tick=999999999; validate_experiment_request(bad); });
        rejects([&]{ auto bad=request; bad.candidates[0].definition.simulation.tick_rate=1; validate_experiment_request(bad); });
        rejects([&]{ auto bad=request; bad.candidates.push_back(bad.candidates[0]); validate_experiment_request(bad); });
        rejects([&]{ auto bad=request; bad.checkpoint->scenario_identity="wrong"; validate_experiment_request(bad); });
        ExperimentExecution service; Gate gate;
        const auto expected=execute_experiment(request);
        auto id=service.start(request,gate.observer("Evaluating candidate")); gate.wait();
        w.edit(Field::Forklifts,"8"); w.remove_candidate(w.candidates()[0].id);
        request.candidates[0].definition.name="Mutated caller copy"; request.checkpoint->shipments.clear();
        gate.release(); check(finish(service).state==ExecutionState::Completed); auto actual=service.take_result(id);
        check(actual && actual->executions==expected.executions && actual->candidate_ids==expected.candidate_ids);
        return;
    }
    if (test=="retention") {
        w.start_experiment(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted); finish(w.execution()); if(!w.poll_experiment()) throw std::runtime_error(w.message()); const auto json=w.json();
        w.update_candidate(w.candidates()[0].id); check(w.json()==json);
        Gate gate; auto id=w.execution().start(w.capture_request(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted),gate.observer("Publishing"));
        gate.wait(); w.execution().cancel(); gate.release(); check(finish(w.execution()).state==ExecutionState::Cancelled);
        check(!w.poll_experiment() && w.json()==json && !w.execution().take_result(id));
        Gate failure; w.execution().start(w.capture_request(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted),failure.observer("Evaluating candidate",true));
        failure.wait(); failure.release(); check(finish(w.execution()).state==ExecutionState::Failed); check(!w.poll_experiment() && w.json()==json);
        w.start_experiment(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted); finish(w.execution()); if(!w.poll_experiment()) throw std::runtime_error(w.message());
        w.view_result("Candidate"); check(w.view().observation().metrics==w.report()->candidates.front().metrics);
        const auto preview=w.view().runtime().capture_snapshot();
        const auto camera=w.view().orbit_camera().camera().view_projection();
        w.start_experiment(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted); finish(w.execution()); if(!w.poll_experiment()) throw std::runtime_error(w.message());
        check(w.view().result_preview() && w.view().runtime().capture_snapshot()==preview && w.view().orbit_camera().camera().view_projection()==camera);
        w.start_experiment(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted); finish(w.execution());
        auto replacement=definition(); replacement.name="Replacement baseline"; w.load(replacement,true);
        check(!w.poll_experiment() && !w.report());
        return;
    }
    ExperimentExecution service; auto request=w.capture_request(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted);
    if (test=="sequential") {
        std::uint64_t previous=0; for (int i=0;i<30;++i) { auto id=service.start(request); check(id>previous); previous=id; check(finish(service).state==ExecutionState::Completed); check(!service.take_result(id-1)); check(service.take_result(id).has_value()); }
        service.cancel(); check(service.progress().state==ExecutionState::Completed); service.shutdown(); rejects([&]{service.start(request);}); return;
    }
    if (test=="shutdown") {
        std::atomic<bool> reached=false;
        auto owned=std::make_unique<ExperimentExecution>(); owned->start(request,[&](auto,auto,auto,auto){reached=true;});
        const auto deadline=Clock::now()+std::chrono::seconds{5}; while(!reached) {check(Clock::now()<deadline); std::this_thread::yield();}
        owned.reset(); return;
    }
    const std::string phase=test=="immediate" ? "Preparing" : test=="probes" ? "Probe simulation" : test=="publication" ? "Publishing" : test=="failure" ? "Evaluating candidate" : "Simulating";
    Gate gate; auto id=service.start(request,gate.observer(phase,test=="failure")); gate.wait();
    check(service.progress().active()); rejects([&]{service.start(request);});
    if(test=="publication") check(service.progress().completed_candidates==service.progress().total_candidates && service.progress().state!=ExecutionState::Completed);
    if (test!="failure") { service.cancel(); service.cancel(); check(service.progress().state==ExecutionState::Cancelling); }
    const auto started=Clock::now(); gate.release(); auto terminal=finish(service);
    if (test=="failure") { check(terminal.state==ExecutionState::Failed && terminal.error=="Injected candidate failure"); }
    else { check(terminal.state==ExecutionState::Cancelled && !service.take_result(id)); }
    if (test=="long_cancel") std::cout<<"Long-workload cancellation after gate release (ms): "<<std::chrono::duration<double,std::milli>{Clock::now()-started}.count()<<'\n';
    auto retry=service.start(request); check(retry>id); check(finish(service).state==ExecutionState::Completed); check(service.take_result(retry).has_value());
    const auto attempt=service.last_unsuccessful_attempt(); check(attempt && attempt->request_id==id && attempt->state==terminal.state);
}
}
int main(int argc,char** argv) { try { check(argc==2); run(argv[1]); return 0; } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
