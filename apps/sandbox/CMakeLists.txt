add_executable(simforge_sandbox
    main.cpp
)

target_link_libraries(simforge_sandbox
    PRIVATE
        simforge_engine
)

target_compile_features(simforge_sandbox PRIVATE cxx_std_23)