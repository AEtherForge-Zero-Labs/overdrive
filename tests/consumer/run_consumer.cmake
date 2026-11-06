# Configures, builds, and runs the minimal consumer as a separate CMake
# project (specs/003-packaging, R208). Run with cmake -P and:
#   -DMODE=vendored|duplicate
#   -DOVERDRIVE_SOURCE_DIR=<Overdrive's source tree>
#   -DCONSUMER_SOURCE_DIR=<this directory>
#   -DWORK_DIR=<outer build>/consumer
#   -DSETTINGS=<file of the outer build's settings, written by tests/CMakeLists.txt>
# Every failure names its step. Commands run as argument lists, never
# through a shell (Art. XIII).
cmake_minimum_required(VERSION 3.28)

foreach(required IN ITEMS MODE OVERDRIVE_SOURCE_DIR CONSUMER_SOURCE_DIR WORK_DIR SETTINGS)
    if(NOT DEFINED "${required}" OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "run_consumer: step arguments failed: ${required} is not set")
    endif()
endforeach()

# Defines consumer_generator and, for each forwarded setting NAME,
# forward_NAME, listed in forward_names.
set(forward_names "")
include("${SETTINGS}")
if(DEFINED forward_CMAKE_FIND_ROOT_PATH)
    message(FATAL_ERROR
        "run_consumer: step arguments failed: CMAKE_FIND_ROOT_PATH is set; "
        "forwarding it is not designed (R208)")
endif()
set(consumer_arguments -G "${consumer_generator}")
foreach(name IN LISTS forward_names)
    # A list value would split into several arguments here.
    string(FIND "${forward_${name}}" ";" semicolon)
    if(NOT semicolon EQUAL -1)
        message(FATAL_ERROR
            "run_consumer: step arguments failed: ${name} is a list; forwarding lists is not designed")
    endif()
    list(APPEND consumer_arguments "-D${name}=${forward_${name}}")
endforeach()
list(APPEND consumer_arguments "-DOVERDRIVE_SOURCE_DIR=${OVERDRIVE_SOURCE_DIR}")

# Configures the consumer for one route in a fresh directory; sets
# <prefix>_result and <prefix>_output.
function(consumer_configure route prefix)
    set(directory "${WORK_DIR}/${route}")
    file(REMOVE_RECURSE "${directory}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" ${consumer_arguments}
            "-DOVERDRIVE_CONSUMER_ROUTE=${route}"
            -S "${CONSUMER_SOURCE_DIR}" -B "${directory}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    set("${prefix}_result" "${result}" PARENT_SCOPE)
    set("${prefix}_output" "${output}" PARENT_SCOPE)
endfunction()

# CMake wraps long messages at spaces, so compare with runs of
# whitespace collapsed.
function(consumer_expect_refusal route expected)
    consumer_configure("${route}" refusal)
    string(REGEX REPLACE "[ \t\r\n]+" " " flat_output "${refusal_output}")
    string(REGEX REPLACE "[ \t\r\n]+" " " flat_expected "${expected}")
    string(FIND "${flat_output}" "${flat_expected}" found)
    if(refusal_result EQUAL 0 OR found EQUAL -1)
        message(FATAL_ERROR
            "run_consumer: step configure-${route} failed: expected a refusal containing\n"
            "  ${expected}\ngot exit ${refusal_result}:\n${refusal_output}")
    endif()
    message(STATUS "configure-${route}: refused as expected")
endfunction()

if(MODE STREQUAL "vendored")
    consumer_configure(vendored configure)
    if(NOT configure_result EQUAL 0)
        message(FATAL_ERROR "run_consumer: step configure failed (exit ${configure_result}):\n${configure_output}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${WORK_DIR}/vendored"
        RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_output)
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR "run_consumer: step build failed (exit ${build_result}):\n${build_output}")
    endif()
    file(READ "${WORK_DIR}/vendored/consumer_path.txt" program)
    set(emulator "")
    if(DEFINED forward_CMAKE_CROSSCOMPILING_EMULATOR)
        set(emulator ${forward_CMAKE_CROSSCOMPILING_EMULATOR})
    endif()
    execute_process(COMMAND ${emulator} "${program}"
        RESULT_VARIABLE run_result OUTPUT_VARIABLE run_output ERROR_VARIABLE run_output)
    if(NOT run_result EQUAL 0)
        message(FATAL_ERROR "run_consumer: step run failed (exit ${run_result}):\n${run_output}")
    endif()
    message(STATUS "vendored: configured, built, and ran ${program}")
elseif(MODE STREQUAL "duplicate")
    consumer_expect_refusal(duplicate
        "Overdrive was added twice: first from ${OVERDRIVE_SOURCE_DIR}, again from ${OVERDRIVE_SOURCE_DIR}")
    consumer_expect_refusal(collision
        "a target named overdrive already exists (defined in ${CONSUMER_SOURCE_DIR}); Overdrive needs that name")
else()
    message(FATAL_ERROR "run_consumer: step arguments failed: unknown MODE '${MODE}'")
endif()
