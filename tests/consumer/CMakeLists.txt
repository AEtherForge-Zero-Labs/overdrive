# The minimal consumer (specs/003-packaging, R209). run_consumer.cmake
# configures it as a separate project; it is never added to Overdrive's
# own build. Each check stops configuration with a message naming it.
cmake_minimum_required(VERSION 3.28)
project(consumer LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
# The first consumer's own flags, set before Overdrive is added (SC-005).
add_compile_options(-Wall -Wextra -Wpedantic -Werror)
# Many consumers include this first; its CMAKE_INSTALL_* entries then
# exist before the cache snapshot below.
include(GNUInstallDirs)

set(OVERDRIVE_CONSUMER_ROUTE "vendored" CACHE STRING
    "How the consumer gets Overdrive: vendored, duplicate, collision, or installed")
set_property(CACHE OVERDRIVE_CONSUMER_ROUTE
    PROPERTY STRINGS vendored duplicate collision installed)
set(OVERDRIVE_CONSUMER_VERSION "0.1" CACHE STRING
    "The version the installed route requests")
set(OVERDRIVE_SOURCE_DIR "" CACHE PATH "Overdrive's source tree (all routes but installed)")
if(NOT OVERDRIVE_CONSUMER_ROUTE STREQUAL "installed" AND NOT IS_DIRECTORY "${OVERDRIVE_SOURCE_DIR}")
    message(FATAL_ERROR "consumer: OVERDRIVE_SOURCE_DIR is not a directory: '${OVERDRIVE_SOURCE_DIR}'")
endif()

get_cmake_property(consumer_cache_before CACHE_VARIABLES)

if(OVERDRIVE_CONSUMER_ROUTE STREQUAL "installed")
    find_package(overdrive ${OVERDRIVE_CONSUMER_VERSION} REQUIRED)
elseif(OVERDRIVE_CONSUMER_ROUTE STREQUAL "vendored")
    add_subdirectory("${OVERDRIVE_SOURCE_DIR}" overdrive)
elseif(OVERDRIVE_CONSUMER_ROUTE STREQUAL "duplicate")
    # Must stop at the second copy (R203).
    add_subdirectory("${OVERDRIVE_SOURCE_DIR}" overdrive_first)
    add_subdirectory("${OVERDRIVE_SOURCE_DIR}" overdrive_second)
    message(FATAL_ERROR "consumer: the second copy of Overdrive was accepted")
elseif(OVERDRIVE_CONSUMER_ROUTE STREQUAL "collision")
    # Must stop: the name overdrive is taken (R203).
    add_library(overdrive INTERFACE)
    add_subdirectory("${OVERDRIVE_SOURCE_DIR}" overdrive)
    message(FATAL_ERROR "consumer: Overdrive was added beside a target named overdrive")
else()
    message(FATAL_ERROR "consumer: unknown OVERDRIVE_CONSUMER_ROUTE '${OVERDRIVE_CONSUMER_ROUTE}'")
endif()

add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE overdrive::overdrive)
# Tells run_consumer.cmake which file to run.
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/consumer_path.txt"
    CONTENT "$<TARGET_FILE:consumer>")
# The consumer installs its own program; a vendored Overdrive must add
# nothing to that install (FR-016).
install(TARGETS consumer)

# FR-003, channel 1: what linking adds. C23, and OVERDRIVE_SHARED exactly
# when the library is shared; no compile options, no other definitions.
get_target_property(consumer_library overdrive::overdrive ALIASED_TARGET)
if(NOT consumer_library)
    set(consumer_library overdrive::overdrive)
endif()
get_target_property(consumer_options "${consumer_library}" INTERFACE_COMPILE_OPTIONS)
if(NOT consumer_options STREQUAL "consumer_options-NOTFOUND")
    message(FATAL_ERROR
        "consumer: check usage-options failed: INTERFACE_COMPILE_OPTIONS is '${consumer_options}'")
endif()
get_target_property(consumer_type "${consumer_library}" TYPE)
get_target_property(consumer_definitions "${consumer_library}" INTERFACE_COMPILE_DEFINITIONS)
if(consumer_definitions STREQUAL "consumer_definitions-NOTFOUND")
    set(consumer_definitions "")
endif()
if(consumer_type STREQUAL "SHARED_LIBRARY")
    set(consumer_expected_definitions "OVERDRIVE_SHARED")
else()
    set(consumer_expected_definitions "")
endif()
if(NOT consumer_definitions STREQUAL consumer_expected_definitions)
    message(FATAL_ERROR
        "consumer: check usage-definitions failed: a ${consumer_type} has "
        "INTERFACE_COMPILE_DEFINITIONS '${consumer_definitions}', "
        "expected '${consumer_expected_definitions}'")
endif()

# FR-003, channel 2: cache entries Overdrive added.
get_cmake_property(consumer_cache_after CACHE_VARIABLES)
list(REMOVE_ITEM consumer_cache_after ${consumer_cache_before})
foreach(name IN LISTS consumer_cache_after)
    if(NOT name MATCHES "^(OVERDRIVE_|overdrive_)" AND NOT name STREQUAL "BUILD_SHARED_LIBS")
        message(FATAL_ERROR "consumer: check cache-diff failed: Overdrive added the cache entry ${name}")
    endif()
endforeach()

# SC-003: a vendored Overdrive brings exactly its library and its
# generated tables. Anything else is a test, example, or benchmark
# leaking in.
function(consumer_collect_targets directory out)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        consumer_collect_targets("${subdirectory}" nested)
        list(APPEND targets ${nested})
    endforeach()
    set("${out}" "${targets}" PARENT_SCOPE)
endfunction()
if(OVERDRIVE_CONSUMER_ROUTE STREQUAL "vendored")
    consumer_collect_targets("${CMAKE_CURRENT_BINARY_DIR}/overdrive" consumer_targets)
    get_property(consumer_subdirectories DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/overdrive"
        PROPERTY SUBDIRECTORIES)
    list(SORT consumer_targets)
    if(NOT consumer_targets STREQUAL "ovd_unicode_tables;overdrive")
        message(FATAL_ERROR
            "consumer: check target-allowlist failed: Overdrive defines '${consumer_targets}' "
            "(subdirectories: '${consumer_subdirectories}'), expected 'ovd_unicode_tables;overdrive'")
    endif()
endif()

# A shared Overdrive's DLL must sit beside the program on Windows. The
# list is empty for a static build: copy -t (the form the CMake manual
# shows for TARGET_RUNTIME_DLLS) then copies nothing, where
# copy_if_different with no file fails.
if(WIN32)
    add_custom_command(TARGET consumer POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy -t
            "$<TARGET_FILE_DIR:consumer>" "$<TARGET_RUNTIME_DLLS:consumer>"
        COMMAND_EXPAND_LISTS)
endif()
