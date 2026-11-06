# Configures, builds, and runs the minimal consumer as a separate CMake
# project (specs/003-packaging, R208). Run with cmake -P and:
#   -DMODE=vendored|duplicate|installed
#   -DOVERDRIVE_SOURCE_DIR=<Overdrive's source tree>
#   -DOVERDRIVE_BUILD_DIR=<the outer build tree>
#   -DCONSUMER_SOURCE_DIR=<this directory>
#   -DWORK_DIR=<outer build>/consumer
#   -DSETTINGS=<file of the outer build's settings, written by tests/CMakeLists.txt>
# Every failure names its step. Commands run as argument lists, never
# through a shell (Art. XIII).
cmake_minimum_required(VERSION 3.28)

foreach(required IN ITEMS MODE OVERDRIVE_SOURCE_DIR OVERDRIVE_BUILD_DIR CONSUMER_SOURCE_DIR
        WORK_DIR SETTINGS)
    if(NOT DEFINED "${required}" OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "run_consumer: step arguments failed: ${required} is not set")
    endif()
endforeach()

# Defines consumer_generator, consumer_executable_suffix, the install
# layout (consumer_install_*), the public header names
# (consumer_public_headers, the list the header file set is built from),
# and library file names (consumer_library_*) when Overdrive's install
# rules are on, and, for each forwarded setting
# NAME, forward_NAME, listed in forward_names.
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
set(emulator "")
if(DEFINED forward_CMAKE_CROSSCOMPILING_EMULATOR)
    set(emulator ${forward_CMAKE_CROSSCOMPILING_EMULATOR})
endif()

# Configures the consumer for one route in a fresh directory under
# WORK_DIR; extra arguments are passed on. Sets <prefix>_result and
# <prefix>_output.
function(consumer_configure route directory prefix)
    file(REMOVE_RECURSE "${WORK_DIR}/${directory}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" ${consumer_arguments} ${ARGN}
            "-DOVERDRIVE_CONSUMER_ROUTE=${route}"
            -S "${CONSUMER_SOURCE_DIR}" -B "${WORK_DIR}/${directory}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    set("${prefix}_result" "${result}" PARENT_SCOPE)
    set("${prefix}_output" "${output}" PARENT_SCOPE)
endfunction()

# Configures, builds, and runs the consumer; each failure names its step.
function(consumer_configure_build_run route directory)
    consumer_configure("${route}" "${directory}" configure ${ARGN})
    if(NOT configure_result EQUAL 0)
        message(FATAL_ERROR
            "run_consumer: step configure failed (exit ${configure_result}):\n${configure_output}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${WORK_DIR}/${directory}"
        RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_output)
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR "run_consumer: step build failed (exit ${build_result}):\n${build_output}")
    endif()
    file(READ "${WORK_DIR}/${directory}/consumer_path.txt" program)
    execute_process(COMMAND ${emulator} "${program}"
        RESULT_VARIABLE run_result OUTPUT_VARIABLE run_output ERROR_VARIABLE run_output)
    if(NOT run_result EQUAL 0)
        message(FATAL_ERROR "run_consumer: step run failed (exit ${run_result}):\n${run_output}")
    endif()
    message(STATUS "${route}: configured, built, and ran ${program}")
endfunction()

# Requires a failed configure whose output contains every EXPECT text;
# ARGS are passed on. CMake wraps long messages at spaces, so compare
# with runs of whitespace collapsed.
function(consumer_expect_refusal route directory)
    cmake_parse_arguments(PARSE_ARGV 2 refusal "" "" "EXPECT;ARGS")
    consumer_configure("${route}" "${directory}" refusal ${refusal_ARGS})
    string(REGEX REPLACE "[ \t\r\n]+" " " flat_output "${refusal_output}")
    foreach(text IN LISTS refusal_EXPECT)
        string(REGEX REPLACE "[ \t\r\n]+" " " flat_text "${text}")
        string(FIND "${flat_output}" "${flat_text}" found)
        if(refusal_result EQUAL 0 OR found EQUAL -1)
            message(FATAL_ERROR
                "run_consumer: step configure-${directory} failed: expected a refusal containing\n"
                "  ${text}\ngot exit ${refusal_result}:\n${refusal_output}")
        endif()
    endforeach()
    message(STATUS "configure-${directory}: refused as expected")
endfunction()

# Installs a build tree to a fresh prefix and lists the files there,
# relative to it, in <out>.
function(consumer_install build prefix out)
    file(REMOVE_RECURSE "${prefix}")
    execute_process(COMMAND "${CMAKE_COMMAND}" --install "${build}" --prefix "${prefix}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "run_consumer: step install failed (exit ${result}):\n${output}")
    endif()
    file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE "${prefix}" "${prefix}/*")
    list(SORT files)
    set("${out}" "${files}" PARENT_SCOPE)
endfunction()

if(MODE STREQUAL "vendored")
    consumer_configure_build_run(vendored vendored
        "-DOVERDRIVE_SOURCE_DIR=${OVERDRIVE_SOURCE_DIR}")
    # FR-016: the consumer's install holds its own program only.
    consumer_install("${WORK_DIR}/vendored" "${WORK_DIR}/vendored-install" installed)
    set(expected "bin/consumer${consumer_executable_suffix}")
    if(NOT installed STREQUAL expected)
        message(FATAL_ERROR
            "run_consumer: step install-contents failed: the consumer's install holds "
            "'${installed}', expected '${expected}'")
    endif()
    message(STATUS "vendored: the consumer's install holds only ${expected}")
elseif(MODE STREQUAL "duplicate")
    consumer_expect_refusal(duplicate duplicate
        EXPECT "Overdrive was added twice: first from ${OVERDRIVE_SOURCE_DIR}, again from ${OVERDRIVE_SOURCE_DIR}"
        ARGS "-DOVERDRIVE_SOURCE_DIR=${OVERDRIVE_SOURCE_DIR}")
    consumer_expect_refusal(collision collision
        EXPECT "a target named overdrive already exists (defined in ${CONSUMER_SOURCE_DIR}); Overdrive needs that name"
        ARGS "-DOVERDRIVE_SOURCE_DIR=${OVERDRIVE_SOURCE_DIR}")
elseif(MODE STREQUAL "installed")
    if(NOT DEFINED consumer_install_libdir)
        message(FATAL_ERROR "run_consumer: step arguments failed: Overdrive's install rules are off")
    endif()
    # 1. Install, to a path with a space (R206).
    set(prefix "${WORK_DIR}/installed/a b/prefix")
    set(moved "${WORK_DIR}/installed/a b/moved")
    file(REMOVE_RECURSE "${WORK_DIR}/installed")
    consumer_install("${OVERDRIVE_BUILD_DIR}" "${prefix}" installed)

    # 2. Exactly the contract's files (R211, FR-009, FR-010).
    set(libdir "${consumer_install_libdir}")
    set(package "${libdir}/cmake/overdrive")
    set(doc "${consumer_install_datarootdir}/doc/overdrive")
    set(required "${package}/overdrive-config.cmake" "${package}/overdrive-config-version.cmake"
        "${package}/overdrive-targets.cmake" "${doc}/LICENSE" "${doc}/LICENSE-UCD"
        "${consumer_library_file}" "${consumer_library_linker_file}")
    # The headers come from the one list that builds the file set (R204),
    # so a header added there is required here without editing this file.
    set(header_alternatives "")
    foreach(header IN LISTS consumer_public_headers)
        list(APPEND required "${consumer_install_includedir}/overdrive/${header}")
        string(REPLACE "." "[.]" header_pattern "${header}")
        list(APPEND header_alternatives "${header_pattern}")
    endforeach()
    list(JOIN header_alternatives "|" header_alternatives)
    foreach(file IN LISTS required)
        if(NOT file IN_LIST installed)
            message(FATAL_ERROR
                "run_consumer: step install-contents failed: ${file} is missing; installed:\n${installed}")
        endif()
    endforeach()
    # GNUInstallDirs names (lib, lib64, lib/<multiarch>, share, include,
    # bin) hold no regex metacharacter, so they go into the patterns as is.
    set(libdir_pattern "${libdir}")
    set(bindir_pattern "${consumer_install_bindir}")
    set(doc_pattern "${doc}")
    set(include_pattern "${consumer_install_includedir}")
    set(library_name "(lib)?overdrive([.][0-9]+)*[.](a|so|dylib|dll[.]a|lib)([.][0-9]+)*")
    foreach(file IN LISTS installed)
        if(NOT file MATCHES "^${include_pattern}/overdrive/(${header_alternatives})$"
                AND NOT file MATCHES "^${libdir_pattern}/${library_name}$"
                AND NOT file MATCHES "^${bindir_pattern}/(lib)?overdrive[.]dll$"
                AND NOT file MATCHES "^${libdir_pattern}/cmake/overdrive/overdrive-(config|config-version|targets|targets-[a-z]+)[.]cmake$"
                AND NOT file MATCHES "^${doc_pattern}/(LICENSE|LICENSE-UCD)$")
            message(FATAL_ERROR
                "run_consumer: step install-contents failed: ${file} is outside the contract's list")
        endif()
    endforeach()

    # 3. The license files are unchanged (FR-009).
    foreach(pair IN ITEMS "LICENSE=LICENSE" "LICENSE-UCD=third_party/ucd/LICENSE")
        string(REPLACE "=" ";" pair "${pair}")
        list(GET pair 0 installed_name)
        list(GET pair 1 source_name)
        file(SHA256 "${prefix}/${doc}/${installed_name}" installed_hash)
        file(SHA256 "${OVERDRIVE_SOURCE_DIR}/${source_name}" source_hash)
        if(NOT installed_hash STREQUAL source_hash)
            message(FATAL_ERROR
                "run_consumer: step license-hash failed: ${doc}/${installed_name} differs from ${source_name}")
        endif()
    endforeach()

    # 4. The package files name no build-time path (R206). The prefix
    # itself lies under the source tree, so it is caught too.
    file(GLOB package_files "${prefix}/${package}/*.cmake")
    foreach(file IN LISTS package_files)
        file(READ "${file}" text)
        foreach(path IN ITEMS "${OVERDRIVE_SOURCE_DIR}" "${OVERDRIVE_BUILD_DIR}")
            string(FIND "${text}" "${path}" found)
            if(NOT found EQUAL -1)
                message(FATAL_ERROR
                    "run_consumer: step build-paths failed: ${file} contains ${path}")
            endif()
        endforeach()
    endforeach()

    # 5. Move the prefix (FR-008).
    file(RENAME "${prefix}" "${moved}")

    # 6. Find the package there and nowhere else, then build and run.
    set(find_arguments "-DCMAKE_PREFIX_PATH=${moved}" "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF")
    consumer_configure_build_run(installed installed/build ${find_arguments})
    file(STRINGS "${WORK_DIR}/installed/build/CMakeCache.txt" found_dir REGEX "^overdrive_DIR:")
    string(REGEX REPLACE "^overdrive_DIR:[A-Z]+=" "" found_dir "${found_dir}")
    cmake_path(IS_PREFIX moved "${found_dir}" NORMALIZE found_in_moved)
    if(NOT found_in_moved)
        message(FATAL_ERROR
            "run_consumer: step find-location failed: overdrive_DIR is '${found_dir}', not under ${moved}")
    endif()

    # 7. A request for another minor version is refused (FR-007). 0.2 is
    # refused by any compatibility rule; 0.0 only by SameMinorVersion,
    # since the major versions match.
    foreach(other IN ITEMS 0.2 0.0)
        consumer_expect_refusal(installed "installed/version-${other}"
            EXPECT "requested version \"${other}\"" "version: 0.1.0"
            ARGS ${find_arguments} "-DOVERDRIVE_CONSUMER_VERSION=${other}")
    endforeach()
else()
    message(FATAL_ERROR "run_consumer: step arguments failed: unknown MODE '${MODE}'")
endif()
