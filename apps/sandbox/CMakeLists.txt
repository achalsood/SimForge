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
    target_sources(simforge_sandbox PRIVATE graphics.cpp)
    target_compile_definitions(simforge_sandbox PRIVATE SIMFORGE_HAS_RENDERER=1)
    target_link_libraries(simforge_sandbox PRIVATE simforge_renderer)
else()
    target_compile_definitions(simforge_sandbox PRIVATE SIMFORGE_HAS_RENDERER=0)
endif()

add_executable(simforge_headless main.cpp)
target_compile_definitions(simforge_headless PRIVATE SIMFORGE_HAS_RENDERER=0)
target_link_libraries(simforge_headless PRIVATE simforge_engine simforge_analysis)
target_compile_features(simforge_headless PRIVATE cxx_std_23)
