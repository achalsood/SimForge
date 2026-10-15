// Included inside the workspace namespace after ProjectCodec. All state here is
// CPU-owned; publication and dirty bookkeeping belong to Project::poll().
namespace {
struct IoControl {
    std::stop_token stop;
    std::function<void(std::string_view,std::size_t,std::size_t)> progress;
    ProjectWriteHook hook;
    std::function<void()> commit;
    void phase(std::string_view name,std::size_t bytes=0,std::size_t studies=0) const {
        detail::checkpoint(stop);if(progress)progress(name,bytes,studies);if(hook)hook(name);detail::checkpoint(stop);
    }
};
using Sink=std::function<void(std::string_view)>;
void emit_project(const ProjectCodec::Capture& capture,const Sink& sink,ProjectTimings& timings,const IoControl& control) {
    const auto began=Timer::now();detail::Crc32 crc;std::size_t payload_bytes=0,total=0;
    const auto out=[&](std::string_view text){if(text.size()>project_max_bytes-total)throw std::length_error("Project exceeds 256 MiB limit");sink(text);total+=text.size();};
    const auto payload_out=[&](std::string_view text){const auto hashing=Timer::now();crc.append(text);timings.integrity_ms+=ms(hashing);payload_bytes+=text.size();out(text);};
    auto features=Json::array({"warehouse_snapshots","clock_phase","historical_reports","independent_runs"});
    bool poisson=has_poisson(capture.metadata),coverage=false;
    for(const auto& run:capture.runs)poisson|=scenario::workload::poisson(std::get<scenario::WarehouseConfig>(run.definition.domain));
    bool checkpoint_study=false;
    bool workload=has_workload(capture.metadata),statistics=has_statistics(capture.metadata),interaction=has_interaction(capture.metadata),sensitivity=has_sensitivity(capture.metadata);
    out("{\"payload\":");payload_out("{");bool first=true;
    for(const auto& [key,value]:capture.metadata.items()) {
        control.phase("serialization",total);
        if(!first)payload_out(",");first=false;payload_out(Json(key).dump());payload_out(":");
        if(key=="baseline") {auto run=ProjectCodec::run(capture.runs.front());workload|=has_workload(run);payload_out(run.dump());}
        else if(key=="runs") {payload_out("[");for(std::size_t i=1;i<capture.runs.size();++i){control.phase("serialization",total);if(i>1)payload_out(",");auto run=ProjectCodec::run(capture.runs[i]);workload|=has_workload(run);payload_out(run.dump());}payload_out("]");}
        else if(key=="study_history") {
            payload_out("[");std::size_t history=0;
            for(std::size_t i=0;i<capture.studies.size();++i) {
                control.phase("study_serialization",total,i);const auto& study=capture.studies[i];
                const auto evidence=comparison_storage_json(*study.evidence);
                poisson|=evidence.find("\"mode\":\"poisson\"")!=std::string::npos || evidence.find("\"mode\":\"piecewise_poisson\"")!=std::string::npos;
                if(evidence.size()!=study.serialized_bytes || evidence.size()>max_study_bytes)invalid("study","evidence size mismatch");
                history+=evidence.size();if(history>max_history_bytes)invalid("history","exceeds 192 MiB");
                workload|=evidence.find("\"workload\":")!=std::string::npos;
                checkpoint_study|=bool(study.evidence->checkpoint_study);statistics|=bool(study.evidence->statistics);interaction|=bool(study.evidence->interaction);sensitivity|=bool(study.evidence->sensitivity);
                Json entry{{"id",study.id},{"type",study_type_name(study.type)},{"title",study.title},{"notes",study.notes},{"completed",study.completed},{"bytes",evidence.size()},{"engine",project_engine_version},{"generator_version",scenario::workload_generator_version},{"evidence",nullptr},{"reserved_confirmation_seeds",study.reserved_confirmation_seeds},{"view",{{"configuration",study.view.configuration},{"seed",study.view.seed},{"metric",study.view.metric},{"rectangle",study.view.rectangle}}}};
                if(i)payload_out(",");payload_out("{");bool field_first=true;
                for(const auto& [name,field]:entry.items()){if(!field_first)payload_out(",");field_first=false;payload_out(Json(name).dump());payload_out(":");payload_out(name=="evidence"?std::string_view(evidence):std::string_view(field.dump()));}
                payload_out("}");control.phase("study_written",total,i+1);
            }
            payload_out("]");
        } else if(key=="demand_calibrations") {
            payload_out("[");
            for(std::size_t i=0;i<capture.calibrations.size();++i) {
                control.phase("calibration_serialization",total,i);
                const auto& record=capture.calibrations[i];const auto evidence=analysis::demand::report_json(*record.evidence);
                coverage|=!record.evidence->provenance.exposure.windows.empty();for(const auto& c:record.evidence->conversions)poisson|=scenario::workload::poisson(std::get<scenario::WarehouseConfig>(c.scenario.domain));
                workload|=evidence.find("\"workload\":")!=std::string::npos;
                if(evidence.size()!=record.serialized_bytes)invalid("calibration","evidence size mismatch");
                if(i)payload_out(",");payload_out(evidence);
            }payload_out("]");
        } else if(key=="demand_diagnostics") {
            payload_out("[");
            for(std::size_t i=0;i<capture.diagnostics.size();++i) {
                control.phase("diagnostic_serialization",total,i);
                const auto& record=capture.diagnostics[i];const auto evidence=analysis::diagnostics::report_json(*record.evidence);
                coverage|=!record.evidence->request.source->provenance.exposure.windows.empty();
                for(const auto& c:record.evidence->request.source->conversions)poisson|=scenario::workload::poisson(std::get<scenario::WarehouseConfig>(c.scenario.domain));
                workload|=evidence.find("\"workload\":")!=std::string::npos;
                if(evidence.size()!=record.serialized_bytes)invalid("diagnostic","evidence size mismatch");
                if(i)payload_out(",");payload_out(evidence);
            }payload_out("]");
        } else payload_out(value.dump());
    }
    payload_out("}");control.phase("integrity_finalization",total,capture.studies.size());
    if(workload)features.push_back("workload_v1");if(statistics)features.push_back("statistics_v1");if(interaction)features.push_back("interaction_v1");if(sensitivity)features.push_back("sensitivity_v1");
    if(checkpoint_study)features.push_back("checkpoint_intervention_v1");
    if(capture.metadata.contains("demand_calibrations"))features.push_back("demand_calibration_v1");
    if(capture.metadata.contains("demand_diagnostics"))features.push_back("demand_diagnostics_v1");
    if(poisson)features.push_back("poisson_arrivals_v2");if(coverage)features.push_back("coverage_windows_v2");
    Json header{{"crc32",crc.value()},{"engine",project_engine_version},{"features",std::move(features)},{"format","SimForge"},{"payload_size",payload_bytes},{"scenario_schema",1},{"version",project_format_version}};
    for(const auto& [key,value]:header.items()){out(",");out(Json(key).dump());out(":");out(value.dump());}out("}");
    timings.bytes=total;timings.serialization_ms=ms(began)-timings.write_ms-timings.integrity_ms;
}

void write_project(const std::filesystem::path& destination,const ProjectCodec::Capture& capture,bool replace,ProjectTimings& timings,const IoControl& control) {
    path_valid(destination);auto temporary=destination;temporary+=L".tmp-"+std::filesystem::path(identity()).wstring();
    bool owned=false;
#ifdef _WIN32
    HANDLE file=INVALID_HANDLE_VALUE;
#else
    std::ofstream file;
#endif
    try {
        control.phase("before_write");
#ifdef _WIN32
        file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create project temporary file (Windows error "+std::to_string(GetLastError())+")");
#else
        file.open(temporary,std::ios::binary|std::ios::noreplace);if(!file)throw std::runtime_error("Cannot create project temporary file");
#endif
        owned=true;std::array<char,64*1024> buffer{};std::size_t used=0,written_bytes=0;
        const auto flush=[&] {
            if(!used)return;control.phase("file_writing",written_bytes);const auto began=Timer::now();
#ifdef _WIN32
            DWORD written=0;if(!WriteFile(file,buffer.data(),static_cast<DWORD>(used),&written,nullptr) || written!=used)throw std::runtime_error("Project write failed (Windows error "+std::to_string(GetLastError())+")");
#else
            file.write(buffer.data(),static_cast<std::streamsize>(used));if(!file)throw std::runtime_error("Project write failed");
#endif
            written_bytes+=used;used=0;timings.write_ms+=ms(began);
        };
        emit_project(capture,[&](std::string_view text){while(!text.empty()){detail::checkpoint(control.stop);const auto count=std::min(text.size(),buffer.size()-used);std::copy_n(text.data(),count,buffer.data()+used);used+=count;text.remove_prefix(count);if(used==buffer.size())flush();}},timings,control);
        flush();control.phase("file_flush",written_bytes);const auto began=Timer::now();
#ifdef _WIN32
        if(!FlushFileBuffers(file))throw std::runtime_error("Project flush failed (Windows error "+std::to_string(GetLastError())+")");
        const auto closed=CloseHandle(file);file=INVALID_HANDLE_VALUE;if(!closed)throw std::runtime_error("Project close failed");
#else
        file.close();if(!file)throw std::runtime_error("Project close failed");const auto fd=::open(temporary.c_str(),O_RDONLY);if(fd<0)throw std::runtime_error("Project flush open failed");const auto status=::fsync(fd);::close(fd);if(status)throw std::runtime_error("Project flush failed");
#endif
        timings.write_ms+=ms(began);control.phase("after_write",written_bytes);control.phase("before_replace",written_bytes);
        if(control.commit)control.commit();else detail::checkpoint(control.stop);
        const auto replacing=Timer::now();
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH|(replace?MOVEFILE_REPLACE_EXISTING:0)))throw std::runtime_error("Project replacement failed; previous save retained (Windows error "+std::to_string(GetLastError())+")");
#else
        if(replace)std::filesystem::rename(temporary,destination);else{std::filesystem::create_hard_link(temporary,destination);std::filesystem::remove(temporary);}
#endif
        owned=false;timings.replacement_ms=ms(replacing);
    }catch(...) {
#ifdef _WIN32
        if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
#else
        file.close();
#endif
        if(owned){std::error_code ignored;std::filesystem::remove(temporary,ignored);}throw;
    }
}

struct LoadedProject {ScenarioWorkspace workspace;ProjectInfo info;ProjectPreferences preferences;};
LoadedProject read_project(const std::filesystem::path& path,bool recovery,ProjectTimings& timings,const IoControl& control) {
    path_valid(path);const auto began=Timer::now();control.phase("file_reading");
    detail::InputBuffer buffer(path,project_max_bytes,control.stop,[&](std::size_t bytes){if(control.progress)control.progress("file_reading",bytes,0);});
    std::istream input(&buffer);input.exceptions(std::ios::badbit);
    std::size_t events=0,study_events=0,history_bytes=0;std::vector<std::vector<std::string>> objects;std::vector<StudyRecord> studies;
    ScenarioWorkspace decoder;ProjectCodec::decoder_ids(decoder);
    std::vector<CalibrationStudy> calibrations;std::set<std::string> calibration_ids;
    std::vector<DiagnosticStudy> diagnostics;std::set<std::string> diagnostic_ids;
    bool diagnostic_active=false,diagnostic_seen=false;std::size_t diagnostic_events=0;
    detail::Digest diagnostic_digest;diagnostic_digest.append("[");
    bool calibration_active=false,calibration_seen=false;std::size_t calibration_events=0;
    bool poisson=false,coverage=false,checkpoint_study=false;
    detail::Digest calibration_digest;calibration_digest.append("[");
    bool history_active=false,history_seen=false,runs_active=false,runs_seen=false,workload=false,statistics=false,interaction=false,sensitivity=false;
    auto restored=std::make_unique<ProjectCodec::RestoredRuns>();Json baseline_summary,runs_summary=Json::array();
    std::optional<detail::Digest> baseline_digest;detail::Digest runs_digest;runs_digest.append("[");
    std::string payload_key;detail::Digest history;history.append("[");
    const auto callback=[&](int depth,Json::parse_event_t event,Json& value) {
        if((events&1023)==0)detail::checkpoint(control.stop);
        if(depth>32 || ++events>project_max_json_events)throw std::length_error("JSON nesting/event limit exceeded");
        if(events==1)control.phase("parsing",buffer.bytes(),0);
        if(history_active && ++study_events>4'000'000)throw std::length_error("Study JSON event limit exceeded");
        if(calibration_active && ++calibration_events>100'000)throw std::length_error("Calibration JSON event limit exceeded");
        if(diagnostic_active && ++diagnostic_events>500'000)throw std::length_error("Diagnostic JSON event limit exceeded");
        if(event==Json::parse_event_t::object_start)objects.emplace_back();
        else if(event==Json::parse_event_t::key) {
            const auto key=value.get<std::string>();if(key.size()>4096)invalid("JSON","key length exceeds limit");
            if(depth==1) {
                constexpr std::array<std::string_view,8> allowed{"format","version","engine","scenario_schema","features","payload_size","crc32","payload"};
                if(std::find(allowed.begin(),allowed.end(),key)==allowed.end())invalid(key,"unexpected project field");
            }
            if(depth==2 && !history_active && !runs_active) {
                constexpr std::array<std::string_view,22> allowed{"identity","baseline","draft","draft_origin","candidates","runs","active_run","next_run_id","next_candidate_id","default_speed","preferences","report","preview","draft_workload","draft_origin_workload","study_history","selected_study","next_study_id","demand_calibrations","selected_calibration","demand_diagnostics","selected_diagnostic"};
                if(std::find(allowed.begin(),allowed.end(),key)==allowed.end())invalid(key,"unexpected payload field");
            }
            auto& keys=objects.back();if(keys.size()>=64)throw std::length_error("JSON object field limit exceeded");
            if(std::find(keys.begin(),keys.end(),key)!=keys.end())invalid(key,"duplicate JSON field");keys.push_back(key);
            workload|=key=="workload" || key=="draft_workload" || key=="draft_origin_workload";
            checkpoint_study|=key=="checkpoint_report_version";
            statistics|=key=="statistics" || key=="statistical_report_version";
            interaction|=key=="interaction" || key=="interaction_report_version";
            sensitivity|=key=="sensitivity" || key=="sensitivity_report_version";
            coverage|=key=="coverage_windows";
            poisson|=key=="intensities";
            if(depth==2)payload_key=key;
        }else if(event==Json::parse_event_t::object_end)objects.pop_back();
        if(event==Json::parse_event_t::value && !objects.empty() && !objects.back().empty()) {
            const auto& key=objects.back().back();
            if(key=="mode" && value.is_string())poisson|=value=="poisson" || value=="piecewise_poisson";
            if(key=="arrivals" && value.is_number_integer())poisson|=value==3 || value==4;
        }
        if(depth==2 && payload_key=="study_history" && event==Json::parse_event_t::array_start){history_active=true;history_seen=true;}
        if(depth==2 && payload_key=="runs" && event==Json::parse_event_t::array_start){runs_active=true;runs_seen=true;}
        if(depth==2 && payload_key=="demand_calibrations" && event==Json::parse_event_t::array_start){calibration_active=true;calibration_seen=true;}
        if(depth==2 && payload_key=="demand_diagnostics" && event==Json::parse_event_t::array_start){diagnostic_active=true;diagnostic_seen=true;}
        if(diagnostic_active && depth==3 && (event==Json::parse_event_t::value || event==Json::parse_event_t::array_start))invalid("diagnostic","expected report object");
        if(diagnostic_active && depth==3 && event==Json::parse_event_t::object_end) {
            control.phase("diagnostic_reconstruction",buffer.bytes(),diagnostics.size());
            if(diagnostics.size()>=max_studies)invalid("diagnostics","at most 32 retained studies");
            const auto bytes=value.dump();auto report=analysis::diagnostics::parse_report(bytes);
            if(!diagnostic_ids.insert(report->id).second)invalid("diagnostics","duplicate diagnostic ID");
            if(!diagnostics.empty())diagnostic_digest.append(",");diagnostic_digest.append(bytes);
            diagnostics.push_back({std::move(report),bytes.size()});diagnostic_events=0;return false;
        }
        if(diagnostic_active && depth==2 && event==Json::parse_event_t::array_end){diagnostic_active=false;diagnostic_digest.append("]");}
        if(calibration_active && depth==3 && (event==Json::parse_event_t::value || event==Json::parse_event_t::array_start))invalid("calibration","expected report object");
        if(calibration_active && depth==3 && event==Json::parse_event_t::object_end) {
            control.phase("calibration_reconstruction",buffer.bytes(),calibrations.size());
            if(calibrations.size()>=max_studies)invalid("calibrations","at most 32 retained studies");
            const auto bytes=value.dump();auto report=analysis::demand::parse_report(bytes);
            if(!calibration_ids.insert(report->id).second)invalid("calibrations","duplicate calibration ID");
            if(!calibrations.empty())calibration_digest.append(",");calibration_digest.append(bytes);
            calibrations.push_back({std::move(report),bytes.size()});calibration_events=0;return false;
        }
        if(calibration_active && depth==2 && event==Json::parse_event_t::array_end){calibration_active=false;calibration_digest.append("]");}
        if(depth==3 && event==Json::parse_event_t::array_start && (history_active || runs_active))invalid("project collection","expected object entry");
        if((depth==2 && payload_key=="baseline" && event==Json::parse_event_t::object_end) || (runs_active && depth==3 && event==Json::parse_event_t::object_end)) {
            control.phase("run_reconstruction",buffer.bytes(),studies.size());
            const bool baseline_run=depth==2;const auto run_id=id(value.at("id"));
            if(baseline_run && run_id)invalid("baseline","identity must be zero");
            if(!baseline_run && restored->runs.size()>=max_custom_runs)invalid("runs","too many independent runs");
            auto summary=Json{{"id",run_id},{"selection",value.at("selection")}};detail::Digest digest;digest.append(value.dump());workload|=has_workload(value);
            if(baseline_run){baseline_digest=digest;baseline_summary=std::move(summary);restored->baseline=ProjectCodec::run(value,timings);}
            else {if(!restored->runs.empty())runs_digest.append(",");runs_digest.append(digest);runs_summary.push_back(std::move(summary));restored->runs.push_back({run_id,ProjectCodec::run(value,timings)});}
            return false;
        }
        if(runs_active && depth==3 && event==Json::parse_event_t::value)invalid("run","expected object entry");
        if(runs_active && depth==2 && event==Json::parse_event_t::array_end){runs_active=false;runs_digest.append("]");}
        if(history_active && depth==3 && event==Json::parse_event_t::value)invalid("study","expected object entry");
        if(history_active && depth==3 && event==Json::parse_event_t::object_end) {
            if(studies.size()>=max_studies)invalid("history","too many studies");
            control.phase("study_reconstruction",buffer.bytes(),studies.size());const auto start=Timer::now();
            const auto hashing=Timer::now();const auto canonical=value.dump();if(canonical.size()>max_study_bytes+256*1024)throw std::length_error("Study entry exceeds byte limit");
            if(!studies.empty())history.append(",");history.append(canonical);
            const auto integrity_ms=ms(hashing);timings.integrity_ms+=integrity_ms;
            auto study=ProjectCodec::study(decoder,value);history_bytes+=study.serialized_bytes;if(history_bytes>max_history_bytes)invalid("history","exceeds 192 MiB");
            studies.push_back(std::move(study));timings.validation_ms+=ms(start)-integrity_ms;study_events=0;return false; // release this entry, never a complete history DOM
        }
        if(history_active && depth==2 && event==Json::parse_event_t::array_end){history_active=false;history.append("]");}
        if(event==Json::parse_event_t::value && value.is_string() && value.get_ref<const std::string&>().size()>warehouse::warehouse_snapshot_max_bytes*2)throw std::length_error("Project JSON string exceeds limit");
        return true;
    };
    Json j;
    try {j=Json::parse(input,callback);}catch(const Json::exception& e){invalid("JSON",e.what());}
    timings.parsing_ms=ms(began)-timings.validation_ms-timings.integrity_ms-timings.scene_ms-timings.restoration_ms;timings.bytes=buffer.bytes();control.phase("validation",buffer.bytes(),studies.size());const auto validating=Timer::now();
    if(baseline_digest)j.at("payload")["baseline"]=std::move(baseline_summary);
    if(runs_seen)j.at("payload")["runs"]=std::move(runs_summary);
    fields(j,{"format","version","engine","scenario_schema","features","payload_size","crc32","payload"},{},"project");
    if(bounded(j.at("format"),16,"format")!="SimForge")invalid("format","not a SimForge project");
    const auto version=integer(j.at("version"),UINT32_MAX,"version");if(version!=1 && version!=project_format_version)invalid("version","unsupported project version");
    const auto& payload=j.at("payload");if((version==2)!=history_seen || history_seen!=payload.contains("study_history"))invalid("history","version and history representation disagree");
    if(calibration_seen!=payload.contains("demand_calibrations") || (calibration_seen && version!=2))invalid("calibrations","v2 bounded collection required");
    if(diagnostic_seen!=payload.contains("demand_diagnostics") || (diagnostic_seen && version!=2))invalid("diagnostics","v2 bounded collection required");
    if(bounded(j.at("engine"),64,"engine")!=project_engine_version || integer(j.at("scenario_schema"),1,"scenario schema")!=1)invalid("engine","incompatible simulation/schema version");
    auto features=Json::array({"warehouse_snapshots","clock_phase","historical_reports","independent_runs"});
    if(workload||has_workload(payload))features.push_back("workload_v1");if(statistics||has_statistics(payload))features.push_back("statistics_v1");if(interaction||has_interaction(payload))features.push_back("interaction_v1");if(sensitivity||has_sensitivity(payload))features.push_back("sensitivity_v1");
    if(checkpoint_study)features.push_back("checkpoint_intervention_v1");
    if(calibration_seen)features.push_back("demand_calibration_v1");
    if(diagnostic_seen)features.push_back("demand_diagnostics_v1");
    if(poisson)features.push_back("poisson_arrivals_v2");if(coverage)features.push_back("coverage_windows_v2");
    if(j.at("features")!=features)invalid("features","unsupported or missing persistence features");
    const auto integrity=Timer::now();detail::Digest canonical;canonical.append("{");bool first=true;
    for(const auto& [key,value]:payload.items()){if(!first)canonical.append(",");first=false;canonical.append(Json(key).dump());canonical.append(":");if(key=="study_history" && history_seen)canonical.append(history);else if(key=="demand_calibrations" && calibration_seen)canonical.append(calibration_digest);else if(key=="demand_diagnostics" && diagnostic_seen)canonical.append(diagnostic_digest);else if(key=="baseline" && baseline_digest)canonical.append(*baseline_digest);else if(key=="runs" && runs_seen)canonical.append(runs_digest);else canonical.append(value.dump());}
    canonical.append("}");if(integer(j.at("payload_size"),project_max_bytes,"payload size")!=canonical.bytes || integer(j.at("crc32"),UINT32_MAX,"crc32")!=canonical.crc)invalid("project","corrupt project payload");const auto final_integrity=ms(integrity);timings.integrity_ms+=final_integrity;
    LoadedProject result;control.phase("workspace_reconstruction",buffer.bytes(),studies.size());
    if(!baseline_digest || !runs_seen)invalid("runs","baseline object and runs array required");
    result.workspace=ProjectCodec::payload(payload,result.info,result.preferences,timings,history_seen?std::optional(std::move(studies)):std::nullopt,std::move(restored),std::move(calibrations),std::move(diagnostics));
    timings.validation_ms+=ms(validating)-final_integrity;
    if(!recovery)result.info.path=std::filesystem::absolute(path).lexically_normal();control.phase("ready_to_publish",buffer.bytes(),history_seen?result.workspace.studies().size():0);return result;
}
}
