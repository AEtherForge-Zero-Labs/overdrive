# Generates the Unicode range tables src/unicode.c consumes (Art. II: the
# output lives in the build tree and is never committed).
#
#   cmake -DEAST_ASIAN_WIDTH=<EastAsianWidth.txt>
#         -DGENERAL_CATEGORY=<DerivedGeneralCategory.txt>
#         -DOUTPUT=<unicode_tables.h> -P gen_unicode_tables.cmake
#
# The output declares two sorted, non-overlapping, non-adjacent tables of
# inclusive code point ranges:
#   ovd_unicode_wide_ranges      East_Asian_Width W or F, including any
#                                @missing default of W or F (R16)
#   ovd_unicode_extender_ranges  General_Category Mn, Me, or Cf (R15)
cmake_minimum_required(VERSION 3.28)

# The pinned Unicode version (research R5, R17, decision log §D.4). Change it
# only together with the vendored files in third_party/ucd/.
set(OVD_UNICODE_VERSION "18.0.0")

foreach(variable EAST_ASIAN_WIDTH GENERAL_CATEGORY OUTPUT)
    if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
        message(FATAL_ERROR "gen_unicode_tables: ${variable} is required.")
    endif()
endforeach()

# Reads a UCD property file of the pinned version.
# Sets <out_ranges> to "first:last:value" entries (decimal code points) and
# <out_missing> to the same for its @missing lines.
function(ovd_read_ucd path name out_ranges out_missing)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "gen_unicode_tables: missing ${path}")
    endif()
    file(STRINGS "${path}" lines ENCODING UTF-8)
    list(GET lines 0 first_line)
    if(NOT first_line STREQUAL "# ${name}-${OVD_UNICODE_VERSION}.txt")
        message(FATAL_ERROR "gen_unicode_tables: ${path} is not ${name} "
            "${OVD_UNICODE_VERSION}; its first line is '${first_line}'.")
    endif()
    set(hex "([0-9A-F]+)")
    set(ranges)
    set(missing)
    foreach(line IN LISTS lines)
        if(line MATCHES "^# @missing: ${hex}\\.\\.${hex}[ \t]*;[ \t]*([A-Za-z]+)")
            math(EXPR first "0x${CMAKE_MATCH_1}")
            math(EXPR last "0x${CMAKE_MATCH_2}")
            list(APPEND missing "${first}:${last}:${CMAKE_MATCH_3}")
        elseif(line MATCHES "^#" OR line MATCHES "^[ \t]*$")
            continue()
        elseif(line MATCHES "^${hex}(\\.\\.${hex})?[ \t]*;[ \t]*([A-Za-z]+)[ \t]*(#.*)?$")
            math(EXPR first "0x${CMAKE_MATCH_1}")
            if(NOT CMAKE_MATCH_3 STREQUAL "")
                math(EXPR last "0x${CMAKE_MATCH_3}")
            else()
                set(last "${first}")
            endif()
            if(last LESS first OR last GREATER 1114111)
                message(FATAL_ERROR "gen_unicode_tables: bad range in ${path}: ${line}")
            endif()
            list(APPEND ranges "${first}:${last}:${CMAKE_MATCH_4}")
        else()
            message(FATAL_ERROR "gen_unicode_tables: cannot parse ${path}: ${line}")
        endif()
    endforeach()
    if(NOT ranges)
        message(FATAL_ERROR "gen_unicode_tables: no data in ${path}")
    endif()
    set(${out_ranges} "${ranges}" PARENT_SCOPE)
    set(${out_missing} "${missing}" PARENT_SCOPE)
endfunction()

# Sorts "first:last" entries and merges overlapping or adjacent ones.
function(ovd_merge_ranges in_ranges out_ranges)
    set(keyed)
    foreach(range IN LISTS ${in_ranges})
        string(REPLACE ":" ";" parts "${range}")
        list(GET parts 0 first)
        list(GET parts 1 last)
        # Zero-pad so a string sort is a numeric sort (max 1114111, 7 digits).
        string(LENGTH "${first}" digits)
        math(EXPR pad "7 - ${digits}")
        string(REPEAT "0" ${pad} zeros)
        list(APPEND keyed "${zeros}${first}:${last}")
    endforeach()
    list(SORT keyed)
    set(merged)
    set(current_first -1)
    set(current_last -2)
    foreach(range IN LISTS keyed)
        string(REPLACE ":" ";" parts "${range}")
        list(GET parts 0 first)
        list(GET parts 1 last)
        math(EXPR first "${first}")
        math(EXPR next "${current_last} + 1")
        if(current_first GREATER_EQUAL 0 AND first LESS_EQUAL next)
            if(last GREATER current_last)
                set(current_last "${last}")
            endif()
        else()
            if(current_first GREATER_EQUAL 0)
                list(APPEND merged "${current_first}:${current_last}")
            endif()
            set(current_first "${first}")
            set(current_last "${last}")
        endif()
    endforeach()
    if(current_first GREATER_EQUAL 0)
        list(APPEND merged "${current_first}:${current_last}")
    endif()
    set(${out_ranges} "${merged}" PARENT_SCOPE)
endfunction()

# Keeps the "first:last" of entries whose value is in the given list.
function(ovd_select_ranges in_ranges values out_ranges)
    set(selected)
    foreach(range IN LISTS ${in_ranges})
        string(REPLACE ":" ";" parts "${range}")
        list(GET parts 2 value)
        if(value IN_LIST ${values})
            list(GET parts 0 first)
            list(GET parts 1 last)
            list(APPEND selected "${first}:${last}")
        endif()
    endforeach()
    set(${out_ranges} "${selected}" PARENT_SCOPE)
endfunction()

# Returns the parts of [first, last] not covered by the sorted, merged
# "first:last" entries in covered.
function(ovd_uncovered first last covered out_ranges)
    set(gaps)
    set(cursor "${first}")
    foreach(range IN LISTS ${covered})
        string(REPLACE ":" ";" parts "${range}")
        list(GET parts 0 range_first)
        list(GET parts 1 range_last)
        if(range_last LESS cursor OR range_first GREATER last)
            continue()
        endif()
        if(range_first GREATER cursor)
            math(EXPR gap_last "${range_first} - 1")
            list(APPEND gaps "${cursor}:${gap_last}")
        endif()
        math(EXPR cursor "${range_last} + 1")
        if(cursor GREATER last)
            break()
        endif()
    endforeach()
    if(cursor LESS_EQUAL last)
        list(APPEND gaps "${cursor}:${last}")
    endif()
    set(${out_ranges} "${gaps}" PARENT_SCOPE)
endfunction()

function(ovd_emit_table name ranges out_text)
    list(LENGTH ${ranges} count)
    set(text "static const uint32_t ${name}[${count}][2] = {\n")
    foreach(range IN LISTS ${ranges})
        string(REPLACE ":" ";" parts "${range}")
        list(GET parts 0 first)
        list(GET parts 1 last)
        math(EXPR first "${first}" OUTPUT_FORMAT HEXADECIMAL)
        math(EXPR last "${last}" OUTPUT_FORMAT HEXADECIMAL)
        string(APPEND text "    { ${first}, ${last} },\n")
    endforeach()
    string(APPEND text "};\n")
    set(${out_text} "${text}" PARENT_SCOPE)
endfunction()

ovd_read_ucd("${EAST_ASIAN_WIDTH}" EastAsianWidth eaw_ranges eaw_missing)
ovd_read_ucd("${GENERAL_CATEGORY}" DerivedGeneralCategory gc_ranges gc_missing)

set(wide_values W F)
ovd_select_ranges(eaw_ranges wide_values wide)

# An @missing default applies only to code points listed nowhere else.
set(all_listed)
foreach(range IN LISTS eaw_ranges)
    string(REGEX REPLACE ":[A-Za-z]+$" "" range "${range}")
    list(APPEND all_listed "${range}")
endforeach()
ovd_merge_ranges(all_listed all_listed)
ovd_select_ranges(eaw_missing wide_values wide_defaults)
foreach(range IN LISTS wide_defaults)
    string(REPLACE ":" ";" parts "${range}")
    list(GET parts 0 first)
    list(GET parts 1 last)
    ovd_uncovered("${first}" "${last}" all_listed gaps)
    list(APPEND wide ${gaps})
endforeach()
ovd_merge_ranges(wide wide)

set(extender_values Mn Me Cf)
ovd_select_ranges(gc_ranges extender_values extenders)
ovd_merge_ranges(extenders extenders)

ovd_emit_table(ovd_unicode_wide_ranges wide wide_text)
ovd_emit_table(ovd_unicode_extender_ranges extenders extender_text)

file(CONFIGURE OUTPUT "${OUTPUT}" @ONLY NEWLINE_STYLE UNIX CONTENT
"/* Generated by cmake/gen_unicode_tables.cmake from Unicode @OVD_UNICODE_VERSION@.
 * Build output: do not edit or commit (Art. II). */
#ifndef OVERDRIVE_GENERATED_UNICODE_TABLES_H
#define OVERDRIVE_GENERATED_UNICODE_TABLES_H

#include <stdint.h>

/* East_Asian_Width W or F, inclusive ranges, sorted. */
@wide_text@
/* General_Category Mn, Me, or Cf, inclusive ranges, sorted. */
@extender_text@
#endif /* OVERDRIVE_GENERATED_UNICODE_TABLES_H */
")
