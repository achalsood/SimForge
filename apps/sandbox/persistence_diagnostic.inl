    if(smoke_frames && std::getenv("SIMFORGE_PERSISTENCE_TEST_PATH")) {
        if(project_path) {
            require(workspace.studies().size()==20 && workspace.report()->interaction.has_value(),"Fresh process lost near-limit history");
            require(workspace.execution().progress().state==workspace::ExecutionState::Idle,"Opening reran a study");
            submit();capture("persistence-reopened");
        }else {
            scenario::ScenarioDefinition definition{1,"Persistence workload",{10,120,42},scenario::WarehouseConfig{12,2,1,0,0}};
            auto& workload=std::get<scenario::WarehouseConfig>(definition.domain).workload;workload.arrivals=scenario::ArrivalMode::Uniform;workload.arrival_min_seconds=1;workload.arrival_max_seconds=3;workload.processing=scenario::ProcessingMode::Uniform;workload.processing_min_seconds=2;workload.processing_max_seconds=12;
            workspace.load(definition,true);workspace.view().reset();
            analysis::InteractionRequest grid;grid.source=definition;for(unsigned i=1;i<=10;++i){grid.x_values.push_back(i);grid.y_values.push_back(i);}grid.multi_seed=true;grid.plan.count=100;grid.plan.base_seed=1001;
            workspace.start_interaction(grid);while(workspace.execution().progress().active())submit();require(workspace.poll_experiment() || workspace.report(),"Native persistence grid failed");
            const auto evidence=workspace.report();while(workspace.studies().size()<20)workspace.retain_study(*evidence);
            const auto saved_snapshot=workspace.view().runtime().capture_snapshot();const auto selected=workspace.selected_study_id();const auto last_evidence=workspace.studies().back().evidence;
            project_ui.test_path=std::filesystem::u8path(std::getenv("SIMFORGE_PERSISTENCE_TEST_PATH"));
            project.rename("Persistent large history");submit();click(dashboard.file_menu);submit();click(project_ui.save_button);submit();
            require(project.busy(),"Near-limit Save completed without exercising background frames");capture("persistence-save-progress");
            std::vector<double> saving_frames,opening_frames;
            const auto measure=[&](std::vector<double>& samples){while(project.busy()){const auto began=Clock::now();submit();samples.push_back(std::chrono::duration<double,std::milli>(Clock::now()-began).count());}};
            workspace.edit(workspace::Field::Workers,"9");workspace.view().seek(11);measure(saving_frames);
            require(project_ui.error.empty() && project.dirty() && std::filesystem::exists(project_ui.test_path),"Save marked later edits as saved");
            const auto file_size=std::filesystem::file_size(project_ui.test_path);
            click(dashboard.file_menu);submit();click(project_ui.save_button);submit();require(project.busy(),"Cancellation save not active");capture("persistence-cancel-save");click(project_ui.operation_cancel);submit();wait_project();
            require(!project_ui.error.empty() && std::filesystem::file_size(project_ui.test_path)==file_size && project.dirty(),"Cancel Save lost primary or dirty state");
            click(dashboard.file_menu);submit();click(project_ui.open_button);submit();click(project_ui.discard);submit();require(project.opening(),"Native Open not active");capture("persistence-open-progress");
            const auto camera=workspace.view().orbit_camera().pose();
            const auto viewport=workspace.view().viewport();const auto pointer=viewport.origin+viewport.size*glm::vec2{0.55f,0.5f};motion(id,pointer);submit();SDL_Event zoom{};zoom.type=SDL_EVENT_MOUSE_WHEEL;zoom.wheel.windowID=id;zoom.wheel.mouse_x=pointer.x;zoom.wheel.mouse_y=pointer.y;zoom.wheel.y=1;push(zoom);submit();require(workspace.view().orbit_camera().pose()!=camera,"Camera blocked during Open");
            window.resize(640,480);submit();window.resize(1600,900);submit();SDL_MinimizeWindow(window.native_handle());frame(std::chrono::nanoseconds{0});SDL_RestoreWindow(window.native_handle());submit();
            click(project_ui.operation_cancel);submit();wait_project();require(workspace.draft().values[2]=="9" && workspace.view().observation().tick==11 && workspace.studies().back().evidence==last_evidence,"Cancel Open replaced old workspace");
            click(dashboard.file_menu);submit();click(project_ui.open_button);submit();click(project_ui.discard);submit();measure(opening_frames);
            require(project_ui.error.empty(),"Open returned an error");require(workspace.studies().size()==20,"Open lost history count");require(workspace.selected_study_id()==selected,"Open changed selected study");require(workspace.view().runtime().capture_snapshot()==saved_snapshot,"Open changed saved runtime snapshot");require(!project.dirty(),"Open immediately marked restored project dirty");capture("persistence-restored");
            const auto original=project_ui.test_path;const auto corrupt=original.parent_path()/"corrupt.simforge";{std::ofstream out(corrupt);out<<"{\"payload\":";}
            project_ui.test_path=corrupt;click(dashboard.file_menu);submit();click(project_ui.open_button);submit();wait_project();require(!project_ui.error.empty() && workspace.studies().size()==20 && workspace.view().runtime().capture_snapshot()==saved_snapshot,"Corrupt native Open changed project");capture("persistence-corrupt-preserved");project_ui.test_path=original;
            // Seeded result playback is reconstructed only when explicitly requested.
            const auto cell=workspace.report()->interaction->cells[1].id;workspace.simulate_interaction(workspace.report()->request_id,cell,1001);submit();workspace.view().seek(50);submit();require(workspace.view().observation().tick==50,"Seeded playback after Open failed");workspace.remove_run(workspace.active_run_id());workspace.select_run(0);submit();
            const auto report=[](const char* name,std::vector<double> values){require(!values.empty(),"No background rendering samples");std::sort(values.begin(),values.end());double total=0;for(auto value:values)total+=value;std::cout<<"Persistence "<<name<<" ungated rendered frames: "<<values.size()<<" | mean "<<total/values.size()<<" ms | p95 "<<values[std::min(values.size()-1,values.size()*95/100)]<<" ms | maximum "<<values.back()<<" ms\n";};report("save",saving_frames);report("open",opening_frames);
            std::cout<<"Persistence graphics: near-limit Save/Open, later edits dirty, both Cancel buttons, camera, resize/minimize/restore, corrupt Open preservation and seeded restored playback verified.\n";
        }
    }
