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

find_package(glm CONFIG REQUIRED)
add_library(simforge_render_scene STATIC src/renderer/camera.cpp src/renderer/geometry.cpp
    src/renderer/industrial_geometry.cpp src/renderer/render_frame_data.cpp)
target_include_directories(simforge_render_scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(simforge_render_scene PUBLIC glm::glm-header-only)
target_compile_features(simforge_render_scene PUBLIC cxx_std_23)
add_library(simforge_visualization STATIC src/visualization/warehouse_layout.cpp)
target_link_libraries(simforge_visualization PUBLIC simforge_engine simforge_render_scene)
target_compile_features(simforge_visualization PUBLIC cxx_std_23)
if(MSVC)
    target_compile_options(simforge_render_scene PRIVATE /W4 /permissive-)
    target_compile_options(simforge_visualization PRIVATE /W4 /permissive-)
else()
    target_compile_options(simforge_render_scene PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(simforge_visualization PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SIMFORGE_BUILD_RENDERER)
    find_package(Vulkan 1.4.335 REQUIRED COMPONENTS glslc)
    set(shader_headers)
    foreach(stage vert frag)
        set(source "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene.${stage}")
        set(binary "${CMAKE_CURRENT_BINARY_DIR}/generated/scene.${stage}.spv")
        set(header "${CMAKE_CURRENT_BINARY_DIR}/generated/scene_${stage}.hpp")
        add_custom_command(OUTPUT "${header}" "${binary}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
            COMMAND Vulkan::glslc --target-env=vulkan1.1 -O "${source}" -o "${binary}"
            COMMAND ${CMAKE_COMMAND} "-DINPUT=${binary}" "-DOUTPUT=${header}" "-DSYMBOL=scene_${stage}"
                -P "${PROJECT_SOURCE_DIR}/cmake/EmbedSpirv.cmake"
            DEPENDS "${source}" "${PROJECT_SOURCE_DIR}/cmake/EmbedSpirv.cmake" VERBATIM)
        list(APPEND shader_headers "${header}")
    endforeach()
    target_sources(simforge_platform PRIVATE src/platform/vulkan_window.cpp)
    # The opt-in Vulkan platform header exposes Vulkan types, but no loader link.
    target_link_libraries(simforge_platform PUBLIC Vulkan::Headers)
    add_library(simforge_render_policy STATIC src/renderer/render_policy.cpp)
    target_include_directories(simforge_render_policy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(simforge_render_policy PUBLIC Vulkan::Headers)
    target_compile_features(simforge_render_policy PUBLIC cxx_std_23)
    add_library(simforge_renderer STATIC src/renderer/renderer.cpp src/renderer/vulkan_context.cpp
        src/renderer/vulkan_device.cpp src/renderer/vulkan_swapchain.cpp src/renderer/vulkan_buffer.cpp
        src/renderer/vulkan_scene.cpp ${shader_headers})
    target_include_directories(simforge_renderer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_include_directories(simforge_renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(simforge_renderer PUBLIC cxx_std_23)
    target_link_libraries(simforge_renderer PUBLIC simforge_render_scene PRIVATE simforge_platform simforge_render_policy Vulkan::Vulkan)
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
