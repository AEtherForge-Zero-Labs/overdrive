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
ovd_add_test(test_unicode)
# The iterator is hidden in the shared library, so the test compiles it in.
target_sources(test_unicode PRIVATE "${PROJECT_SOURCE_DIR}/src/unicode.c")
target_include_directories(test_unicode PRIVATE "${PROJECT_BINARY_DIR}/generated")
add_dependencies(test_unicode ovd_unicode_tables)
