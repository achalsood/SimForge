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

add_executable(simforge_world_test world_test.cpp)
target_link_libraries(simforge_world_test PRIVATE simforge_engine)
add_test(NAME simforge_world_test COMMAND simforge_world_test)
add_test(NAME simforge_headless_parameters COMMAND simforge_sandbox --headless --entities 10000 --ticks 120)
set_tests_properties(simforge_headless_parameters PROPERTIES PASS_REGULAR_EXPRESSION "Entities: 10000.*Ticks: 120.*simulated seconds: 2")
add_test(NAME simforge_headless_empty COMMAND simforge_sandbox --headless --entities 0 --ticks 0)
set_tests_properties(simforge_headless_empty PROPERTIES PASS_REGULAR_EXPRESSION "Entities: 0.*Ticks: 0.*simulated seconds: 0")

function(add_invalid_sandbox_test name)
    add_test(NAME ${name} COMMAND simforge_sandbox ${ARGN})
    set_tests_properties(${name} PROPERTIES WILL_FAIL TRUE)
endfunction()
add_invalid_sandbox_test(simforge_cli_negative --headless --entities -1)
add_invalid_sandbox_test(simforge_cli_malformed --headless --ticks 12abc)
add_invalid_sandbox_test(simforge_cli_overflow --headless --ticks 18446744073709551616)
add_invalid_sandbox_test(simforge_cli_entities_overflow --headless --entities 4294967296)
add_invalid_sandbox_test(simforge_cli_missing --headless --ticks)
add_invalid_sandbox_test(simforge_cli_duplicate --headless --entities 1 --entities 2)
add_invalid_sandbox_test(simforge_cli_unknown --unknown)
add_invalid_sandbox_test(simforge_cli_window_options --ticks 1)

add_executable(simforge_snapshot_test snapshot_test.cpp)
target_link_libraries(simforge_snapshot_test PRIVATE simforge_engine)
foreach(case capture validation clock serialization replay randomized stress)
    add_test(NAME simforge_snapshot_${case} COMMAND simforge_snapshot_test ${case})
endforeach()
