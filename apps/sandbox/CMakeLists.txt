find_package(nlohmann_json CONFIG REQUIRED)
add_executable(simforge_sandbox
    main.cpp
)

target_link_libraries(simforge_sandbox
    PRIVATE
        simforge_engine
        simforge_analysis
        nlohmann_json::nlohmann_json
)

target_compile_features(simforge_sandbox PRIVATE cxx_std_23)
add_library(simforge_capture STATIC capture.cpp)
target_include_directories(simforge_capture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(simforge_capture PUBLIC simforge_render_scene)
if(MSVC)
    target_compile_options(simforge_capture PRIVATE /W4 /permissive-)
else()
    target_compile_options(simforge_capture PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
    target_link_libraries(simforge_capture PRIVATE windowscodecs ole32)
endif()
if(SIMFORGE_BUILD_RENDERER)
    find_package(imgui CONFIG REQUIRED)
    target_sources(simforge_sandbox PRIVATE graphics.cpp viewer.cpp dashboard.cpp workspace_panel.cpp statistical_panel.cpp sensitivity_panel.cpp interaction_panel.cpp project_panel.cpp scenario_language_panel.cpp save_dialog.cpp capture_failure.cpp)
    add_custom_command(TARGET simforge_sandbox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/examples" "$<TARGET_FILE_DIR:simforge_sandbox>/examples"
        VERBATIM)
    target_compile_definitions(simforge_sandbox PRIVATE SIMFORGE_HAS_RENDERER=1)
    target_link_libraries(simforge_sandbox PRIVATE simforge_renderer simforge_workspace simforge_capture imgui::imgui SDL3::SDL3)
    if(WIN32)
        target_link_libraries(simforge_sandbox PRIVATE comdlg32)
    endif()
else()
    target_compile_definitions(simforge_sandbox PRIVATE SIMFORGE_HAS_RENDERER=0)
endif()

add_executable(simforge_headless main.cpp)
target_compile_definitions(simforge_headless PRIVATE SIMFORGE_HAS_RENDERER=0)
target_link_libraries(simforge_headless PRIVATE simforge_engine simforge_analysis nlohmann_json::nlohmann_json)
target_compile_features(simforge_headless PRIVATE cxx_std_23)
