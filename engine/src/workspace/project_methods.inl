struct Project::Operation {
    mutable std::mutex mutex;
    ProjectProgress progress;
    ProjectCompletion completion{ProjectOperation::Save};
    ProjectTimings timings;
    std::optional<LoadedProject> loaded;
    std::string marker,saved;
    bool done{false};
    Timer::time_point began{Timer::now()};
    std::stop_source stop;
    std::jthread worker;
    IoControl control(const ProjectWriteHook& hook) {
        return {stop.get_token(),[this](std::string_view phase,std::size_t bytes,std::size_t studies){std::lock_guard lock(mutex);progress.phase=phase;progress.bytes=std::max(progress.bytes,bytes);progress.studies=std::max(progress.studies,studies);},hook,
            [this]{std::lock_guard lock(mutex);detail::checkpoint(stop.get_token());progress.cancellable=false;progress.phase="publication";}};
    }
    void finish() {std::lock_guard lock(mutex);done=true;progress.phase=completion.success?"ready_to_publish":completion.cancelled?"cancelled":"failed";}
    void failed(const std::exception& error) {completion.cancelled=stop.stop_requested();completion.error=completion.cancelled?"Project operation cancelled":error.what();}
};
Project::~Project() {if(operation_){operation_->stop.request_stop();operation_->worker.join();}}
void Project::require_idle() const {if(busy())throw std::logic_error("A project operation is already active");}
bool Project::busy() const noexcept {return bool(operation_);}
bool Project::opening() const noexcept {return operation_ && (operation_->progress.operation==ProjectOperation::Open || operation_->progress.operation==ProjectOperation::RecoveryOpen);}
ProjectProgress Project::progress() const {if(!operation_)return {};std::lock_guard lock(operation_->mutex);return operation_->progress;}
bool Project::cancel() {if(!operation_)return false;std::lock_guard lock(operation_->mutex);if(!operation_->progress.cancellable)return false;operation_->stop.request_stop();operation_->progress.phase="cancelling";return true;}
void Project::shutdown() {if(operation_){cancel();operation_->worker.join();(void)poll();}}
std::filesystem::path Project::save_destination(const std::filesystem::path& path,bool save_as) const {
    auto destination=path.empty()?info_.path:path;if(destination.empty())throw std::invalid_argument("Choose a project filename with Save As");path_valid(destination);
    if(destination.extension()!=".simforge")throw std::invalid_argument("Project filename must end in .simforge");destination=std::filesystem::absolute(destination).lexically_normal();
    if(!save_as && !info_.path.empty() && destination!=info_.path)throw std::invalid_argument("Use Save As to change project location");return destination;
}
std::string Project::serialize() const {
    require_idle();const auto began=Timer::now();const auto capture=ProjectCodec::capture(workspace_,info_,preferences_);timings_={};timings_.snapshot_ms=ms(began);
    std::string bytes;emit_project(capture,[&](std::string_view text){bytes.append(text);},timings_,{});return bytes;
}
void Project::save(const std::filesystem::path& path,bool save_as,const ProjectWriteHook& hook) {
    require_idle();auto destination=save_destination(path,save_as);const bool associated=!info_.path.empty() && destination==info_.path;
    const auto began=Timer::now();timings_={};auto capture=ProjectCodec::capture(workspace_,info_,preferences_);timings_.snapshot_ms=ms(began);
    write_project(destination,capture,associated&&!save_as,timings_,{{},{},hook,{}});
    info_.path=std::move(destination);info_.saved=std::move(capture.saved);saved_marker_=std::move(capture.marker);timings_.total_ms=ms(began);
}
void Project::load(const std::filesystem::path& path,bool recovery) {
    require_idle();const auto began=Timer::now();ProjectTimings measured;auto loaded=read_project(path,recovery,measured,{});
    auto replacement_marker=ProjectCodec::marker(loaded.workspace,loaded.info,loaded.preferences);auto saved=recovery?std::string{}:replacement_marker;
    if(recovery)loaded.workspace.message_="Recovery restored. Use Save As to choose a primary project file.";
    const auto publication=Timer::now();workspace_.execution().cancel();workspace_.execution().shutdown();workspace_=std::move(loaded.workspace);
    info_=std::move(loaded.info);preferences_=std::move(loaded.preferences);saved_marker_=std::move(saved);recovery_marker_=std::move(replacement_marker);last_recovery_=Timer::now();
    measured.publication_ms=ms(publication);measured.total_ms=ms(began);timings_=measured;
}
void Project::submit_save(const std::filesystem::path& path,bool save_as,bool recovery,const ProjectWriteHook& hook) {
    require_idle();const auto began=Timer::now();auto destination=recovery?std::filesystem::absolute(path).lexically_normal():save_destination(path,save_as);
    if(recovery){path_valid(destination);if(destination.extension()!=".recovery" || destination==info_.path)throw std::invalid_argument("Recovery requires a separate .recovery file");}
    auto capture=ProjectCodec::capture(workspace_,info_,preferences_);auto operation=std::make_unique<Operation>();auto* op=operation.get();
    op->began=began;op->timings.snapshot_ms=ms(began);op->marker=capture.marker;op->saved=capture.saved;
    op->progress={recovery?ProjectOperation::RecoverySave:ProjectOperation::Save,destination,"snapshot_prepared",0,0,true,true};op->completion.operation=op->progress.operation;
    const bool replace=recovery || (!save_as && destination==info_.path);
    op->worker=std::jthread([op,destination,replace,hook,capture=std::move(capture)]() mutable {
        try{write_project(destination,capture,replace,op->timings,op->control(hook));op->completion.success=true;}
        catch(const std::exception& error){op->failed(error);}catch(...){op->completion.error="Unknown project save failure";}
        capture={};op->timings.total_ms=ms(op->began);op->finish();
    });operation_=std::move(operation);
}
void Project::start_save(const std::filesystem::path& path,bool save_as,const ProjectWriteHook& hook){submit_save(path,save_as,false,hook);}
void Project::start_open(const std::filesystem::path& path,bool recovery,const ProjectWriteHook& hook) {
    require_idle();path_valid(path);auto operation=std::make_unique<Operation>();auto* op=operation.get();
    op->progress={recovery?ProjectOperation::RecoveryOpen:ProjectOperation::Open,std::filesystem::absolute(path).lexically_normal(),"file_reading",0,0,true,true};op->completion.operation=op->progress.operation;
    op->worker=std::jthread([op,path,recovery,hook] {
        try{const auto control=op->control(hook);op->loaded=read_project(path,recovery,op->timings,control);control.phase("publication_preparation",op->timings.bytes,op->loaded->workspace.studies().size());op->marker=ProjectCodec::marker(op->loaded->workspace,op->loaded->info,op->loaded->preferences);op->saved=recovery?std::string{}:op->marker;if(recovery)op->loaded->workspace.message_="Recovery restored. Use Save As to choose a primary project file.";detail::checkpoint(op->stop.get_token());op->completion.success=true;}
        catch(const std::exception& error){op->loaded.reset();op->failed(error);}catch(...){op->loaded.reset();op->completion.error="Unknown project open failure";}
        op->timings.total_ms=ms(op->began);op->finish();
    });operation_=std::move(operation);
}
std::optional<ProjectCompletion> Project::poll() {
    if(!operation_)return {};{std::lock_guard lock(operation_->mutex);if(!operation_->done)return {};}
    auto op=std::move(operation_);if(op->worker.joinable())op->worker.join();const auto publication=Timer::now();
    const bool opening=op->completion.operation==ProjectOperation::Open || op->completion.operation==ProjectOperation::RecoveryOpen;
    if(opening && op->stop.stop_requested()){op->completion.success=false;op->completion.cancelled=true;op->completion.error="Project operation cancelled";}
    if(op->completion.success) {
        if(opening) {
            workspace_.execution().cancel();workspace_.execution().shutdown();workspace_=std::move(op->loaded->workspace);info_=std::move(op->loaded->info);preferences_=std::move(op->loaded->preferences);
            saved_marker_=std::move(op->saved);recovery_marker_=std::move(op->marker);last_recovery_=Timer::now();
        }else if(op->completion.operation==ProjectOperation::RecoverySave)recovery_marker_=std::move(op->marker);
        else {info_.path=std::move(op->progress.path);info_.saved=std::move(op->saved);saved_marker_=std::move(op->marker);}
    }
    op->timings.publication_ms=ms(publication);op->timings.total_ms=ms(op->began);timings_=op->timings;return std::move(op->completion);
}
bool Project::start_recovery_if_due(const std::filesystem::path& path,Timer::time_point now) {
    if(busy() || now-last_recovery_<std::chrono::seconds(60))return false;last_recovery_=now;
    if(!dirty() || marker()==recovery_marker_)return false;submit_save(path,false,true,{});return true;
}
