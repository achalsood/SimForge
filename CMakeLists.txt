cmake_minimum_required(VERSION 3.25)

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

add_subdirectory(engine)
add_subdirectory(apps/sandbox)

if(SIMFORGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()