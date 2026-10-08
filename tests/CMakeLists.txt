add_executable(simforge_smoke_test
    smoke_test.cpp
)

target_link_libraries(simforge_smoke_test
    PRIVATE
        simforge_engine
)

add_test(
    NAME simforge_smoke_test
    COMMAND simforge_smoke_test
)