cmake_minimum_required(VERSION 3.28)
# The version is set below, not by project(VERSION): as a subproject,
# that would also write CMAKE_PROJECT_VERSION* into the consumer's cache
# whenever the consumer's own project() gives no version (R213).
project(overdrive LANGUAGES C)
set(PROJECT_VERSION 0.1.0)
set(PROJECT_VERSION_MAJOR 0)

# One copy per build, and the name overdrive is Overdrive's (R203). A
# global property starts empty on every configure, so a re-run passes.
get_property(overdrive_first_source_dir GLOBAL PROPERTY OVERDRIVE_SOURCE_DIR)
if(overdrive_first_source_dir)
    message(FATAL_ERROR "Overdrive was added twice: first from "
        "${overdrive_first_source_dir}, again from ${PROJECT_SOURCE_DIR}")
endif()
if(TARGET overdrive)
    get_target_property(overdrive_existing_dir overdrive SOURCE_DIR)
    message(FATAL_ERROR "a target named overdrive already exists (defined in "
        "${overdrive_existing_dir}); Overdrive needs that name")
endif()
set_property(GLOBAL PROPERTY OVERDRIVE_SOURCE_DIR "${PROJECT_SOURCE_DIR}")

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build through CMakePresets.json.")
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(overdrive_compiler_minimum 14)
elseif(CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    set(overdrive_compiler_minimum 17)
elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(overdrive_compiler_minimum 19)
else()
    message(FATAL_ERROR "Overdrive requires GCC, Clang, or Apple Clang with C23 support.")
endif()
if(CMAKE_C_COMPILER_VERSION VERSION_LESS overdrive_compiler_minimum)
    message(FATAL_ERROR
        "${CMAKE_C_COMPILER_ID} ${overdrive_compiler_minimum}+ is required; "
        "found ${CMAKE_C_COMPILER_VERSION}. See the machine baseline.")
endif()

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
# A layout for the whole build tree: a consumer's choice when vendored (R202).
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

if(CMAKE_GENERATOR MATCHES "^Ninja")
    execute_process(COMMAND "${CMAKE_MAKE_PROGRAM}" --version
        OUTPUT_VARIABLE overdrive_ninja_version
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE overdrive_ninja_result)
    if(NOT overdrive_ninja_result EQUAL 0 OR overdrive_ninja_version VERSION_LESS 1.11)
        message(FATAL_ERROR "Ninja 1.11+ is required.")
    endif()
endif()

# Check the features the approved public headers actually require.
try_compile(overdrive_has_c23
    SOURCE_FROM_CONTENT features.c [=[
        #include <stdint.h>
        enum Kind : uint8_t { KIND_ZERO };
        [[nodiscard]] static bool available(void) { return true; }
        int main(void) {
            void *pointer = nullptr;
            enum Kind kind = KIND_ZERO;
            return available() && pointer == nullptr
                && kind == KIND_ZERO ? 0 : 1;
        }
]=]
    C_STANDARD 23 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    COMPILE_DEFINITIONS -Wall -Wextra -Wpedantic -Werror
    OUTPUT_VARIABLE overdrive_c23_log)
if(NOT overdrive_has_c23)
    message(FATAL_ERROR "The compiler lacks required C23 features:\n${overdrive_c23_log}")
endif()

option(BUILD_SHARED_LIBS "Build Overdrive as a shared library" OFF)
# Off when vendored, so a consumer builds none of Overdrive's tests (R201).
option(OVERDRIVE_BUILD_TESTS "Build Overdrive's tests" ${PROJECT_IS_TOP_LEVEL})
option(OVERDRIVE_BUILD_EXAMPLES "Build available Overdrive examples" OFF)

function(ovd_target_warnings target)
    target_compile_options("${target}" PRIVATE -Wall -Wextra -Wpedantic -Werror)
endfunction()

add_library(overdrive src/core.c)
add_library(overdrive::overdrive ALIAS overdrive)
target_compile_features(overdrive PUBLIC c_std_23)
target_compile_definitions(overdrive PRIVATE OVERDRIVE_BUILDING)
set_target_properties(overdrive PROPERTIES
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}"
    C_VISIBILITY_PRESET hidden
    WINDOWS_EXPORT_ALL_SYMBOLS OFF)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(overdrive PUBLIC OVERDRIVE_SHARED)
endif()
ovd_target_warnings(overdrive)

# The public headers, as one PUBLIC file set: its base directory is the
# include directory of the build tree (R204). tests/ reads the list too.
set(overdrive_public_headers core.h terminal.h render.h input.h overdrive.h)
list(TRANSFORM overdrive_public_headers PREPEND "include/overdrive/"
    OUTPUT_VARIABLE overdrive_public_header_files)
target_sources(overdrive PUBLIC FILE_SET HEADERS BASE_DIRS include
    FILES ${overdrive_public_header_files})

if(OVERDRIVE_BUILD_TESTS)
    enable_testing()
    if(CMAKE_CROSSCOMPILING)
        if(NOT CMAKE_CROSSCOMPILING_EMULATOR)
            message(FATAL_ERROR "Cross tests require CMAKE_CROSSCOMPILING_EMULATOR.")
        endif()
        list(GET CMAKE_CROSSCOMPILING_EMULATOR 0 overdrive_emulator)
        find_program(overdrive_emulator_path NAMES "${overdrive_emulator}" REQUIRED)
        execute_process(COMMAND "${overdrive_emulator_path}" --version
            OUTPUT_VARIABLE overdrive_wine_version
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE overdrive_wine_result)
        if(NOT overdrive_wine_result EQUAL 0
                OR NOT overdrive_wine_version MATCHES "^wine-([0-9]+\\.[0-9]+)")
            message(FATAL_ERROR "The declared cross-test path requires Wine 10.0+.")
        endif()
        if(CMAKE_MATCH_1 VERSION_LESS 10.0)
            message(FATAL_ERROR "Wine 10.0+ is required; found ${overdrive_wine_version}.")
        endif()
    endif()
    add_subdirectory(tests)
endif()

if(OVERDRIVE_BUILD_EXAMPLES)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
        message(FATAL_ERROR "No example targets are available in this implementation increment.")
    endif()
    add_subdirectory(examples)
endif()
# One platform file per build (research R11) and color detection (R10).
if(WIN32)
    target_sources(overdrive PRIVATE src/platform/platform_windows.c)
else()
    target_sources(overdrive PRIVATE src/platform/platform_posix.c)
endif()
target_sources(overdrive PRIVATE src/detect.c)

target_sources(overdrive PRIVATE src/alloc.c src/output.c)

# Unicode (OVD-03): range tables generated into the build tree from the
# vendored UCD by the build itself (Art. II, R5).
set(overdrive_unicode_tables "${PROJECT_BINARY_DIR}/generated/unicode_tables.h")
add_custom_command(OUTPUT "${overdrive_unicode_tables}"
    COMMAND "${CMAKE_COMMAND}"
        "-DEAST_ASIAN_WIDTH=${PROJECT_SOURCE_DIR}/third_party/ucd/EastAsianWidth.txt"
        "-DGENERAL_CATEGORY=${PROJECT_SOURCE_DIR}/third_party/ucd/DerivedGeneralCategory.txt"
        "-DOUTPUT=${overdrive_unicode_tables}"
        -P "${PROJECT_SOURCE_DIR}/cmake/gen_unicode_tables.cmake"
    DEPENDS cmake/gen_unicode_tables.cmake
        third_party/ucd/EastAsianWidth.txt third_party/ucd/DerivedGeneralCategory.txt
    COMMENT "Generating Unicode tables from the pinned UCD"
    VERBATIM)
add_custom_target(ovd_unicode_tables DEPENDS "${overdrive_unicode_tables}")
target_sources(overdrive PRIVATE src/unicode.c "${overdrive_unicode_tables}")
target_include_directories(overdrive PRIVATE "${PROJECT_BINARY_DIR}/generated")

target_sources(overdrive PRIVATE src/color.c)

# Cell grid and canvas (OVD-07).
target_sources(overdrive PRIVATE src/buffer.c src/canvas.c)

# Differential renderer (OVD-08).
target_sources(overdrive PRIVATE src/diff.c)

# Box drawing (OVD-09).
target_sources(overdrive PRIVATE src/box.c)

# Terminal and frame API, headless backend (OVD-10).
target_sources(overdrive PRIVATE src/terminal.c)

# System backend (OVD-11), through the platform layer.
target_sources(overdrive PRIVATE src/terminal_system.c)

# Render benchmark (OVD-13, US4): off unless the bench preset turns it on.
option(OVERDRIVE_BUILD_BENCHMARKS "Build Overdrive's benchmarks" OFF)
if(OVERDRIVE_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()

# Input decoder (OVD-16, R104).
target_sources(overdrive PRIVATE src/decode.c)
# Headless input: wait and feed over the decoder (OVD-17, R102, R109, R110).
target_sources(overdrive PRIVATE src/input.c)
