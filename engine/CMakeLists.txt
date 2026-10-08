find_package(SDL3 CONFIG REQUIRED)

add_library(simforge_engine STATIC
    src/core/clock.cpp
    src/core/engine.cpp
    src/simulation/simulation_clock.cpp
)

target_compile_features(simforge_engine PUBLIC cxx_std_23)

target_include_directories(simforge_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(simforge_platform STATIC src/platform/window.cpp)
target_include_directories(simforge_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(simforge_platform PUBLIC cxx_std_23)
target_link_libraries(simforge_platform PRIVATE SDL3::SDL3)

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
