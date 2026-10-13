#include <simforge/workspace/project.hpp>
#include <simforge/workspace/experiment_execution.hpp>
#include <fstream>
#include <iostream>
#include <iomanip>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#endif
using namespace simforge;
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
std::size_t peak(){
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS c{};if(GetProcessMemoryInfo(GetCurrentProcess(),&c,sizeof(c)))return c.PeakWorkingSetSize;
#endif
    return 0;
}
scenario::ScenarioDefinition baseline() {
    scenario::ScenarioDefinition d{1,"History benchmark",{10,120,42},scenario::WarehouseConfig{12,2,1,0,0}};
    auto& w=std::get<scenario::WarehouseConfig>(d.domain).workload;w.arrivals=scenario::ArrivalMode::Uniform;w.arrival_min_seconds=1;w.arrival_max_seconds=3;
    w.processing=scenario::ProcessingMode::Uniform;w.processing_min_seconds=2;w.processing_max_seconds=12;return d;
}
void wait(workspace::ScenarioWorkspace& w){while(w.execution().progress().active())std::this_thread::yield();if(!w.poll_experiment())throw std::runtime_error(w.message());}
workspace::ScenarioWorkspace source(unsigned seeds=20,bool curated=false) {
    auto definition=baseline();if(curated)definition.name="Variable demand warehouse";workspace::ScenarioWorkspace w(definition);w.edit(workspace::Field::Name,"Two forklifts");w.edit(workspace::Field::Forklifts,"2");w.add_candidate();
    analysis::ReplicationPlan p;p.count=seeds;p.base_seed=1001;w.start_statistical_experiment(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted,p);wait(w);return w;
}
void write_examples(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);auto w=source(20,true);analysis::ReplicationPlan plan;plan.count=30;plan.base_seed=50001;
    auto save=[&](const char* file,std::uint64_t selection){std::ofstream out(dir/file);out<<analysis::serialize_confirmation_request(w.freeze_confirmation(selection,plan))<<'\n';};
    save("warehouse-confirmation-forklift.json",1);
    analysis::SensitivityRequest s;s.source=w.baseline();s.values={1,2,3,4};s.multi_seed=true;s.plan.count=20;s.plan.base_seed=1001;w.start_sensitivity(s);wait(w);save("warehouse-confirmation-sensitivity.json",w.report()->sensitivity->points[1].id);
    analysis::InteractionRequest g;g.source=w.baseline();g.x_values={1,2,3};g.y_values={1,2,3};g.multi_seed=true;g.plan=s.plan;w.start_interaction(g);wait(w);save("warehouse-confirmation-interaction.json",w.report()->interaction->cells[4].id);
    // Same tested capacity under varying stochastic demand: actual zero paired effects, honest zero-width interval.
    w.edit(workspace::Field::Name,"Unchanged capacities");w.edit(workspace::Field::Forklifts,"1");w.add_candidate();w.start_statistical_experiment(workspace::ComparisonMode::Initial,analysis::Objective::MaximizeCompleted,s.plan);wait(w);save("warehouse-confirmation-inconclusive.json",w.candidates().back().id);
}
void history(const char* name,unsigned count,bool large=false,bool near_limit=false) {
    auto w=source();auto report=w.report();
    if(large) {analysis::InteractionRequest g;g.source=baseline();for(unsigned i=1;i<=10;++i){g.x_values.push_back(i);g.y_values.push_back(i);}g.multi_seed=true;g.plan.count=100;g.plan.base_seed=1001;w.start_interaction(g);wait(w);report=w.report();w.delete_study(w.studies().front().id,true);}
    double insert=0;bool limit=false;
    while(w.studies().size()<count) {auto begin=Clock::now();try{w.retain_study(*report);}catch(const std::length_error&){limit=true;break;}insert+=ms(begin);}
    if(near_limit && !limit)throw std::runtime_error("Near-limit benchmark did not exercise byte retention limit");
    auto begin=Clock::now();for(unsigned i=0;i<10000;++i)w.select_study(w.studies()[i%w.studies().size()].id);const auto switching=ms(begin)/10000;
    workspace::Project p(w);const auto path=std::filesystem::temp_directory_path()/("simforge-history-benchmark-"+std::to_string(Clock::now().time_since_epoch().count())+".simforge");
    begin=Clock::now();p.save(path);const auto saving=ms(begin);const auto bytes=p.timings().bytes;workspace::ScenarioWorkspace loaded;workspace::Project q(loaded);begin=Clock::now();q.open(path);const auto loading=ms(begin);std::filesystem::remove(path);
    auto recovery=path;recovery.replace_extension(".recovery");w.rename_study(w.selected_study_id(),"Unsaved benchmark metadata","Recovery benchmark");begin=Clock::now();p.recover_if_due(recovery,Clock::now()+std::chrono::seconds(61));const auto recovering=ms(begin);const auto recovery_bytes=std::filesystem::file_size(recovery);p.discard_own_recovery(recovery);if(std::filesystem::exists(recovery))throw std::runtime_error("Own recovery not discarded");
    const auto studies=w.studies().size(),owned=w.history_bytes();begin=Clock::now();const auto deleting=w.studies().back().id;report.reset();w.delete_study(deleting,true);const auto deletion=ms(begin);
    std::cout<<name<<','<<studies<<','<<owned<<','<<bytes<<','<<insert<<','<<switching<<','<<saving<<','<<loading<<','<<recovering<<','<<recovery_bytes<<','<<deletion<<','<<peak()<<'\n';
}
void confirmation(unsigned seeds) {
    auto w=source();analysis::ReplicationPlan p;p.count=seeds;p.base_seed=50001;const auto c=w.freeze_confirmation(1,p);auto begin=Clock::now();w.start_confirmation(c);const auto submit=ms(begin);wait(w);const auto elapsed=ms(begin);
    std::cout<<"confirmation_"<<seeds<<",2,"<<w.history_bytes()<<",0,"<<submit<<",0,"<<elapsed<<",0,0,0,0,"<<peak()<<'\n';
    // Cancel through the actual workspace lifecycle; wait for a measured seed to begin.
    w.select_study(w.studies().front().id);p.count=100;p.base_seed=60001;const auto cancel=w.freeze_confirmation(1,p);w.start_confirmation(cancel);while(w.execution().progress().active() && !w.execution().progress().horizon)std::this_thread::yield();begin=Clock::now();w.execution().cancel();while(w.execution().progress().active())std::this_thread::yield();w.poll_experiment();
    if(w.studies().size()!=2)throw std::runtime_error("Cancellation published partial confirmation");std::cout<<"cancel_"<<seeds<<",2,"<<w.history_bytes()<<",0,"<<ms(begin)<<",0,0,0,0,0,0,"<<peak()<<'\n';
}
int main(int argc,char** argv)try {
    if(argc==3 && std::string_view(argv[1])=="--examples"){write_examples(argv[2]);return 0;}
    std::cout<<"case,studies,history_serialized_bytes,project_bytes,insertion_or_submit_ms,switch_ms,save_or_execution_ms,load_ms,recovery_ms,recovery_bytes,deletion_ms,peak_working_set_bytes\n"<<std::fixed<<std::setprecision(4);
    if(argc==2 && std::string_view(argv[1])=="--near"){history("near_storage_limit",32,true,true);return 0;}
    history("one",1);history("ten_moderate",10);history("two_large_grids",2,true);confirmation(20);confirmation(100);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
