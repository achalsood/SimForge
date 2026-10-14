// Reuse the established deterministic Commit #28 fixture generator so comparisons
// exercise the same authoritative grids, retention limits and seeded workload.
#define main history_benchmark_main
#include "study_history_benchmark.cpp"
#undef main
#include <atomic>
#include <mutex>
#include <map>
#include <condition_variable>
std::size_t working_set(){
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS c{};if(GetProcessMemoryInfo(GetCurrentProcess(),&c,sizeof(c)))return c.WorkingSetSize;
#endif
    return 0;
}
workspace::ProjectCompletion finish_project(workspace::Project& p){while(true){if(auto completed=p.poll())return *completed;std::this_thread::yield();}}
void verify(bool equal){if(!equal)throw std::runtime_error("Authoritative benchmark round trip changed evidence");}
void verify_evidence(const workspace::Comparison& a,const workspace::Comparison& b) {
    verify(a.request_id==b.request_id && a.candidate_ids==b.candidate_ids && a.changed_inputs==b.changed_inputs && a.limitations==b.limitations);
    if(a.statistics){verify(bool(b.statistics));const auto& x=*a.statistics;const auto& y=*b.statistics;verify(analysis::serialize_statistical_request(x.request)==analysis::serialize_statistical_request(y.request) && x.replications==y.replications && x.candidates==y.candidates && x.probes==y.probes && x.limitations==y.limitations && x.simulation_executions==y.simulation_executions);}
    if(a.confirmation)verify(b.confirmation && analysis::serialize_confirmation_request(*a.confirmation)==analysis::serialize_confirmation_request(*b.confirmation));
    if(a.interaction){verify(bool(b.interaction));const auto& x=*a.interaction;const auto& y=*b.interaction;verify(analysis::serialize_interaction_request(x.request)==analysis::serialize_interaction_request(y.request) && x.baseline_means==y.baseline_means && x.best_cells==y.best_cells && x.simulation_executions==y.simulation_executions && x.replications.size()==y.replications.size() && x.cells.size()==y.cells.size() && x.interactions.size()==y.interactions.size());
        for(std::size_t i=0;i<x.replications.size();++i){const auto& u=x.replications[i];const auto& v=y.replications[i];verify(u.seed==v.seed && u.baseline==v.baseline && u.cells==v.cells);}
        for(std::size_t i=0;i<x.cells.size();++i){const auto& u=x.cells[i];const auto& v=y.cells[i];verify(u.id==v.id && u.x_index==v.x_index && u.y_index==v.y_index && u.x==v.x && u.y==v.y && u.baseline==v.baseline && u.means==v.means && u.effects==v.effects);}
        for(std::size_t i=0;i<x.interactions.size();++i){const auto& u=x.interactions[i];const auto& v=y.interactions[i];verify(u.x_index==v.x_index && u.y_index==v.y_index && u.x0==v.x0 && u.x1==v.x1 && u.y0==v.y0 && u.y1==v.y1 && u.cells==v.cells && u.effects==v.effects && u.per_seed==v.per_seed && u.interpretation==v.interpretation);}
    }
    if(a.sensitivity){verify(bool(b.sensitivity));const auto& x=*a.sensitivity;const auto& y=*b.sensitivity;verify(analysis::serialize_sensitivity_request(x.request)==analysis::serialize_sensitivity_request(y.request) && x.probes==y.probes && x.baseline_means==y.baseline_means && x.best_point==y.best_point && x.simulation_executions==y.simulation_executions && x.replications.size()==y.replications.size() && x.points.size()==y.points.size());
        for(std::size_t i=0;i<x.replications.size();++i){const auto& u=x.replications[i];const auto& v=y.replications[i];verify(u.seed==v.seed && u.baseline==v.baseline && u.points==v.points && u.probes==v.probes);}
        for(std::size_t i=0;i<x.points.size();++i){const auto& u=x.points[i];const auto& v=y.points[i];verify(u.id==v.id && u.value==v.value && u.baseline==v.baseline && u.against_baseline==v.against_baseline && u.adjacent==v.adjacent && u.means==v.means && u.mean_lower==v.mean_lower && u.mean_upper==v.mean_upper && u.marginal_per_unit==v.marginal_per_unit && u.marginal_interpretation==v.marginal_interpretation);}
    }
}
int main(int argc,char** argv)try {
    const std::string mode=argc>1?argv[1]:"small";const bool budget_case=mode=="near" || mode=="maximum";
    const auto retained=budget_case?20U:mode=="large"?4U:mode=="medium"?10U:mode=="count_limit"?32U:0U;
    std::cout<<"idle_working_set_bytes,"<<working_set()<<'\n';auto w=retained?source():workspace::ScenarioWorkspace(baseline());auto evidence=w.report();auto statistical=evidence;
    if(mode=="medium"){analysis::SensitivityRequest s;s.source=baseline();s.values={1,2,3};s.multi_seed=true;s.plan.count=20;s.plan.base_seed=1001;w.start_sensitivity(s);wait(w);evidence=w.report();}
    if(mode=="large"){analysis::ReplicationPlan p;p.count=20;p.base_seed=9001;w.start_confirmation(w.freeze_confirmation(1,p));wait(w);}
    if(budget_case || mode=="large") {analysis::InteractionRequest g;g.source=baseline();for(unsigned i=1;i<=10;++i){g.x_values.push_back(i);g.y_values.push_back(i);}g.multi_seed=true;g.plan.count=100;g.plan.base_seed=1001;w.start_interaction(g);wait(w);evidence=w.report();if(budget_case)w.delete_study(w.studies().front().id,true);}
    while(w.studies().size()<retained)w.retain_study(mode=="medium" && w.studies().size()%2?*statistical:*evidence);
    if(budget_case){bool bounded=false;try{w.retain_study(*evidence);}catch(const std::length_error&){bounded=true;}if(!bounded)throw std::runtime_error("Near fixture failed to exercise retention limit");}
    std::cout<<"fixture,"<<mode<<",studies,"<<w.studies().size()<<",history_bytes,"<<w.history_bytes()<<",source_working_set,"<<working_set()<<",source_peak,"<<peak()<<'\n';
#ifdef _WIN32
    MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);GlobalMemoryStatusEx(&memory);std::cout<<"physical_ram_bytes,"<<memory.ullTotalPhys<<",logical_processors,"<<std::thread::hardware_concurrency()<<'\n';
#endif
    workspace::Project p(w);const auto path=std::filesystem::temp_directory_path()/("simforge-persistence-benchmark-"+std::to_string(Clock::now().time_since_epoch().count())+".simforge");
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code e;std::filesystem::remove(path,e);auto empty_path=path;empty_path+=".small.simforge";std::filesystem::remove(empty_path,e);path.replace_extension(".recovery");std::filesystem::remove(path,e);}}cleanup{path};
    std::mutex samples_mutex;std::map<std::string,std::size_t> memory_peaks;std::string phase="snapshot";
    auto sampler=std::jthread([&](std::stop_token stop){while(!stop.stop_requested()){const auto memory=working_set();{std::lock_guard lock(samples_mutex);memory_peaks[phase]=std::max(memory_peaks[phase],memory);}std::this_thread::sleep_for(std::chrono::milliseconds(2));}});
    const auto hook=[&](std::string_view value){std::lock_guard lock(samples_mutex);phase=value;memory_peaks[phase]=std::max(memory_peaks[phase],working_set());};
    auto begin=Clock::now();p.start_save(path,true,hook);const auto submission=ms(begin);auto result=finish_project(p);if(!result.success)throw std::runtime_error(result.error);const auto save=p.timings();
    if(mode=="maximum"){std::ofstream padding(path,std::ios::binary|std::ios::app);std::array<char,64*1024> spaces{};spaces.fill(' ');auto remaining=workspace::project_max_bytes-std::filesystem::file_size(path);while(remaining){const auto count=std::min(remaining,std::uintmax_t(spaces.size()));padding.write(spaces.data(),static_cast<std::streamsize>(count));remaining-=count;}}
    workspace::ScenarioWorkspace loaded;workspace::Project q(loaded);q.start_open(path,false,hook);result=finish_project(q);if(!result.success)throw std::runtime_error(result.error);const auto open=q.timings();
    if(loaded.studies().size()!=w.studies().size() || loaded.history_bytes()!=w.history_bytes())throw std::runtime_error("Benchmark round trip changed evidence");
    verify(loaded.view().runtime().capture_snapshot()==w.view().runtime().capture_snapshot() && loaded.selected_study_id()==w.selected_study_id() && p.preferences()==q.preferences());
    for(std::size_t i=0;i<w.studies().size();++i){const auto& a=w.studies()[i];const auto& b=loaded.studies()[i];verify(a.id==b.id && a.title==b.title && a.notes==b.notes && a.completed==b.completed && a.view==b.view && a.reserved_confirmation_seeds==b.reserved_confirmation_seeds);verify_evidence(*a.evidence,*b.evidence);}
    if(mode=="malformed"){std::ofstream out(path,std::ios::binary|std::ios::app);out<<" true";out.close();const auto snapshot=loaded.view().runtime().capture_snapshot();const auto preferences=q.preferences();const auto associated=q.info().path;begin=Clock::now();q.start_open(path,false,hook);if(finish_project(q).success)throw std::runtime_error("Trailing malformed project accepted");std::cout<<"malformed_rejection_ms,"<<ms(begin)<<'\n';verify(snapshot==loaded.view().runtime().capture_snapshot() && preferences==q.preferences() && associated==q.info().path && !q.dirty());}
    if(mode=="oversized"){std::ofstream out(path,std::ios::binary);out.seekp(workspace::project_max_bytes);out.put(' ');out.close();begin=Clock::now();q.start_open(path);if(finish_project(q).success)throw std::runtime_error("Oversized file accepted");std::cout<<"oversized_rejection_ms,"<<ms(begin)<<'\n';}
    const auto recovery=path.parent_path()/(path.stem().string()+".recovery");if(!w.studies().empty())w.rename_study(w.selected_study_id(),"Unsaved benchmark metadata","Recovery benchmark");else w.edit(workspace::Field::Workers,"9");begin=Clock::now();p.start_recovery_if_due(recovery,Clock::now()+std::chrono::seconds(61));result=finish_project(p);if(!result.success)throw std::runtime_error(result.error);const auto recovery_ms=ms(begin);
    const auto recovery_save=p.timings();q.start_open(recovery,true,hook);if(!finish_project(q).success || !q.dirty())throw std::runtime_error("Recovery round trip failed");const auto recovery_open=q.timings();
    std::cout<<"post_operations_working_set,"<<working_set()<<'\n';
    if(!w.studies().empty()) {
        std::mutex gate_mutex;std::condition_variable gate_cv;bool reached=false,released=false;
        q.start_open(recovery,true,[&](std::string_view phase){if(phase!="study_reconstruction")return;std::unique_lock lock(gate_mutex);if(reached)return;reached=true;gate_cv.notify_all();gate_cv.wait(lock,[&]{return released;});});
        {std::unique_lock lock(gate_mutex);if(!gate_cv.wait_for(lock,std::chrono::seconds(30),[&]{return reached;}))throw std::runtime_error("Benchmark cancellation gate timed out");}
        begin=Clock::now();q.cancel();{std::lock_guard lock(gate_mutex);released=true;gate_cv.notify_all();}if(!finish_project(q).cancelled)throw std::runtime_error("Cancellation unexpectedly published");std::cout<<"cancellation_ms,"<<ms(begin)<<",post_cancel_working_set,"<<working_set()<<'\n';
        const auto count=w.studies().size();begin=Clock::now();w.delete_study(w.studies().back().id,true);std::cout<<"deletion_ms,"<<ms(begin)<<",remaining_studies,"<<w.studies().size()<<",post_delete_working_set,"<<working_set()<<'\n';if(w.studies().size()+1!=count)throw std::runtime_error("Deletion did not release study");
    }
    auto empty_path=path;empty_path+=".small.simforge";workspace::ScenarioWorkspace small_workspace(baseline());workspace::Project small_project(small_workspace);small_project.save(empty_path,true);begin=Clock::now();q.start_open(empty_path);if(!finish_project(q).success || !loaded.studies().empty())throw std::runtime_error("Project switch did not release history");std::cout<<"switch_to_empty_ms,"<<ms(begin)<<",post_switch_working_set,"<<working_set()<<'\n';
    sampler.request_stop();sampler.join();
    std::cout<<"metric,snapshot_ms,serialization_ms,write_ms,replacement_ms,parse_ms,validation_ms,integrity_ms,restore_ms,scene_ms,publication_ms,total_ms,bytes\n"<<std::fixed<<std::setprecision(4);
    const auto row=[](const char* name,const workspace::ProjectTimings& t){std::cout<<name<<','<<t.snapshot_ms<<','<<t.serialization_ms<<','<<t.write_ms<<','<<t.replacement_ms<<','<<t.parsing_ms<<','<<t.validation_ms<<','<<t.integrity_ms<<','<<t.restoration_ms<<','<<t.scene_ms<<','<<t.publication_ms<<','<<t.total_ms<<','<<t.bytes<<'\n';};row("save",save);row("open",open);row("recovery",recovery_save);row("recovery_open",recovery_open);
    std::cout<<"submission_ms,"<<submission<<",recovery_wall_ms,"<<recovery_ms<<",peak_working_set_bytes,"<<peak()<<'\n';
    for(const auto& [name,memory]:memory_peaks)std::cout<<"memory_phase,"<<name<<','<<memory<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
