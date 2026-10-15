// Opt-in actual SDL/Vulkan workflow; never executes during normal interaction.
if(smoke_frames && std::getenv("SIMFORGE_DEMAND_DIAGNOSTICS_TEST_PATH")) {
    namespace diagnostics=analysis::diagnostics;
    const auto destination=std::filesystem::path(std::getenv("SIMFORGE_DEMAND_DIAGNOSTICS_TEST_PATH"));
    window.resize(1600,1100);submit();submit();click(dashboard.analysis_mode);submit();
    auto& calibration=workspace_ui.calibration;auto& ui=calibration.diagnostics;
    const auto thread_cpu=[]()->std::optional<std::uint64_t> {
#ifdef _WIN32
        FILETIME created{},exited{},kernel{},user{};
        if(!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user))return {};
        const auto ticks=[](const FILETIME& value){return (std::uint64_t{value.dwHighDateTime}<<32)|value.dwLowDateTime;};
        return ticks(kernel)+ticks(user);
#else
        return {};
#endif
    };
    const auto wait_diagnostics=[&]{const auto deadline=Clock::now()+std::chrono::seconds{30};while(workspace.calibration_execution().progress().active){require(Clock::now()<deadline,"Demand diagnostics timed out");submit();}submit();submit();};
    calibration.open=true;calibration.tab=4;calibration.scroll_target=0;submit();submit();
    if(project_path) {
        require(workspace.diagnostics().size()==1 && workspace.diagnostic_report(),"Diagnostic study missing after fresh-process Open");
        require(!workspace.calibration_dataset(),"Raw historical timestamps were unnecessarily persisted");
        const auto& r=*workspace.diagnostic_report();require(r.replications.size()==1000 && r.request.source->provenance.exposure.windows.size()==72,"Diagnostic reference/provenance lost");
        require(r.title=="Reviewed warehouse demand diagnostics" && r.notes=="Frozen fitted rates; original CSV verified.","Diagnostic metadata lost");
        ui.tab=4;calibration.scroll_target=0;submit();submit();capture("diagnostics-restored");
    } else {
        const auto baseline=workspace.view().runtime().capture_snapshot();
        const auto root=scenario_path.parent_path()/"stochastic-coverage";
        workspace.start_calibration(analysis::demand::load_request(root/"multi-shift-constant.json"));wait_diagnostics();
        require(bool(workspace.calibration_report()),"Source calibration failed");workspace.retain_calibration(workspace.calibration_report());
        calibration.tab=2;calibration.scroll_target=0;submit();submit();click(calibration.tabs[4]);submit();require(calibration.tab==4,"Demand Diagnostics navigation failed");
        ui.tab=0;ui.prediction_start="2026-02-05T08:00:00Z";ui.replications="1000";ui.seed="90210";ui.reimport=true;ui.csv_path=(root/"multi-shift.csv").string();
        calibration.scroll_target=0;submit();submit();capture("diagnostics-setup");
        ui.scroll_target=1;submit();submit();click(ui.run);std::size_t diagnostic_frames=0;double diagnostic_frame_ms=0,diagnostic_max_ms=0,diagnostic_ui_us=0;std::uint64_t diagnostic_cpu_ticks=0;bool cpu_valid=true;while(workspace.calibration_execution().progress().active){const auto started=Clock::now();const auto ui_before=ui.render_us;const auto cpu_before=thread_cpu();submit();const auto cpu_after=thread_cpu();if(cpu_before && cpu_after && *cpu_after>=*cpu_before)diagnostic_cpu_ticks+=*cpu_after-*cpu_before;else cpu_valid=false;diagnostic_ui_us+=ui.render_us-ui_before;const auto dt=std::chrono::duration<double,std::milli>(Clock::now()-started).count();diagnostic_frame_ms+=dt;diagnostic_max_ms=std::max(diagnostic_max_ms,dt);++diagnostic_frames;}submit();submit();if(capture_output){std::ofstream performance(destination.parent_path()/"diagnostics-native-benchmark.csv");performance<<"replications,observations,valid_bins,active_frames,mean_frame_ms,max_frame_ms,mean_diagnostic_ui_us,mean_main_thread_cpu_ms\n"<<"1000,210,8,"<<diagnostic_frames<<','<<(diagnostic_frames?diagnostic_frame_ms/diagnostic_frames:0)<<','<<diagnostic_max_ms<<','<<(diagnostic_frames?diagnostic_ui_us/diagnostic_frames:0)<<',';if(cpu_valid && diagnostic_frames)performance<<static_cast<double>(diagnostic_cpu_ticks)/10000/static_cast<double>(diagnostic_frames);performance<<'\n';}
        require(workspace.diagnostic_report() && workspace.diagnostic_report()->replications.size()==1000,"Native diagnostic run failed");
        const auto completed=workspace.diagnostic_report();
        diagnostics::Request request=completed->request;request.csv=root/"multi-shift.csv";auto cpu=diagnostics::run(request);
        auto native_report=*completed,cpu_report=*cpu;native_report.preparation_ms=native_report.simulation_ms=cpu_report.preparation_ms=cpu_report.simulation_ms=0;require(diagnostics::report_json(native_report)==diagnostics::report_json(cpu_report),"GUI and headless diagnostics differ");
        ui.tab=1;calibration.scroll_target=0;submit();submit();capture("diagnostics-counts-residuals");ui.scroll_target=5;submit();submit();capture("diagnostics-residuals");
        ui.tab=2;calibration.scroll_target=0;submit();submit();capture("diagnostics-reference");
        ui.tab=3;calibration.scroll_target=0;submit();submit();capture("diagnostics-prediction");
        require(completed->future.process && completed->future.parameter_aware,"Supported parameter-aware prediction unavailable");
        ui.tab=0;calibration.scroll_target=0;submit();submit();
        auto gate=std::make_shared<ExperimentGate>();gate->target_phase="Simulating fixed-parameter reference";
        struct ReleaseDiagnosticGate{std::shared_ptr<ExperimentGate> gate;~ReleaseDiagnosticGate(){gate->release();}}release{gate};
        ui.observer=[gate](auto phase){gate->checkpoint(phase);};ui.replications="1000";ui.scroll_target=1;submit();submit();click(ui.run);gate->wait();
        calibration.scroll_target=0;submit();submit();require(workspace.calibration_execution().progress().active,"Diagnostic progress not active");capture("diagnostics-progress");
        click(ui.cancel);gate->release();wait_diagnostics();ui.observer={};
        require(workspace.calibration_execution().progress().cancelled && workspace.diagnostic_report()==completed,"Cancelled diagnostics replaced previous report");
        require(workspace.view().runtime().capture_snapshot()==baseline,"Diagnostics altered active playback");
        ui.tab=4;calibration.scroll_target=0;submit();submit();capture("diagnostics-summary");
        click(ui.source_navigation);require(calibration.tab==2 && workspace.calibration_report()->id==completed->request.source->id,"Source calibration navigation failed");submit();click(calibration.tabs[4]);ui.tab=4;calibration.scroll_target=0;submit();submit();
        ui.scroll_target=2;submit();submit();click(ui.save);require(workspace.diagnostics().size()==1,"Save diagnostic study action failed");
        ui.title="Reviewed warehouse demand diagnostics";ui.notes="Frozen fitted rates; original CSV verified.";ui.scroll_target=3;submit();submit();click(ui.rename);require(workspace.diagnostic_report()->title==ui.title,"Diagnostic rename/notes failed");
        ui.export_path=(destination.parent_path()/"diagnostic-report.json").string();ui.scroll_target=4;submit();submit();click(ui.export_json);require(std::filesystem::exists(ui.export_path),"Native diagnostic JSON export failed");
        ui.export_path=(destination.parent_path()/"diagnostic-report.csv").string();ui.scroll_target=4;submit();submit();click(ui.export_csv);require(std::filesystem::exists(ui.export_path),"Native diagnostic CSV export failed");
        const auto retained=workspace.diagnostic_report();
        const auto invalid_csv=destination.parent_path()/"wrong-original.csv";{std::ofstream file(invalid_csv);file<<"timestamp\n2026-02-02T08:00:00Z\n";}
        ui.tab=0;ui.replications="100";ui.csv_path=invalid_csv.string();ui.scroll_target=1;calibration.scroll_target=0;submit();submit();click(ui.run);wait_diagnostics();
        require(workspace.diagnostic_report()==retained && !workspace.calibration_execution().progress().error.empty(),"Invalid fingerprint was not safely rejected");
        for(const auto size:{std::pair{640,480},std::pair{1280,720},std::pair{1920,1080}}){window.resize(size.first,size.second);ui.tab=3;calibration.scroll_target=0;if(size.first==640)ui.scroll_target=6;submit();submit();capture(("diagnostics-resize-"+std::to_string(size.first)).c_str());}
        SDL_MinimizeWindow(window.native_handle());window.poll_events();std::this_thread::sleep_for(std::chrono::milliseconds{20});SDL_RestoreWindow(window.native_handle());window.resize(1600,1100);submit();submit();
        project.start_save(destination,true);wait_project();require(project_ui.error.empty() && !project.dirty(),"Diagnostic project Save failed");
        project.start_open(destination);wait_project();require(workspace.diagnostic_report() && workspace.diagnostic_report()->id==retained->id,"Native diagnostic project reopen failed");
        calibration.tab=4;ui.tab=4;calibration.scroll_target=0;submit();submit();capture("diagnostics-reopened");
        std::cout<<"Demand diagnostics native workflow completed: 28 steps, frozen-model CPU parity, cancellation/fingerprint rejection, Save/fresh-process Open.\n";
    }
}
