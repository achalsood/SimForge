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
add_executable(simforge_simulation_clock_test simulation_clock_test.cpp)
target_link_libraries(simforge_simulation_clock_test PRIVATE simforge_engine)
add_test(NAME simforge_simulation_clock_test COMMAND simforge_simulation_clock_test)
add_test(NAME simforge_headless_sandbox COMMAND simforge_sandbox --headless)
set_tests_properties(simforge_headless_sandbox PROPERTIES
    PASS_REGULAR_EXPRESSION "Ticks: 600.*simulated seconds: 10")
