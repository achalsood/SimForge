find_package(SDL3 CONFIG REQUIRED)

add_library(simforge_engine STATIC
    src/core/clock.cpp
    src/core/engine.cpp
    src/platform/window.cpp
)

target_compile_features(simforge_engine PUBLIC cxx_std_23)

target_include_directories(simforge_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(simforge_engine
    PUBLIC
        SDL3::SDL3
)

if(MSVC)
    target_compile_options(simforge_engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(simforge_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()