cmake_minimum_required(VERSION 3.25)

option(SIMFORGE_BUILD_RENDERER "Build the SDL/Vulkan graphics sandbox and renderer" ON)
if(SIMFORGE_BUILD_RENDERER)
    list(APPEND VCPKG_MANIFEST_FEATURES viewer)
else()
    list(REMOVE_ITEM VCPKG_MANIFEST_FEATURES viewer)
endif()

project(
    SimForge
    VERSION 0.1.0
    DESCRIPTION "AI-native simulation platform powered by a custom high-performance C++ engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" OUTPUT_VARIABLE SIMFORGE_SOURCE_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" OUTPUT_VARIABLE source_changes
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
set(SIMFORGE_SOURCE_DIRTY false)
if(source_changes)
    set(SIMFORGE_SOURCE_DIRTY true)
endif()
configure_file(cmake/version.hpp.in generated/simforge_version.hpp @ONLY)
file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/release-build.json" CONTENT
    "{\"version\":\"${PROJECT_VERSION}\",\"sourceCommit\":\"${SIMFORGE_SOURCE_COMMIT}\",\"sourceDirty\":${SIMFORGE_SOURCE_DIRTY},\"configuration\":\"$<CONFIG>\",\"renderer\":$<IF:$<BOOL:${SIMFORGE_BUILD_RENDERER}>,true,false>,\"compiler\":\"${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\",\"runtime\":\"dynamic MSVC /MD (x64-windows)\"}\n")

option(SIMFORGE_BUILD_TESTS "Build SimForge tests" ON)
option(SIMFORGE_BUILD_BENCHMARKS "Build SimForge benchmarks" ON)
option(SIMFORGE_GRAPHICS_TESTS "Enable display/GPU integration smoke tests" OFF)

add_subdirectory(engine)
add_subdirectory(apps/sandbox)

if(SIMFORGE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(SIMFORGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
