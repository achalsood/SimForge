# All failure paths must reject before SDL/Vulkan initialization, without a GPU.
file(READ "${SOURCE_DIR}/examples/warehouse-baseline.json" baseline)
string(REPLACE "\"workers\": 10" "\"workers\": 257" oversized "${baseline}")
string(REPLACE "\"tick_rate\": 60" "\"tick_rate\": 0" invalid "${baseline}")
file(WRITE "${BINARY_DIR}/warehouse-visual-oversized.json" "${oversized}")
file(WRITE "${BINARY_DIR}/warehouse-visual-invalid.json" "${invalid}")
set(paths "${SOURCE_DIR}/examples/movement-baseline.json"
    "${BINARY_DIR}/warehouse-visual-oversized.json" "${BINARY_DIR}/warehouse-visual-invalid.json")
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
file(REMOVE "${BINARY_DIR}/warehouse-visual-oversized.json" "${BINARY_DIR}/warehouse-visual-invalid.json")

# A new primary filename must not overwrite an existing diagnostic sibling.
set(capture "${BINARY_DIR}/viewer-capture-collision.ppm")
set(sibling "${BINARY_DIR}/viewer-capture-collision-overview.ppm")
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
