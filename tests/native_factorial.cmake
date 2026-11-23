file(MAKE_DIRECTORY "${WORK}")

function(run)
    execute_process(COMMAND ${ARGV}
        WORKING_DIRECTORY "${WORK}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "${ARGV} failed (${status})\n${stderr}")
    endif()
    set(output "${stdout}" PARENT_SCOPE)
endfunction()

run("${SL}" "${SOURCE}" -o "factorial program")
run("${WORK}/factorial program")
if(NOT output STREQUAL "120\n1\n6\n")
    message(FATAL_ERROR "Unexpected factorial output: ${output}")
endif()
