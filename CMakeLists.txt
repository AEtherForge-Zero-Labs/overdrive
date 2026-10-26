cmake_minimum_required(VERSION 3.28)
project(overdrive VERSION 0.1.0 LANGUAGES C)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build through CMakePresets.json.")
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(overdrive_compiler_minimum 14)
elseif(CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    set(overdrive_compiler_minimum 17)
elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(overdrive_compiler_minimum 18)
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
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

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
option(OVERDRIVE_BUILD_TESTS "Build Overdrive's tests" ON)
option(OVERDRIVE_BUILD_EXAMPLES "Build available Overdrive examples" OFF)

function(ovd_target_warnings target)
    target_compile_options("${target}" PRIVATE -Wall -Wextra -Wpedantic -Werror)
endfunction()

add_library(overdrive src/core.c)
add_library(overdrive::overdrive ALIAS overdrive)
target_compile_features(overdrive PUBLIC c_std_23)
target_include_directories(overdrive PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
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

set(overdrive_public_headers core.h terminal.h render.h overdrive.h)
foreach(header IN LISTS overdrive_public_headers)
    target_sources(overdrive PRIVATE "include/overdrive/${header}")
endforeach()

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
target_sources(overdrive PRIVATE src/color.c)
