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
