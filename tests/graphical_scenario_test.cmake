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
