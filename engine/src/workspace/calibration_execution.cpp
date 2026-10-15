#include <simforge/workspace/calibration_execution.hpp>
#include <simforge/workspace/scenario_workspace.hpp>
#include <simforge/workspace/experiment_execution.hpp>
#include <algorithm>
#include <utility>
namespace simforge::workspace {
CalibrationExecution::~CalibrationExecution(){shutdown();}
void CalibrationExecution::start(analysis::demand::FileRequest request,Observer observer,bool import_only,std::shared_ptr<const analysis::demand::Dataset> dataset) {
    if(progress().active)throw std::logic_error("One calibration job is already running");
    if(worker_.joinable())worker_.join();
    {std::lock_guard lock(mutex_);progress_={true,false,"Reading",{}};result_.reset();dataset_.reset();diagnostics_.reset();}
    try {worker_=std::jthread([this,request=std::move(request),observer=std::move(observer),import_only,dataset=std::move(dataset)](std::stop_token stop){
        try {
            simulation::ExecutionControl control{stop,[this,&observer](auto phase,auto,auto n,auto total){
                {std::lock_guard lock(mutex_);progress_.phase=phase;progress_.processed=n;progress_.total=total;}if(observer)observer(phase);
            }};
            std::shared_ptr<const analysis::demand::Report> report;
            auto imported=import_only?analysis::demand::load_csv(request.csv,request.exposure,&control):nullptr;
            if(!import_only)report=dataset?analysis::demand::fit(dataset,request.fit,&control):analysis::demand::calibrate(request,&control);
            control.check();
            std::lock_guard lock(mutex_);if(stop.stop_requested())throw simulation::ExecutionCancelled{};
            result_=std::move(report);dataset_=std::move(imported);progress_.active=false;progress_.phase="Completed";
        }catch(const simulation::ExecutionCancelled&){std::lock_guard lock(mutex_);progress_.active=false;progress_.cancelled=true;progress_.phase="Cancelled";}
        catch(const std::exception& e){std::lock_guard lock(mutex_);progress_.active=false;progress_.phase="Failed";progress_.error=e.what();}
    });}catch(...){std::lock_guard lock(mutex_);progress_.active=false;progress_.phase="Failed";throw;}
}
void CalibrationExecution::cancel(){if(worker_.joinable())worker_.request_stop();}
void CalibrationExecution::start_diagnostics(analysis::diagnostics::Request request,std::shared_ptr<const analysis::demand::Dataset> dataset,Observer observer) {
    if(progress().active)throw std::logic_error("One calibration/diagnostic job is already running");
    if(worker_.joinable())worker_.join();
    {std::lock_guard lock(mutex_);progress_={true,false,"Validating",{}};result_.reset();dataset_.reset();diagnostics_.reset();}
    try {worker_=std::jthread([this,request=std::move(request),dataset=std::move(dataset),observer=std::move(observer)](std::stop_token stop){
        try {
            simulation::ExecutionControl control{stop,[this,&observer](auto phase,auto,auto n,auto total){
                {std::lock_guard lock(mutex_);progress_.phase=phase;progress_.processed=n;progress_.total=total;}if(observer)observer(phase);
            }};
            auto report=analysis::diagnostics::run(request,dataset,&control);control.check();
            std::lock_guard lock(mutex_);if(stop.stop_requested())throw simulation::ExecutionCancelled{};
            diagnostics_=std::move(report);progress_.active=false;progress_.phase="Completed";
        }catch(const simulation::ExecutionCancelled&){std::lock_guard lock(mutex_);progress_.active=false;progress_.cancelled=true;progress_.phase="Cancelled";}
        catch(const std::exception& e){std::lock_guard lock(mutex_);progress_.active=false;progress_.phase="Failed";progress_.error=e.what();}
    });}catch(...){std::lock_guard lock(mutex_);progress_.active=false;progress_.phase="Failed";throw;}
}
void CalibrationExecution::shutdown(){cancel();if(worker_.joinable())worker_.join();}
CalibrationProgress CalibrationExecution::progress()const{std::lock_guard lock(mutex_);return progress_;}
std::shared_ptr<const analysis::demand::Report> CalibrationExecution::take_result(){std::lock_guard lock(mutex_);return std::exchange(result_,{});}
std::shared_ptr<const analysis::demand::Dataset> CalibrationExecution::take_dataset(){std::lock_guard lock(mutex_);return std::exchange(dataset_,{});}
std::shared_ptr<const analysis::diagnostics::Report> CalibrationExecution::take_diagnostics(){std::lock_guard lock(mutex_);return std::exchange(diagnostics_,{});}
CalibrationExecution& ScenarioWorkspace::calibration_execution(){return *calibration_execution_;}
void ScenarioWorkspace::start_calibration(analysis::demand::FileRequest request,CalibrationExecution::Observer observer) {
    if(execution().progress().active())throw std::logic_error("Wait for or cancel the active experiment before calibration");
    calibration_execution().start(std::move(request),std::move(observer));
}
bool ScenarioWorkspace::poll_calibration() {
    if(auto diagnostic=calibration_execution().take_diagnostics()){diagnostic_report_=std::move(diagnostic);++persistent_generation_;return true;}
    if(auto data=calibration_execution().take_dataset())calibration_dataset_=std::move(data);
    if(auto result=calibration_execution().take_result()){calibration_report_=std::move(result);++persistent_generation_;return true;}return false;
}
void ScenarioWorkspace::import_calibration(analysis::demand::FileRequest request) {
    if(execution().progress().active())throw std::logic_error("Wait for or cancel the active experiment");
    calibration_execution().start(std::move(request),{},true);
}
void ScenarioWorkspace::fit_calibration(analysis::demand::FitSettings settings) {
    if(!calibration_dataset_)throw std::logic_error("Import a validated observation dataset first");
    if(execution().progress().active())throw std::logic_error("Wait for or cancel the active experiment");
    calibration_execution().start({{},calibration_dataset_->provenance.exposure,settings},{},false,calibration_dataset_);
}
bool ScenarioWorkspace::has_pending_calibration()const {
    return calibration_report_ && std::none_of(calibrations_.begin(),calibrations_.end(),[&](const auto& r){return r.evidence==calibration_report_;});
}
void ScenarioWorkspace::retain_calibration(std::shared_ptr<const analysis::demand::Report> report) {
    if(!report)throw std::invalid_argument("No completed calibration report");
    const auto bytes=analysis::demand::report_json(*report);(void)analysis::demand::parse_report(bytes);
    for(const auto& r:calibrations_)if(r.evidence->id==report->id){calibration_report_=r.evidence;return;}
    if(studies_.size()+calibrations_.size()+diagnostics_.size()>=max_studies || history_bytes()+bytes.size()>max_history_bytes)throw std::length_error("Study History is full; explicitly delete a retained study, then save calibration");
    calibrations_.push_back({report,bytes.size()});calibration_report_=std::move(report);++persistent_generation_;
}
void ScenarioWorkspace::select_calibration(std::string_view id) {
    const auto found=std::find_if(calibrations_.begin(),calibrations_.end(),[&](const auto& r){return r.evidence->id==id;});
    if(found==calibrations_.end())throw std::invalid_argument("Unknown calibration ID");if(calibration_report_!=found->evidence){calibration_report_=found->evidence;++persistent_generation_;}
}
void ScenarioWorkspace::delete_calibration(std::string_view id,bool confirmed) {
    if(!confirmed)throw std::invalid_argument("Confirm permanent calibration deletion");
    if(calibration_execution().progress().active)throw std::logic_error("Cancel calibration before deleting studies");
    if(std::none_of(calibrations_.begin(),calibrations_.end(),[&](const auto& r){return r.evidence->id==id;}))throw std::invalid_argument("Unknown calibration ID");
    std::erase_if(calibrations_,[&](const auto& r){return r.evidence->id==id;});
    if(calibration_report_ && calibration_report_->id==id)calibration_report_=calibrations_.empty()?nullptr:calibrations_.back().evidence;++persistent_generation_;
}
void ScenarioWorkspace::discard_calibration(bool confirmed) {
    if(!confirmed)throw std::invalid_argument("Confirm discarding unretained calibration");
    calibration_report_=calibrations_.empty()?nullptr:calibrations_.back().evidence;++persistent_generation_;
}
void ScenarioWorkspace::record_calibration_conversion(analysis::demand::Conversion conversion) {
    if(!calibration_report_ || conversion.calibration_id!=calibration_report_->id)throw std::invalid_argument("Conversion does not match active calibration");
    const auto retained=std::find_if(calibrations_.begin(),calibrations_.end(),[&](const auto& r){return r.evidence->id==calibration_report_->id;});
    auto replacement=std::make_shared<analysis::demand::Report>(retained==calibrations_.end()?*calibration_report_:*retained->evidence);
    if(replacement->conversions.size()>=8)throw std::length_error("At most 8 conversions per calibration study");
    replacement->conversions.push_back(std::move(conversion));const auto bytes=analysis::demand::report_json(*replacement);(void)analysis::demand::parse_report(bytes);
    for(auto& record:calibrations_)if(record.evidence->id==calibration_report_->id) {
        if(history_bytes()-record.serialized_bytes+bytes.size()>max_history_bytes)throw std::length_error("History size limit exceeded");
        record.evidence=replacement;record.serialized_bytes=bytes.size();
    }
    calibration_report_=std::move(replacement);++persistent_generation_;
}
void ScenarioWorkspace::start_diagnostics(analysis::diagnostics::Request request,CalibrationExecution::Observer observer) {
    if(execution().progress().active())throw std::logic_error("Wait for or cancel the active experiment");
    const auto source=std::find_if(calibrations_.begin(),calibrations_.end(),[&](const auto& c){return request.source && c.evidence->id==request.source->id;});
    if(source==calibrations_.end())throw std::invalid_argument("Select a retained completed calibration first");
    request.source=source->evidence;auto dataset=calibration_dataset_;
    if(dataset && dataset->provenance.dataset_id!=request.source->provenance.dataset_id)dataset.reset();
    calibration_execution().start_diagnostics(std::move(request),std::move(dataset),std::move(observer));
}
bool ScenarioWorkspace::has_pending_diagnostic()const {
    return diagnostic_report_ && std::none_of(diagnostics_.begin(),diagnostics_.end(),[&](const auto& r){return r.evidence==diagnostic_report_;});
}
void ScenarioWorkspace::retain_diagnostic(std::shared_ptr<const analysis::diagnostics::Report> report) {
    if(!report)throw std::invalid_argument("No completed diagnostic report");
    auto bytes=analysis::diagnostics::report_json(*report);report=analysis::diagnostics::parse_report(bytes);
    for(const auto& record:diagnostics_)if(record.evidence->id==report->id){diagnostic_report_=record.evidence;return;}
    if(studies_.size()+calibrations_.size()+diagnostics_.size()>=max_studies || history_bytes()+bytes.size()>max_history_bytes)throw std::length_error("Study History limit exceeded; previous evidence retained");
    diagnostics_.push_back({report,bytes.size()});diagnostic_report_=std::move(report);++persistent_generation_;
}
void ScenarioWorkspace::select_diagnostic(std::string_view id) {
    const auto record=std::find_if(diagnostics_.begin(),diagnostics_.end(),[&](const auto& r){return r.evidence->id==id;});
    if(record==diagnostics_.end())throw std::invalid_argument("Unknown diagnostic ID");if(diagnostic_report_!=record->evidence){diagnostic_report_=record->evidence;++persistent_generation_;}
}
void ScenarioWorkspace::delete_diagnostic(std::string_view id,bool confirmed) {
    if(!confirmed)throw std::invalid_argument("Confirm permanent diagnostic deletion");
    if(calibration_execution().progress().active)throw std::logic_error("Cancel active calibration/diagnostics first");
    if(std::none_of(diagnostics_.begin(),diagnostics_.end(),[&](const auto& r){return r.evidence->id==id;}))throw std::invalid_argument("Unknown diagnostic ID");
    std::erase_if(diagnostics_,[&](const auto& r){return r.evidence->id==id;});
    if(diagnostic_report_ && diagnostic_report_->id==id)diagnostic_report_=diagnostics_.empty()?nullptr:diagnostics_.back().evidence;++persistent_generation_;
}
void ScenarioWorkspace::rename_diagnostic(std::string_view id,std::string title,std::string notes) {
    auto record=std::find_if(diagnostics_.begin(),diagnostics_.end(),[&](const auto& r){return r.evidence->id==id;});if(record==diagnostics_.end())throw std::invalid_argument("Unknown diagnostic ID");
    auto replacement=std::make_shared<analysis::diagnostics::Report>(*record->evidence);replacement->title=std::move(title);replacement->notes=std::move(notes);
    auto bytes=analysis::diagnostics::report_json(*replacement);(void)analysis::diagnostics::parse_report(bytes);
    if(history_bytes()-record->serialized_bytes+bytes.size()>max_history_bytes)throw std::length_error("History byte limit exceeded");
    if(diagnostic_report_==record->evidence)diagnostic_report_=replacement;record->evidence=std::move(replacement);record->serialized_bytes=bytes.size();++persistent_generation_;
}
void ScenarioWorkspace::discard_diagnostic(bool confirmed) {
    if(!confirmed)throw std::invalid_argument("Confirm discarding unretained diagnostic");diagnostic_report_=diagnostics_.empty()?nullptr:diagnostics_.back().evidence;++persistent_generation_;
}
}
