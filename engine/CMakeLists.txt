find_package(SDL3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(simforge_engine STATIC
    src/core/clock.cpp
    src/core/engine.cpp
    src/simulation/simulation_clock.cpp
    src/simulation/movement_system.cpp
    src/world/world.cpp
    src/world/world_snapshot.cpp
    src/simulation/simulation_snapshot.cpp
    src/simulation/snapshot_serialization.cpp
    src/simulation/state_comparison.cpp
    src/scenario/json_io.cpp
    src/scenario/scenario.cpp
    src/simulation/input_recording.cpp
    src/scenario/recording_json.cpp
    src/warehouse/event_scheduler.cpp
    src/warehouse/resource_manager.cpp
    src/warehouse/warehouse_simulation.cpp
    src/warehouse/warehouse_state.cpp
    src/warehouse/warehouse_snapshot.cpp
    src/warehouse/warehouse_serialization.cpp
    src/warehouse/warehouse_input.cpp
    src/scenario/warehouse_recording_json.cpp
)

target_compile_features(simforge_engine PUBLIC cxx_std_23)
target_link_libraries(simforge_engine PRIVATE nlohmann_json::nlohmann_json)

target_include_directories(simforge_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(simforge_analysis STATIC src/analysis/comparison.cpp src/analysis/experiment_json.cpp src/analysis/report_export.cpp)
target_link_libraries(simforge_analysis PUBLIC simforge_engine PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(simforge_analysis PUBLIC cxx_std_23)
if(MSVC)
    target_compile_options(simforge_analysis PRIVATE /W4 /permissive-)
else()
    target_compile_options(simforge_analysis PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(simforge_platform STATIC src/platform/window.cpp)
target_include_directories(simforge_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(simforge_platform PUBLIC cxx_std_23)
target_link_libraries(simforge_platform PRIVATE SDL3::SDL3)

if(SIMFORGE_BUILD_RENDERER)
    find_package(Vulkan 1.4.335 REQUIRED)
    target_sources(simforge_platform PRIVATE src/platform/vulkan_window.cpp)
    # The opt-in Vulkan platform header exposes Vulkan types, but no loader link.
    target_link_libraries(simforge_platform PUBLIC Vulkan::Headers)
    add_library(simforge_render_policy STATIC src/renderer/render_policy.cpp)
    target_include_directories(simforge_render_policy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(simforge_render_policy PUBLIC Vulkan::Headers)
    target_compile_features(simforge_render_policy PUBLIC cxx_std_23)
    add_library(simforge_renderer STATIC src/renderer/renderer.cpp src/renderer/vulkan_context.cpp
        src/renderer/vulkan_device.cpp src/renderer/vulkan_swapchain.cpp)
    target_include_directories(simforge_renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(simforge_renderer PUBLIC cxx_std_23)
    target_link_libraries(simforge_renderer PRIVATE simforge_platform simforge_render_policy Vulkan::Vulkan)
    if(MSVC)
        target_compile_options(simforge_renderer PRIVATE /W4 /permissive-)
        target_compile_options(simforge_render_policy PRIVATE /W4 /permissive-)
    else()
        target_compile_options(simforge_renderer PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(simforge_render_policy PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(MSVC)
    target_compile_options(simforge_engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(simforge_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MSVC)
    target_compile_options(simforge_platform PRIVATE /W4 /permissive-)
else()
    target_compile_options(simforge_platform PRIVATE -Wall -Wextra -Wpedantic)
endif()
