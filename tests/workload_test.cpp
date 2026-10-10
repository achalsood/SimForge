#include <simforge/workspace/project.hpp>
#include <simforge/workspace/experiment_execution.hpp>
#include <simforge/warehouse/warehouse_input.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <memory>
#include <source_location>

using namespace simforge;
using Json=nlohmann::json;
namespace {
void check(bool ok,std::source_location p=std::source_location::current())
{if(!ok) throw std::runtime_error("Workload check failed at line "+std::to_string(p.line()));}
template<class F> void rejects(F f) {bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed);}
scenario::ScenarioDefinition source(int mode=0)
{
    scenario::ScenarioDefinition d{1,"Workload test",{10,600,42},scenario::WarehouseConfig{3,10,2,5,20}};
    auto& c=std::get<scenario::WarehouseConfig>(d.domain);
    if(mode==1 || mode==4) {c.shipment_interval_seconds=0;c.workload.arrivals=scenario::ArrivalMode::Profile;c.workload.segments={{0,200,10},{200,400,2},{400,600,8}};}
    if(mode==2 || mode==5) {c.shipment_interval_seconds=0;c.workload.arrivals=scenario::ArrivalMode::Uniform;c.workload.arrival_min_seconds=2;c.workload.arrival_max_seconds=8;}
    if(mode>=3) {c.processing_duration_seconds=0;c.workload.processing=scenario::ProcessingMode::Uniform;c.workload.processing_min_seconds=2;c.workload.processing_max_seconds=40;}
    scenario::validate(d);return d;
}
bool paired(const warehouse::WarehouseSnapshot& a,const warehouse::WarehouseSnapshot& b)
{
    if(a.shipments.size()!=b.shipments.size()) return false;
    for(std::size_t i=0;i<a.shipments.size();++i) if(a.shipments[i].arrival_tick!=b.shipments[i].arrival_tick ||
        a.shipments[i].required_processing_ticks!=b.shipments[i].required_processing_ticks) return false;
    return true;
}
auto future(warehouse::WarehouseSimulation& r)
{
    std::vector<std::vector<warehouse::ShipmentEvent>> trace;
    for(;;) {
        const auto state=r.capture_snapshot();trace.push_back(state.events);
        if(state.events.empty() || state.events.front().tick>r.horizon_ticks()) break;
        r.advance_to(state.events.front().tick);
    }
    r.run();return trace;
}
struct Temp {
    const std::filesystem::path root=std::filesystem::temp_directory_path()/("simforge-workload-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp(){check(std::filesystem::create_directory(root));}
    ~Temp(){std::error_code ignored;std::filesystem::remove_all(root,ignored);}
};
void wait(workspace::ExperimentExecution& service)
{
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{30};
    while(service.progress().active()) {check(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();}
}
void run(std::string_view name)
{
    if(name=="legacy") {
        auto d=scenario::load_scenario(std::filesystem::path{SIMFORGE_SOURCE_DIR}/"examples/warehouse-baseline.json");
        for(unsigned forklifts:{2u,3u}) {
            std::get<scenario::WarehouseConfig>(d.domain).forklifts=forklifts;
            warehouse::WarehouseSimulation r{d};r.run();const auto m=r.metrics();
            check(m.arrived==96 && m.started==(forklifts==2?64:96) && m.completed==(forklifts==2?63:94) && m.waiting==(forklifts==2?32:0) && m.in_progress==(forklifts==2?1:2));
            const auto horizon=r.horizon_ticks();
            for(const auto& shipment:r.shipments()) {
                const auto arrival=std::uint64_t{shipment.id}*300*60;
                const auto start=forklifts==2 ? std::uint64_t{shipment.id/2}*900*60+(shipment.id%2)*300*60 : arrival;
                check(shipment.arrival_tick==arrival && shipment.required_processing_ticks==0);
                if(start>=horizon) check(!shipment.start_tick && shipment.status==warehouse::ShipmentStatus::Waiting);
                else {check(shipment.start_tick==start);const auto completion=start+900*60;
                    check(shipment.completion_tick==(completion<=horizon ? std::optional<std::uint64_t>{completion} : std::nullopt));}
            }
            auto state=r.capture_snapshot();check(state.version==1 && state.workload_generator_version==0);
            const auto bytes=warehouse::serialize_snapshot(state);check(warehouse::serialize_snapshot(warehouse::deserialize_snapshot(bytes))==bytes);
            auto json=Json::parse(scenario::serialize_scenario(d));auto& w=json["warehouse"];
            w["workload"]={{"generator_version",1},{"arrivals",{{"mode","constant"},{"interval_seconds",300}}},{"processing",{{"mode","constant"},{"duration_seconds",900}}}};
            w.erase("shipment_interval_seconds");w.erase("processing_duration_seconds");
            warehouse::WarehouseSimulation mapped{scenario::parse_scenario(json.dump())};mapped.run();check(mapped.capture_snapshot()==state);
        }return;
    }
    if(name=="profile" || name=="gaps") {
        auto d=source(1);d.simulation={1,15,42};auto& c=std::get<scenario::WarehouseConfig>(d.domain);
        c.workload.segments={{0,5,2},{5,10,3},{12,15,10}};
        warehouse::WarehouseSimulation r{d};r.run();std::vector<std::uint64_t> ticks;for(const auto& s:r.shipments()) ticks.push_back(s.arrival_tick);
        check(ticks==std::vector<std::uint64_t>{0,2,4,5,8,12});warehouse::validate_snapshot(r.capture_snapshot());
        if(name=="gaps") {
            c.workload.segments={{3,4,604800},{14,15,1}};warehouse::WarehouseSimulation gaps{d};gaps.advance_to(2);check(gaps.metrics().arrived==0);warehouse::validate_snapshot(gaps.capture_snapshot());
            gaps.run();check(gaps.metrics().arrived==2 && gaps.shipments()[0].arrival_tick==3 && gaps.shipments()[1].arrival_tick==14);
            c.workload.segments.clear();warehouse::WarehouseSimulation idle{d};warehouse::validate_snapshot(idle.capture_snapshot());idle.run();check(idle.metrics().arrived==0 && idle.pending_events()==0);warehouse::validate_snapshot(idle.capture_snapshot());
        }return;
    }
    if(name=="vectors") {
        std::uint64_t state=0;for(const auto expected:{0xe220a8397b1dcdafULL,0x6e789e6aa1b965f4ULL,0x06c45d188009454fULL,0xf88bb8a8724c81ecULL,0x1b39896a51a8749bULL}) check(scenario::workload::next_random(state)==expected);
        const std::uint64_t arrivals[]={161,163,152,125,196,106,117,159},processing[]={102,108,195,132,103,166,174,136};
        for(std::size_t i=0;i<8;++i) {check(scenario::workload::uniform_ticks(42,scenario::workload::arrival_domain,i,100,200)==arrivals[i]);check(scenario::workload::uniform_ticks(42,scenario::workload::processing_domain,i,100,200)==processing[i]);}
        for(std::uint64_t i=0;i<100;++i) {check(scenario::workload::uniform_ticks(~i,scenario::workload::arrival_domain,i,1,1)==1);check(scenario::workload::uniform_ticks(i,scenario::workload::processing_domain,i,604800000,604800000)==604800000);}
        rejects([]{(void)scenario::workload::uniform_ticks(0,0,0,0,1);});return;
    }
    if(name=="uniform" || name=="processing" || name=="streams") {
        auto d=source(name=="processing"?3:5);warehouse::WarehouseSimulation a{d},b{d};a.run();b.run();check(a.capture_snapshot()==b.capture_snapshot());
        auto changed=d;++changed.simulation.random_seed;warehouse::WarehouseSimulation other{changed};other.run();check(!paired(a.capture_snapshot(),other.capture_snapshot()));
        if(name=="streams") {auto independent=d;auto& c=std::get<scenario::WarehouseConfig>(independent.domain);c.workload.processing=scenario::ProcessingMode::Constant;c.workload.processing_min_seconds=c.workload.processing_max_seconds=0;c.processing_duration_seconds=20;
            warehouse::WarehouseSimulation fixed{independent};fixed.run();check(fixed.shipments().size()==a.shipments().size());for(std::size_t i=0;i<a.shipments().size();++i) check(fixed.shipments()[i].arrival_tick==a.shipments()[i].arrival_tick);
            c.workload.arrivals=scenario::ArrivalMode::Constant;c.workload.arrival_min_seconds=c.workload.arrival_max_seconds=0;c.shipment_interval_seconds=5;
            c.workload.processing=std::get<scenario::WarehouseConfig>(d.domain).workload.processing;c.workload.processing_min_seconds=2;c.workload.processing_max_seconds=40;c.processing_duration_seconds=0;
            warehouse::WarehouseSimulation different_arrivals{independent};different_arrivals.run();for(std::size_t i=0;i<std::min(a.shipments().size(),different_arrivals.shipments().size());++i) check(a.shipments()[i].required_processing_ticks==different_arrivals.shipments()[i].required_processing_ticks);
        }return;
    }
    if(name=="validation") {
        auto d=source(5);const auto fail=[&](auto mutate){auto bad=d;mutate(std::get<scenario::WarehouseConfig>(bad.domain));rejects([&]{scenario::validate(bad);});};
        fail([](auto& c){c.workload.arrival_min_seconds=0;});fail([](auto& c){c.workload.arrival_min_seconds=9;});fail([](auto& c){c.workload.processing_max_seconds=1;});
        fail([](auto& c){c.shipment_interval_seconds=10;});fail([](auto& c){c.workload.arrivals=static_cast<scenario::ArrivalMode>(99);});
        auto profile=source(1);for(const auto segments:{std::vector<scenario::DemandSegment>{{0,200,10},{199,600,2}},{{200,300,10},{0,100,5}},{{0,601,2}},{{0,0,2}},{{0,600,0}}}) {auto bad=profile;std::get<scenario::WarehouseConfig>(bad.domain).workload.segments=segments;rejects([&]{scenario::validate(bad);});}
        std::get<scenario::WarehouseConfig>(profile.domain).workload.segments.resize(65);rejects([&]{scenario::validate(profile);});
        auto excessive=source(2);excessive.simulation={1,100001,42};std::get<scenario::WarehouseConfig>(excessive.domain).workload.arrival_min_seconds=1;rejects([&]{scenario::validate(excessive);});
        auto json=Json::parse(scenario::serialize_scenario(d));for(const auto value:{Json(0),Json(-1),Json(1.5),Json("4"),Json(999999999)}) {auto bad=json;bad["warehouse"]["workload"]["arrivals"]["minimum_seconds"]=value;rejects([&]{(void)scenario::parse_scenario(bad.dump());});}
        json["warehouse"]["workload"]["arrivals"]["mode"]="exponential";rejects([&]{(void)scenario::parse_scenario(json.dump());});return;
    }
    if(name=="snapshots") {
        for(int mode=0;mode<=5;++mode) {
            const auto d=source(mode);warehouse::WarehouseSimulation original{d};original.advance_to(1500);
            const auto state=original.capture_snapshot();const auto bytes=warehouse::serialize_snapshot(state);auto restored=warehouse::WarehouseSimulation::create_from(warehouse::deserialize_snapshot(bytes));
            check(future(original)==future(restored));check(original.capture_snapshot()==restored.capture_snapshot());
            if(mode) {auto bad=state;bad.workload_generator_version=0;rejects([&]{warehouse::validate_snapshot(bad);});bad=state;bad.shipments.front().required_processing_ticks=0;rejects([&]{warehouse::validate_snapshot(bad);});bad=state;++bad.shipments.front().arrival_tick;rejects([&]{warehouse::validate_snapshot(bad);});}
        }return;
    }
    if(name=="recording") {
        auto d=source(5);warehouse::WarehouseSimulation r{d};r.advance_to(100);warehouse::WarehouseInputRecorder recorder{r};
        recorder.set_forklift_capacity(3);recorder.advance_to(1200);recorder.set_resource_capacity(warehouse::ResourceType::Worker,12);recorder.advance_to(r.horizon_ticks());
        const auto record=recorder.capture_recording();check(record.schema_version==3);const auto decoded=warehouse::parse_recording(warehouse::serialize_recording(record));
        auto destination=warehouse::WarehouseSimulation::create_from(record.initial_snapshot);warehouse::replay_recording(decoded,destination);check(destination.capture_snapshot()==r.capture_snapshot());
        auto bad=record;bad.schema_version=2;rejects([&]{warehouse::validate_recording(bad);});return;
    }
    if(name=="comparisons") {
        auto request=analysis::prepare_request(source(5),1500,analysis::Objective::MaximizeCompleted,{{"Extra forklift",{3,10,3}},{"Extra bay",{4,10,2}}});
        std::vector<warehouse::WarehouseSnapshot> finals;auto report=analysis::run_comparison(request,nullptr,&finals);
        for(const auto& s:finals) check(paired(finals.front(),s));
        for(const auto& probe:report.bottlenecks) if(probe.result) {warehouse::WarehouseSnapshot state;const auto m=analysis::execute_candidate(request.checkpoint,probe.result->candidate.resources,nullptr,&state);check(m==probe.result->metrics && paired(finals.front(),state));}
        check(analysis::replay_report(analysis::serialize_report(report))==report);check(!analysis::serialize_csv(report).empty());
        std::reverse(request.candidates.begin(),request.candidates.end());check(analysis::run_comparison(request)==report);return;
    }
    if(name=="async") {
        workspace::ScenarioWorkspace w{source(5)};w.edit(workspace::Field::Name,"Alternative");w.edit(workspace::Field::Forklifts,"3");w.add_candidate();
        const auto viewer=w.view().runtime().capture_snapshot();w.compare(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted);const auto expected=w.report()->executions;
        w.start_experiment(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted);wait(w.execution());check(w.poll_experiment());check(w.report()->executions==expected && w.view().runtime().capture_snapshot()==viewer);
        w.compare(workspace::ComparisonMode::Checkpoint,analysis::Objective::MinimizeWaiting,1500);const auto checkpoint=w.report()->executions;
        w.start_experiment(workspace::ComparisonMode::Checkpoint,analysis::Objective::MinimizeWaiting,1500);wait(w.execution());check(w.poll_experiment() && w.report()->executions==checkpoint);
        auto changed=w.draft().workload;changed.bounds[0]="3";w.edit_workload(changed);w.update_candidate(w.candidates().front().id);rejects([&]{(void)w.capture_request(workspace::ComparisonMode::Checkpoint,analysis::Objective::MaximizeCompleted);});return;
    }
    if(name=="cancellation") {
        workspace::ScenarioWorkspace w{source(5)};w.edit(workspace::Field::Name,"Alternative");w.add_candidate();
        w.compare(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted);const auto previous=w.json();
        auto request=w.capture_request(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted);
        request.baseline.simulation.duration_seconds=100000;
        auto& config=std::get<scenario::WarehouseConfig>(request.baseline.domain);config.workload.arrival_min_seconds=1;config.workload.arrival_max_seconds=2;
        for(auto& c:request.candidates) {c.definition=request.baseline;c.definition.name="Long candidate";}
        struct Gate {std::mutex mutex;std::condition_variable cv;bool candidate=false,reached=false,released=false;};
        auto gate=std::make_shared<Gate>();
        struct Release {std::shared_ptr<Gate> gate;~Release(){std::lock_guard lock{gate->mutex};gate->released=true;gate->cv.notify_all();}} release{gate};
        w.execution().start(std::move(request),[gate](auto phase,auto,auto tick,auto) {
            if(phase=="Evaluating candidate") gate->candidate=true;
            if(gate->candidate && phase=="Simulating" && tick>0) {
                std::unique_lock lock{gate->mutex};if(gate->reached) return;gate->reached=true;gate->cv.notify_all();
                if(!gate->cv.wait_for(lock,std::chrono::seconds{20},[&]{return gate->released;})) throw std::runtime_error("Cancellation gate timed out");
            }
        });
        {std::unique_lock lock{gate->mutex};check(gate->cv.wait_for(lock,std::chrono::seconds{20},[&]{return gate->reached;}));}
        w.execution().cancel();w.execution().cancel();const auto start=std::chrono::steady_clock::now();
        {std::lock_guard lock{gate->mutex};gate->released=true;gate->cv.notify_all();}wait(w.execution());
        check(w.execution().progress().state==workspace::ExecutionState::Cancelled && !w.poll_experiment() && w.json()==previous);
        std::cout<<"100K worst-case workload cancellation after gate release ms: "<<std::chrono::duration<double,std::milli>{std::chrono::steady_clock::now()-start}.count()<<'\n';return;
    }
    if(name=="observation") {
        const auto d=source(3);warehouse::WarehouseSimulation r{d};bool out_of_order=false;
        for(std::uint64_t tick=0;tick<=r.horizon_ticks();tick+=10) {
            r.advance_to(tick);const auto state=r.capture_snapshot();const auto o=r.capture_observation(128,100000);check(o.processing.size()==state.reservations.size());
            for(std::size_t i=0;i<o.processing.size();++i) check(o.processing[i].id==state.reservations[i]);
            bool active=false;for(const auto& s:r.shipments()) {active|=s.status==warehouse::ShipmentStatus::InProgress;if(active && s.status==warehouse::ShipmentStatus::Completed) out_of_order=true;}
            if(tick%100==0) warehouse::validate_snapshot(state);
        }check(out_of_order);return;
    }
    if(name=="persistence" || name=="editor") {
        workspace::ScenarioWorkspace w{source(5)};workspace::Project project{w};Temp temp;
        w.view().seek(1500);auto custom=source(4);custom.name="Profile custom";const auto id=w.create_run(custom);w.select_run(id);w.view().seek(1800);w.select_run(0);
        auto draft=w.draft().workload;draft.bounds[0]="invalid";w.edit_workload(draft);check(!w.validate_draft());
        project.save(temp.root/"workload.simforge",true);workspace::ScenarioWorkspace reopened;workspace::Project loaded{reopened};loaded.open(temp.root/"workload.simforge");
        check(reopened.draft()==w.draft() && reopened.runs().size()==1 && reopened.view().runtime().capture_snapshot()==w.view().runtime().capture_snapshot());
        w.view().toggle_playback();reopened.view().toggle_playback();w.view().advance(std::chrono::seconds{2});reopened.view().advance(std::chrono::seconds{2});check(reopened.view().runtime().capture_snapshot()==w.view().runtime().capture_snapshot());
        std::ifstream input(temp.root/"workload.simforge");Json damaged;input>>damaged;
        damaged["payload"]["baseline"]["definition"]["warehouse"]["workload"]["arrivals"]["minimum_seconds"]=0;
        const auto bytes=damaged["payload"].dump();std::uint32_t crc=0xffffffff;
        for(unsigned char byte:bytes) {crc^=byte;for(int bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xedb88320:0);}
        damaged["crc32"]=~crc;damaged["payload_size"]=bytes.size();
        {std::ofstream file(temp.root/"invalid.simforge");file<<damaged.dump();}
        const auto intact=reopened.view().runtime().capture_snapshot();rejects([&]{loaded.open(temp.root/"invalid.simforge");});check(reopened.view().runtime().capture_snapshot()==intact);
        if(name=="editor") {draft.bounds[0]="2";w.edit_workload(draft);check(w.validate_draft());w.run_draft();check(w.view().definition().simulation.random_seed==42 && scenario::workload::stochastic(std::get<scenario::WarehouseConfig>(w.view().definition().domain)));}
        return;
    }
    if(name=="language") {
        auto d=source(5);namespace nl=scenario::language;
        auto p=nl::interpret("I want to see ten more bays",nl::Mode::Modify,d);check(p.valid());check(std::get<scenario::WarehouseConfig>(p.proposed().domain).workload==std::get<scenario::WarehouseConfig>(d.domain).workload);
        workspace::ScenarioWorkspace w{d};const auto original=w.view().runtime().capture_snapshot();
        p=nl::interpret("I want to see ten more bays",nl::Mode::Modify,w.draft_definition(),w.language_stamp());
        rejects([&]{w.apply_language_proposal(p,false);});w.apply_language_proposal(p,true);
        check(std::get<scenario::WarehouseConfig>(w.draft_definition().domain).workload==std::get<scenario::WarehouseConfig>(d.domain).workload && w.view().runtime().capture_snapshot()==original);
        check(!nl::interpret("Shipments arrive every 5 minutes",nl::Mode::Modify,d).valid());check(!nl::interpret("Set processing to 20 minutes",nl::Mode::Modify,d).valid());return;
    }
    if(name=="statistical") {
        for(const auto domain:{scenario::workload::arrival_domain,scenario::workload::processing_domain}) {
        constexpr std::size_t n=100000;double sum=0,squares=0;std::array<unsigned,10> bins{};
        for(std::size_t i=0;i<n;++i) {const auto x=scenario::workload::uniform_ticks(12345,domain,i,100,1099);check(x>=100 && x<=1099);sum+=static_cast<double>(x);squares+=static_cast<double>(x*x);++bins[(x-100)/100];}
        const auto mean=sum/n,variance=squares/n-mean*mean;check(std::abs(mean-599.5)<3 && std::abs(variance-(1000.0*1000-1)/12)<1600);
        for(auto b:bins) check(b>9000 && b<11000);
        std::cout<<"100K fixed-seed uniform samples: domain="<<domain<<", mean="<<mean<<", variance="<<variance<<'\n';
        }return;
    }
    throw std::runtime_error("Unknown workload test");
}
}
int main(int argc,char** argv) {try{check(argc==2);run(argv[1]);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
