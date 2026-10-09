# Every invocation owns a unique fixture; concurrent validation never reuses paths.
string(RANDOM LENGTH 32 ALPHABET 0123456789abcdef run_id)
set(fixture "${BINARY_DIR}/graphical-fixtures/${run_id}")
if(EXISTS "${fixture}")
    message(FATAL_ERROR "Graphical fixture identity collision")
endif()
file(MAKE_DIRECTORY "${fixture}")
# All failure paths must reject before SDL/Vulkan initialization, without a GPU.
file(READ "${SOURCE_DIR}/examples/warehouse-baseline.json" baseline)
string(REPLACE "\"workers\": 10" "\"workers\": 257" oversized "${baseline}")
string(REPLACE "\"tick_rate\": 60" "\"tick_rate\": 0" invalid "${baseline}")
file(WRITE "${fixture}/warehouse-visual-oversized.json" "${oversized}")
file(WRITE "${fixture}/warehouse-visual-invalid.json" "${invalid}")
set(paths "${SOURCE_DIR}/examples/movement-baseline.json"
    "${fixture}/warehouse-visual-oversized.json" "${fixture}/warehouse-visual-invalid.json")
set(messages "requires the warehouse domain" "supports 0..256" "tick_rate")
foreach(index RANGE 0 2)
    list(GET paths ${index} path)
    list(GET messages ${index} message)
    execute_process(COMMAND "${SANDBOX}" --scenario "${path}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
    if(NOT result EQUAL 1 OR NOT error MATCHES "${message}" OR NOT output STREQUAL "")
        message(FATAL_ERROR "Expected scenario rejection before graphics: ${result}; ${output}; ${error}")
    endif()
endforeach()
file(REMOVE "${fixture}/warehouse-visual-oversized.json" "${fixture}/warehouse-visual-invalid.json")

# A new primary filename must not overwrite an existing diagnostic sibling.
set(capture "${fixture}/viewer-capture-collision.ppm")
set(sibling "${fixture}/viewer-capture-collision-overview.ppm")
file(REMOVE "${capture}")
file(WRITE "${sibling}" "preserve existing artifact")
execute_process(COMMAND "${SANDBOX}" --scenario "${SOURCE_DIR}/examples/warehouse-baseline.json"
    --live --graphics-smoke 180 --graphics-capture "${capture}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
file(READ "${sibling}" retained)
file(REMOVE "${sibling}")
if(NOT result EQUAL 1 OR NOT error MATCHES "capture output already exists" OR NOT output STREQUAL "" OR
    NOT retained STREQUAL "preserve existing artifact" OR EXISTS "${capture}")
    message(FATAL_ERROR "Expected capture rejection without graphics/overwriting: ${result}; ${output}; ${error}")
endif()

# Only the fresh, generated fixture is reclaimed; never remove the build tree.
cmake_path(ABSOLUTE_PATH fixture NORMALIZE OUTPUT_VARIABLE absolute_fixture)
set(boundary "${BINARY_DIR}/graphical-fixtures")
cmake_path(ABSOLUTE_PATH boundary NORMALIZE OUTPUT_VARIABLE absolute_boundary)
string(FIND "${absolute_fixture}" "${absolute_boundary}/" position)
if(NOT position EQUAL 0 OR IS_SYMLINK "${fixture}")
    message(FATAL_ERROR "Unsafe graphical fixture cleanup boundary")
endif()
file(REMOVE_RECURSE "${absolute_fixture}")
