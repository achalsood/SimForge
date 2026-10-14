// Opt-in SDL/Vulkan walkthrough; assertions use actual evidence and runtime state.
if(smoke_frames && std::getenv("SIMFORGE_CALIBRATION_TEST_PATH")) {
    const auto destination=std::filesystem::path(std::getenv("SIMFORGE_CALIBRATION_TEST_PATH"));
    window.resize(1600,1000);submit();submit();click(dashboard.analysis_mode);submit();
    auto& calibration=workspace_ui.calibration;
    const auto wait_calibration=[&] {
        const auto deadline=Clock::now()+std::chrono::seconds{30};
        while(workspace.calibration_execution().progress().active){require(Clock::now()<deadline,"Native calibration timed out");submit();}
        submit();submit();
    };
    if(project_path) {
        require(workspace.calibrations().size()==2 && workspace.calibration_report() && workspace.runs().size()==1,"Calibration project evidence/run missing");
        require(!workspace.calibration_dataset(),"Raw observations were unnecessarily restored");
        calibration.open=true;calibration.tab=2;submit();capture("demand-restored");
        const auto seed=workspace.calibration_report()->conversions.back().seed;require(workspace.view().definition().simulation.random_seed==seed,"Restored calibrated seed differs");
    } else {
        const auto fixture_root=scenario_path.parent_path()/"demand-calibration";
        if(!calibration.open)click(calibration.toggle);submit();
        calibration.continuous=true;calibration.path=(fixture_root/"constant.csv").string();submit();capture("demand-import");
        click(calibration.import);wait_calibration();require(workspace.calibration_dataset() && workspace.calibration_dataset()->provenance.accepted==288,"Native CSV import failed");calibration.scroll_target=6;submit();submit();capture("demand-quality");
        click(calibration.tabs[1]);submit();capture("demand-model");click(calibration.fit);wait_calibration();
        require(workspace.calibration_report() && workspace.calibration_report()->training.observed==192 && workspace.calibration_report()->holdout.observed==96,"Native constant calibration counts differ");
        click(calibration.tabs[2]);calibration.scroll_target=4;submit();submit();capture("demand-holdout");
        click(calibration.tabs[3]);submit();click(calibration.save);submit();require(workspace.calibrations().size()==1,"Constant study not retained");
        click(calibration.tabs[0]);calibration.path=(fixture_root/"morning-surge.csv").string();submit();click(calibration.import);wait_calibration();
        click(calibration.tabs[1]);calibration.model=1;submit();click(calibration.fit);wait_calibration();
        require(workspace.calibration_report()->rates.size()==24 && workspace.calibration_report()->holdout.expected==240,"Native daily fit differs");
        click(calibration.tabs[2]);calibration.scroll_target=1;submit();submit();capture("demand-profile");
        for(const auto size:{std::pair{1280,720},std::pair{1920,1080}}){window.resize(size.first,size.second);submit();submit();capture(("demand-validation-"+std::to_string(size.first)).c_str());}
        window.resize(1600,1000);submit();click(calibration.tabs[3]);submit();click(calibration.save);submit();
        require(workspace.calibrations().size()==2,"Profile study not retained");
        calibration.seed="54321";calibration.utc_start="2026-01-05T08:00:00Z";submit();click(calibration.preview_button);submit();
        require(calibration.preview.has_value(),"Scenario preview failed");calibration.scroll_target=2;submit();submit();capture("demand-conversion");
        click(calibration.approve);submit();calibration.scroll_target=3;submit();submit();click(calibration.run);submit();require(workspace.runs().size()==1 && workspace.view().definition().simulation.random_seed==54321,"Calibrated playback not created");
        auto fitted=workspace.calibration_report();const auto simulated=workspace.view().definition();
        workspace.view().seek(scenario::total_ticks(simulated));warehouse::WarehouseSimulation reference(simulated);reference.run();
        require(workspace.view().runtime().metrics()==reference.metrics(),"Native metrics differ from authoritative seeded run");
        require(workspace.view().runtime().capture_snapshot()==reference.capture_snapshot(),"Calibrated playback snapshot differs");
        click(dashboard.simulation_mode);submit();capture("demand-simulation");
        click(dashboard.analysis_mode);submit();calibration.tab=0;calibration.scroll_target=0;calibration.path=(fixture_root/"malformed.csv").string();submit();submit();click(calibration.import);wait_calibration();
        require(!workspace.calibration_execution().progress().error.empty() && workspace.calibration_report()==fitted,"Malformed CSV replaced previous successful evidence");capture("demand-malformed-preserved");
        ExperimentGate gate;gate.target_phase="Parsing";
        auto request=analysis::demand::load_request(fixture_root/"constant.json");
        workspace.start_calibration(request,[&](auto phase){gate.checkpoint(phase);});gate.wait();submit();
        click(calibration.cancel);gate.release();wait_calibration();require(workspace.calibration_execution().progress().cancelled && workspace.calibration_report()==fitted,"Cancellation changed retained report");
        require(workspace.view().runtime().capture_snapshot()==reference.capture_snapshot(),"Cancellation changed active simulation");
        // Measure real frames while importing the maximum bounded dataset, without a delay gate.
        const auto benchmark_csv=destination.parent_path()/"demand-benchmark.csv";
        {std::ofstream rows(benchmark_csv,std::ios::binary);rows<<"timestamp\n";for(std::size_t i=0;i<analysis::demand::max_records;++i)rows<<"2026-01-05T00:00:00Z\n";require(bool(rows),"Native import benchmark fixture write failed");}
        const auto ui_began=calibration.render_us;const auto ui_frames=calibration.rendered_frames;
        workspace.import_calibration({benchmark_csv,{analysis::demand::parse_timestamp(calibration.start),analysis::demand::parse_timestamp(calibration.end),true},{}});
        std::vector<double> active_frames;
        while(workspace.calibration_execution().progress().active){const auto began=Clock::now();submit();active_frames.push_back(std::chrono::duration<double,std::milli>(Clock::now()-began).count());}
        submit();require(workspace.calibration_dataset()->provenance.accepted==analysis::demand::max_records && workspace.calibration_report()==fitted,"Maximum native import lost prior evidence");
        double sum=0,maximum=0;for(auto duration:active_frames){sum+=duration;maximum=std::max(maximum,duration);}
        const auto frame_count=calibration.rendered_frames-ui_frames;
        std::ofstream benchmark(destination.parent_path()/"native-calibration-benchmark.csv");
        benchmark<<"records,active_frames,mean_frame_ms,max_frame_ms,mean_calibration_ui_us,import_ms\n"<<analysis::demand::max_records<<','<<active_frames.size()<<',';
        if(!active_frames.empty())benchmark<<sum/active_frames.size();benchmark<<',';if(!active_frames.empty())benchmark<<maximum;benchmark<<',';
        if(frame_count)benchmark<<(calibration.render_us-ui_began)/frame_count;benchmark<<','<<workspace.calibration_dataset()->import_ms<<'\n';benchmark.close();
        std::filesystem::remove(benchmark_csv); // Only this explicitly owned generated fixture.
        std::cout<<"Native maximum import: "<<active_frames.size()<<" active frames; mean="<<(active_frames.empty()?0:sum/active_frames.size())<<" ms, max="<<maximum<<" ms.\n";
        window.resize(640,480);submit();capture("demand-compact");window.resize(1600,1000);submit();SDL_MinimizeWindow(window.native_handle());frame(std::chrono::nanoseconds{0});SDL_RestoreWindow(window.native_handle());submit();
        calibration.tab=2;submit();project_ui.test_path=destination;click(dashboard.file_menu);submit();click(project_ui.save_button);submit();wait_project();
        require(std::filesystem::exists(destination) && project_ui.error.empty(),"Native calibrated project Save failed");
        analysis::demand::save_export(destination.parent_path()/"calibration.json",analysis::demand::report_json(*fitted));
        analysis::demand::save_export(destination.parent_path()/"calibration.csv",analysis::demand::report_csv(*fitted));
        std::cout<<"Calibration graphics: import/data quality, constant/daily holdout, four charts, retained evidence, reviewed conversion, explicit-seed authoritative playback, failure/cancellation preservation, Save and resize verified. Calibration UI mean "<<calibration.render_us/std::max<std::size_t>(1,calibration.rendered_frames)<<" us.\n";
    }
}
