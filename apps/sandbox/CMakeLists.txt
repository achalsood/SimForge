add_executable(simforge_sandbox
    main.cpp
)

target_link_libraries(simforge_sandbox
    PRIVATE
        simforge_engine
        simforge_analysis
)

target_compile_features(simforge_sandbox PRIVATE cxx_std_23)
if(SIMFORGE_BUILD_RENDERER)
    find_package(imgui CONFIG REQUIRED)
    target_sources(simforge_sandbox PRIVATE graphics.cpp viewer.cpp dashboard.cpp workspace_panel.cpp save_dialog.cpp)
    add_custom_command(TARGET simforge_sandbox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/examples" "$<TARGET_FILE_DIR:simforge_sandbox>/examples"
        VERBATIM)
    target_compile_definitions(simforge_sandbox PRIVATE SIMFORGE_HAS_RENDERER=1)
    target_link_libraries(simforge_sandbox PRIVATE simforge_renderer simforge_workspace imgui::imgui SDL3::SDL3)
    if(WIN32)
        target_link_libraries(simforge_sandbox PRIVATE comdlg32)
    endif()
else()
    target_compile_definitions(simforge_sandbox PRIVATE SIMFORGE_HAS_RENDERER=0)
endif()

add_executable(simforge_headless main.cpp)
target_compile_definitions(simforge_headless PRIVATE SIMFORGE_HAS_RENDERER=0)
target_link_libraries(simforge_headless PRIVATE simforge_engine simforge_analysis)
target_compile_features(simforge_headless PRIVATE cxx_std_23)
