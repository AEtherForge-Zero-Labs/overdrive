function(ovd_add_test name)
    add_executable("${name}" "${name}.c")
    target_link_libraries("${name}" PRIVATE overdrive::overdrive)
    target_include_directories("${name}" PRIVATE "${PROJECT_SOURCE_DIR}/src")
    ovd_target_warnings("${name}")
    add_test(NAME "${name}" COMMAND "${name}")
endfunction()

# Public headers may include one another and ISO C headers, not OS/private headers.
set(c_standard_headers
    assert.h complex.h ctype.h errno.h fenv.h float.h inttypes.h iso646.h
    limits.h locale.h math.h setjmp.h signal.h stdalign.h stdarg.h stdatomic.h
    stdbit.h stdbool.h stdckdint.h stddef.h stdint.h stdio.h stdlib.h
    stdnoreturn.h string.h tgmath.h threads.h time.h uchar.h wchar.h wctype.h)
set(header_sources)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/headers")
foreach(header IN LISTS overdrive_public_headers)
    set(path "${PROJECT_SOURCE_DIR}/include/overdrive/${header}")
    file(STRINGS "${path}" includes REGEX "^[ \t]*#[ \t]*include")
    foreach(directive IN LISTS includes)
        if(NOT directive MATCHES "^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"]")
            message(FATAL_ERROR "Public header ${header} has a non-literal include.")
        endif()
        set(dependency "${CMAKE_MATCH_1}")
        if(dependency MATCHES "^overdrive/(.+)$")
            if(NOT CMAKE_MATCH_1 IN_LIST overdrive_public_headers)
                message(FATAL_ERROR "${header} includes a non-public header: ${dependency}")
            endif()
        elseif(NOT dependency IN_LIST c_standard_headers)
            message(FATAL_ERROR "${header} includes a non-ISO/OS header: ${dependency}")
        endif()
    endforeach()
    set(tu "${CMAKE_CURRENT_BINARY_DIR}/headers/${header}.c")
    file(WRITE "${tu}" "#include <overdrive/${header}>\n#include <overdrive/${header}>\n")
    list(APPEND header_sources "${tu}")
endforeach()
add_library(overdrive_header_checks OBJECT ${header_sources})
target_link_libraries(overdrive_header_checks PRIVATE overdrive::overdrive)
ovd_target_warnings(overdrive_header_checks)

if(WIN32)
    set(export_format PE)
elseif(APPLE)
    set(export_format MACHO)
else()
    set(export_format ELF)
endif()

function(ovd_add_export_test name target)
    add_test(NAME "${name}" COMMAND "${CMAKE_COMMAND}"
        "-DLIBRARY=$<TARGET_FILE:${target}>"
        "-DLIBRARY_KIND=$<TARGET_PROPERTY:${target},TYPE>"
        "-DFORMAT=${export_format}" "-DNM=${CMAKE_NM}" "-DOBJDUMP=${CMAKE_OBJDUMP}"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_exports.cmake")
endfunction()

# The good fixture also contains an unexported generic helper. The bad fixture
# deliberately exports a generic name, exercising the checker rather than an allowlist.
foreach(kind good bad)
    set(fixture "${CMAKE_CURRENT_BINARY_DIR}/export_${kind}.c")
    if(kind STREQUAL "good")
        set(symbol ovd_export_fixture)
    else()
        set(symbol generic_export_fixture)
    endif()
    file(WRITE "${fixture}"
        "#include <overdrive/core.h>\n"
        "int hidden_fixture_helper(void) { return 1; }\n"
        "OVD_API int ${symbol}(void) { return hidden_fixture_helper(); }\n")
    add_library("export_${kind}" SHARED "${fixture}")
    target_include_directories("export_${kind}" PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_compile_definitions("export_${kind}" PRIVATE OVERDRIVE_SHARED OVERDRIVE_BUILDING)
    set_target_properties("export_${kind}" PROPERTIES C_VISIBILITY_PRESET hidden
        WINDOWS_EXPORT_ALL_SYMBOLS OFF)
    ovd_target_warnings("export_${kind}")
endforeach()
ovd_add_export_test(check_exports overdrive)
ovd_add_export_test(check_exports_good export_good)

# Reject a false positive caused by a missing tool or malformed listing.
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/check_exports_negative.cmake" CONTENT "
execute_process(COMMAND \"${CMAKE_COMMAND}\"
    \"-DLIBRARY=$<TARGET_FILE:export_bad>\" \"-DLIBRARY_KIND=SHARED_LIBRARY\"
    \"-DFORMAT=${export_format}\" \"-DNM=${CMAKE_NM}\" \"-DOBJDUMP=${CMAKE_OBJDUMP}\"
    -P \"${PROJECT_SOURCE_DIR}/cmake/check_exports.cmake\"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result STREQUAL \"0\" OR NOT error MATCHES \"Non-ovd export: generic_export_fixture\")
    message(FATAL_ERROR \"Expected the generic export rejection; got: \${output}\${error}\")
endif()
")
add_test(NAME check_exports_rejects_generic COMMAND "${CMAKE_COMMAND}"
    -P "${CMAKE_CURRENT_BINARY_DIR}/check_exports_negative.cmake")

ovd_add_test(test_core)
ovd_add_test(test_headers)
ovd_add_test(test_detect)
# Internal functions are hidden in a shared build; the test compiles its modules directly.
if(WIN32)
    target_sources(test_detect PRIVATE "${PROJECT_SOURCE_DIR}/src/platform/platform_windows.c")
else()
    target_sources(test_detect PRIVATE "${PROJECT_SOURCE_DIR}/src/platform/platform_posix.c")
endif()
target_sources(test_detect PRIVATE "${PROJECT_SOURCE_DIR}/src/detect.c")
# The real environment lookup is tested against variables set for this test only.
set_tests_properties(test_detect PROPERTIES ENVIRONMENT_MODIFICATION
    "OVD_TEST_DETECT_PROBE=set:probe-value;OVD_TEST_DETECT_UNSET=unset:;NO_COLOR=set:1")
# The static archive lists every global symbol, hidden or not (R12): a hidden,
# non-static generic name must still be rejected there.
set(static_fixture "${CMAKE_CURRENT_BINARY_DIR}/export_static_bad.c")
file(WRITE "${static_fixture}"
    "#include <overdrive/core.h>\n"
    "int generic_static_fixture(void) { return 1; }\n"
    "OVD_API int ovd_static_fixture(void) { return generic_static_fixture(); }\n")
add_library(export_static_bad STATIC "${static_fixture}")
target_include_directories(export_static_bad PRIVATE "${PROJECT_SOURCE_DIR}/include")
set_target_properties(export_static_bad PROPERTIES C_VISIBILITY_PRESET hidden)
ovd_target_warnings(export_static_bad)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/check_exports_static_negative.cmake" CONTENT "
execute_process(COMMAND \"${CMAKE_COMMAND}\"
    \"-DLIBRARY=$<TARGET_FILE:export_static_bad>\" \"-DLIBRARY_KIND=STATIC_LIBRARY\"
    \"-DFORMAT=${export_format}\" \"-DNM=${CMAKE_NM}\" \"-DOBJDUMP=${CMAKE_OBJDUMP}\"
    -P \"\${OVD_CHECK_EXPORTS}\"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result STREQUAL \"0\" OR NOT error MATCHES \"Non-ovd export: generic_static_fixture\")
    message(FATAL_ERROR \"Expected the static generic rejection; got: \${output}\${error}\")
endif()
")
add_test(NAME check_exports_static_rejects_generic COMMAND "${CMAKE_COMMAND}"
    "-DOVD_CHECK_EXPORTS=${PROJECT_SOURCE_DIR}/cmake/check_exports.cmake"
    -P "${CMAKE_CURRENT_BINARY_DIR}/check_exports_static_negative.cmake")
ovd_add_test(test_alloc)
# Shared builds hide internal symbols, so the test compiles the modules it tests.
target_sources(test_alloc PRIVATE support/test_alloc.c
    "${PROJECT_SOURCE_DIR}/src/alloc.c" "${PROJECT_SOURCE_DIR}/src/output.c")
ovd_add_test(test_unicode)
# The iterator is hidden in the shared library, so the test compiles it in.
target_sources(test_unicode PRIVATE "${PROJECT_SOURCE_DIR}/src/unicode.c")
target_include_directories(test_unicode PRIVATE "${PROJECT_BINARY_DIR}/generated")
add_dependencies(test_unicode ovd_unicode_tables)
ovd_add_test(test_color)
# Internal functions are hidden in a shared build; the test compiles its module directly.
target_sources(test_color PRIVATE "${PROJECT_SOURCE_DIR}/src/color.c")

# Replay interpreter and screen model: the oracle for rendering tests (SC-001).
add_library(overdrive_vt_replay STATIC support/screen.c support/vt_replay.c)
target_link_libraries(overdrive_vt_replay PUBLIC $<COMPILE_ONLY:overdrive::overdrive>)
target_include_directories(overdrive_vt_replay PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/support")
ovd_target_warnings(overdrive_vt_replay)
ovd_add_test(test_vt_replay)
target_link_libraries(test_vt_replay PRIVATE overdrive_vt_replay)
ovd_add_test(test_canvas)
# The grid and canvas internals are hidden in a shared build; compile them in.
target_sources(test_canvas PRIVATE support/test_alloc.c
    "${PROJECT_SOURCE_DIR}/src/alloc.c" "${PROJECT_SOURCE_DIR}/src/buffer.c"
    "${PROJECT_SOURCE_DIR}/src/canvas.c" "${PROJECT_SOURCE_DIR}/src/unicode.c")
target_include_directories(test_canvas PRIVATE "${PROJECT_BINARY_DIR}/generated")
add_dependencies(test_canvas ovd_unicode_tables)
# canvas.c defines OVD_API functions: compiled here it is Overdrive's own code,
# so a Windows shared build must see them as dllexport, not dllimport.
target_compile_definitions(test_canvas PRIVATE OVERDRIVE_BUILDING)
ovd_add_test(test_diff)
# The diff and the modules it reads are hidden in a shared build; compile them in.
target_sources(test_diff PRIVATE support/test_alloc.c
    "${PROJECT_SOURCE_DIR}/src/alloc.c" "${PROJECT_SOURCE_DIR}/src/buffer.c"
    "${PROJECT_SOURCE_DIR}/src/output.c" "${PROJECT_SOURCE_DIR}/src/color.c"
    "${PROJECT_SOURCE_DIR}/src/diff.c")
target_link_libraries(test_diff PRIVATE overdrive_vt_replay)
ovd_add_test(test_box)
# The canvas internals are hidden in a shared build; compile them and box.c in.
target_sources(test_box PRIVATE support/test_alloc.c
    "${PROJECT_SOURCE_DIR}/src/alloc.c" "${PROJECT_SOURCE_DIR}/src/buffer.c"
    "${PROJECT_SOURCE_DIR}/src/canvas.c" "${PROJECT_SOURCE_DIR}/src/unicode.c"
    "${PROJECT_SOURCE_DIR}/src/box.c")
target_include_directories(test_box PRIVATE "${PROJECT_BINARY_DIR}/generated")
add_dependencies(test_box ovd_unicode_tables)
# canvas.c and box.c define OVD_API functions; a Windows shared build must
# see them as dllexport, not dllimport.
target_compile_definitions(test_box PRIVATE OVERDRIVE_BUILDING)
ovd_add_test(test_terminal)
# Public API only; the counting allocator is test support.
target_sources(test_terminal PRIVATE support/test_alloc.c)
# The headless backend must ignore these (R35).
set_tests_properties(test_terminal PROPERTIES ENVIRONMENT_MODIFICATION
    "NO_COLOR=set:1;COLORTERM=set:truecolor")
# The system backend with stdout redirected (T034). Public API only. The
# test is the driver below: it runs the program with stdout sent to a file,
# through the cross emulator when there is one, and fails unless the
# program exits 0 and the file is empty (R28 step 1: nothing written).
add_executable(test_system_notty test_system_notty.c support/test_alloc.c)
target_link_libraries(test_system_notty PRIVATE overdrive::overdrive)
target_include_directories(test_system_notty PRIVATE "${PROJECT_SOURCE_DIR}/src")
ovd_target_warnings(test_system_notty)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/test_system_notty.cmake" CONTENT "
set(output \"${CMAKE_CURRENT_BINARY_DIR}/test_system_notty.out\")
file(REMOVE \"\${output}\")
execute_process(COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} \"$<TARGET_FILE:test_system_notty>\"
    OUTPUT_FILE \"\${output}\" ERROR_VARIABLE error RESULT_VARIABLE result)
message(\"\${error}\")
if(NOT result STREQUAL \"0\")
    message(FATAL_ERROR \"test_system_notty exited with \${result}\")
endif()
if(NOT EXISTS \"\${output}\")
    message(FATAL_ERROR \"The redirected stdout file was not created\")
endif()
file(SIZE \"\${output}\" size)
if(NOT size EQUAL 0)
    file(READ \"\${output}\" bytes HEX)
    message(FATAL_ERROR \"\${size} bytes reached stdout: \${bytes}\")
endif()
")
add_test(NAME test_system_notty COMMAND "${CMAKE_COMMAND}"
    -P "${CMAKE_CURRENT_BINARY_DIR}/test_system_notty.cmake")
# Steady-state allocations (T035, SC-004). Public API only; the counting
# allocator is test support.
ovd_add_test(test_steady_state)
target_sources(test_steady_state PRIVATE support/test_alloc.c)
# The system backend on a pseudo-terminal the test opens itself (#43),
# POSIX only: the enter and exit sequences, the modes create changes and
# destroy restores, create's rollbacks, and the per-frame size read.
# Public API only; the counting allocator and the replay oracle are test
# support.
if(UNIX)
    ovd_add_test(test_system_pty)
    target_sources(test_system_pty PRIVATE support/test_alloc.c)
    target_link_libraries(test_system_pty PRIVATE overdrive_vt_replay)
endif()

# The hello_overdrive example with stdout redirected (US2 scenario 3,
# OVD-12). The driver runs it with stdout sent to a file, through the
# cross emulator when there is one, and fails unless it exits 1, the file
# is empty, and stderr names OVD_ERROR_NOT_A_TERMINAL.
if(OVERDRIVE_BUILD_EXAMPLES)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/hello_overdrive_notty.cmake" CONTENT "
set(output \"${CMAKE_CURRENT_BINARY_DIR}/hello_overdrive_notty.out\")
file(REMOVE \"\${output}\")
execute_process(COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} \"$<TARGET_FILE:hello_overdrive>\"
    OUTPUT_FILE \"\${output}\" ERROR_VARIABLE error RESULT_VARIABLE result)
message(\"\${error}\")
if(NOT result STREQUAL \"1\")
    message(FATAL_ERROR \"hello_overdrive exited with \${result}, not 1\")
endif()
if(NOT EXISTS \"\${output}\")
    message(FATAL_ERROR \"The redirected stdout file was not created\")
endif()
file(SIZE \"\${output}\" size)
if(NOT size EQUAL 0)
    file(READ \"\${output}\" bytes HEX)
    message(FATAL_ERROR \"\${size} bytes reached stdout: \${bytes}\")
endif()
if(NOT error MATCHES \"ovd_terminal_create failed: OVD_ERROR_NOT_A_TERMINAL\")
    message(FATAL_ERROR \"stderr does not name OVD_ERROR_NOT_A_TERMINAL\")
endif()
")
    add_test(NAME hello_overdrive_notty COMMAND "${CMAKE_COMMAND}"
        -P "${CMAKE_CURRENT_BINARY_DIR}/hello_overdrive_notty.cmake")
endif()

# Input decoder (OVD-16). Its functions are hidden in a shared build, so
# each test compiles the decoder and the UTF-8 decoder it reuses.
foreach(decode_test IN ITEMS test_decode test_decode_fuzz)
    ovd_add_test("${decode_test}")
    target_sources("${decode_test}" PRIVATE "${PROJECT_SOURCE_DIR}/src/decode.c"
        "${PROJECT_SOURCE_DIR}/src/unicode.c")
    target_include_directories("${decode_test}" PRIVATE "${PROJECT_BINARY_DIR}/generated")
    add_dependencies("${decode_test}" ovd_unicode_tables)
endforeach()

# Headless input (OVD-17): wait, feed, virtual time, the paste buffer, and
# create's input allocations. Public API only; the counting allocator is
# test support.
ovd_add_test(test_input)
target_sources(test_input PRIVATE support/test_alloc.c)

# Abnormal-exit restoration (OVD-19, T117) on a pty with forked children,
# POSIX only; public API only. The sanitizers leave these signals alone,
# so the sanitize preset tests Overdrive's handlers, not theirs.
if(UNIX)
    ovd_add_test(test_restore_pty)
    set(restore_signal_options
        "handle_segv=0:handle_sigbus=0:handle_sigfpe=0:handle_sigill=0:handle_abort=0")
    set_tests_properties(test_restore_pty PROPERTIES ENVIRONMENT_MODIFICATION
        "ASAN_OPTIONS=set:${restore_signal_options};UBSAN_OPTIONS=set:${restore_signal_options}")
endif()

# The vendored and installed routes (specs/003-packaging, R208): each
# test configures, builds, and runs tests/consumer as a separate project
# with this build's settings; consumer_installed first installs this
# build, and exists only when its install rules do. The command is a
# script path, not a target, so CTest does not prepend the emulator; the
# script runs the program through it. The
# system name and processor are forwarded only when cross compiling:
# setting CMAKE_SYSTEM_NAME marks a build as cross.
string(CONCAT consumer_settings "set(consumer_generator [==[${CMAKE_GENERATOR}]==])\n"
    "set(consumer_executable_suffix [==[${CMAKE_EXECUTABLE_SUFFIX}]==])\n")
# The install layout and the library's file names, for the installed
# route's file list (R211).
if(OVERDRIVE_INSTALL)
    if(WIN32 AND BUILD_SHARED_LIBS)
        set(consumer_runtime_dir "${CMAKE_INSTALL_BINDIR}")
    else()
        set(consumer_runtime_dir "${CMAKE_INSTALL_LIBDIR}")
    endif()
    string(APPEND consumer_settings
        "set(consumer_install_libdir [==[${CMAKE_INSTALL_LIBDIR}]==])\n"
        "set(consumer_install_bindir [==[${CMAKE_INSTALL_BINDIR}]==])\n"
        "set(consumer_install_includedir [==[${CMAKE_INSTALL_INCLUDEDIR}]==])\n"
        "set(consumer_install_datarootdir [==[${CMAKE_INSTALL_DATAROOTDIR}]==])\n"
        "set(consumer_public_headers [==[${overdrive_public_headers}]==])\n"
        "set(consumer_library_file [==[${consumer_runtime_dir}/$<TARGET_FILE_NAME:overdrive>]==])\n"
        "set(consumer_library_linker_file [==[${CMAKE_INSTALL_LIBDIR}/$<TARGET_LINKER_FILE_NAME:overdrive>]==])\n")
endif()
set(consumer_forwarded CMAKE_MAKE_PROGRAM CMAKE_TOOLCHAIN_FILE CMAKE_C_COMPILER
    CMAKE_BUILD_TYPE BUILD_SHARED_LIBS CMAKE_C_FLAGS CMAKE_EXE_LINKER_FLAGS
    CMAKE_SHARED_LINKER_FLAGS CMAKE_FIND_ROOT_PATH_MODE_PROGRAM
    CMAKE_CROSSCOMPILING_EMULATOR CMAKE_FIND_ROOT_PATH)
if(CMAKE_CROSSCOMPILING)
    list(APPEND consumer_forwarded CMAKE_SYSTEM_NAME CMAKE_SYSTEM_PROCESSOR)
endif()
foreach(name IN LISTS consumer_forwarded)
    if(DEFINED "${name}" AND NOT "${${name}}" STREQUAL "")
        string(APPEND consumer_settings
            "set(forward_${name} [==[${${name}}]==])\nlist(APPEND forward_names ${name})\n")
    endif()
endforeach()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/consumer_settings.cmake"
    CONTENT "${consumer_settings}")
set(consumer_modes vendored duplicate)
if(OVERDRIVE_INSTALL)
    list(APPEND consumer_modes installed)
endif()
foreach(consumer_mode IN LISTS consumer_modes)
    add_test(NAME "consumer_${consumer_mode}" COMMAND "${CMAKE_COMMAND}"
        "-DMODE=${consumer_mode}"
        "-DOVERDRIVE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DOVERDRIVE_BUILD_DIR=${PROJECT_BINARY_DIR}"
        "-DCONSUMER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/consumer"
        "-DWORK_DIR=${PROJECT_BINARY_DIR}/consumer"
        "-DSETTINGS=${CMAKE_CURRENT_BINARY_DIR}/consumer_settings.cmake"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/consumer/run_consumer.cmake")
    # Takes precedence over the presets' 60 s default; the vendored route
    # compiles the library again.
    set_tests_properties("consumer_${consumer_mode}" PROPERTIES TIMEOUT 300)
endforeach()
