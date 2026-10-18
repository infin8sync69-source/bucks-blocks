# Install layout check, run by ctest as:
#   cmake -DBUCKS_ROOT=<source dir> -DBUCKS_BUILD=<build dir> -DPREFIX=<scratch dir>
#         -DCONFIG=<build type> -DCHECK_WORDLISTS=<bucks-check-wordlists> -P install_layout.cmake
# Installs the build into a scratch prefix and fails unless it is the layout the node looks for:
# <prefix>/bin/{bucks,bucks-mine-genesis}, <prefix>/share/bucks/web (the whole UI, no tests) and
# <prefix>/share/bucks/wordlists (the manifest and exactly the lists it names, byte for byte),
# with nothing from googletest or the test suites.

foreach(var BUCKS_ROOT BUCKS_BUILD PREFIX CHECK_WORDLISTS)
    if(NOT ${var})
        message(FATAL_ERROR "${var} is not set")
    endif()
endforeach()

set(errors "")
file(REMOVE_RECURSE "${PREFIX}")
execute_process(
    COMMAND ${CMAKE_COMMAND} "-DCMAKE_INSTALL_PREFIX=${PREFIX}" "-DCMAKE_INSTALL_CONFIG_NAME=${CONFIG}"
            -P "${BUCKS_BUILD}/cmake_install.cmake"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "install into ${PREFIX} failed (${rc}):\n${out}")
endif()

# --- bin/: the node and the genesis miner, nothing else ---
file(GLOB installed_bin RELATIVE "${PREFIX}/bin" "${PREFIX}/bin/*")
list(SORT installed_bin)
if(NOT installed_bin STREQUAL "bucks;bucks-mine-genesis" AND NOT installed_bin STREQUAL "bucks-mine-genesis.exe;bucks.exe")
    list(JOIN installed_bin ", " shown)
    list(APPEND errors "bin/ holds '${shown}', expected bucks and bucks-mine-genesis")
endif()
execute_process(COMMAND "${PREFIX}/bin/bucks" --help RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0)
    list(APPEND errors "installed bin/bucks --help exited with ${rc}")
endif()

# --- share/bucks/web: every UI file, but not the JS tests or their package.json ---
file(GLOB_RECURSE ui_files RELATIVE "${BUCKS_ROOT}/web" "${BUCKS_ROOT}/web/*")
set(ui_count 0)
foreach(f IN LISTS ui_files)
    if(f MATCHES "^(test/|node_modules/)" OR f STREQUAL "package.json")
        if(EXISTS "${PREFIX}/share/bucks/web/${f}")
            list(APPEND errors "share/bucks/web/${f} is installed but is not part of the UI")
        endif()
        continue()
    endif()
    math(EXPR ui_count "${ui_count} + 1")
    if(NOT EXISTS "${PREFIX}/share/bucks/web/${f}")
        list(APPEND errors "web/${f} is not installed to share/bucks/web")
    endif()
endforeach()
if(NOT EXISTS "${PREFIX}/share/bucks/web/index.html")
    list(APPEND errors "share/bucks/web/index.html is missing")
endif()

# --- share/bucks/wordlists: the manifest and the lists it names, identical to the source ---
file(READ "${BUCKS_ROOT}/wordlists/manifest.json" manifest)
string(REGEX MATCHALL "\"file\" *: *\"[^\"]+\"" entries "${manifest}")
set(expected manifest.json)
foreach(entry IN LISTS entries)
    string(REGEX REPLACE ".*\"([^\"]+)\"$" "\\1" list_file "${entry}")
    list(APPEND expected "${list_file}")
endforeach()
list(SORT expected)
file(GLOB installed_lists RELATIVE "${PREFIX}/share/bucks/wordlists" "${PREFIX}/share/bucks/wordlists/*")
list(SORT installed_lists)
if(NOT installed_lists STREQUAL expected)
    list(JOIN installed_lists ", " shown)
    list(JOIN expected ", " wanted)
    list(APPEND errors "share/bucks/wordlists holds '${shown}', expected '${wanted}'")
endif()
foreach(f IN LISTS expected)
    if(EXISTS "${PREFIX}/share/bucks/wordlists/${f}")
        file(SHA256 "${BUCKS_ROOT}/wordlists/${f}" want)
        file(SHA256 "${PREFIX}/share/bucks/wordlists/${f}" got)
        if(NOT want STREQUAL got)
            list(APPEND errors "share/bucks/wordlists/${f} differs from wordlists/${f}")
        endif()
    endif()
endforeach()
execute_process(COMMAND "${CHECK_WORDLISTS}" "${PREFIX}/share/bucks/wordlists"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
if(NOT rc EQUAL 0)
    list(APPEND errors "bucks-check-wordlists rejects the installed lists:\n${out}")
endif()

# --- nothing else: googletest's headers and libraries stay out (INSTALL_GTEST OFF) ---
file(GLOB top RELATIVE "${PREFIX}" "${PREFIX}/*")
list(SORT top)
if(NOT top STREQUAL "bin;share")
    list(JOIN top ", " shown)
    list(APPEND errors "the prefix holds '${shown}', expected only bin and share")
endif()
file(GLOB share_top RELATIVE "${PREFIX}/share" "${PREFIX}/share/*")
if(NOT share_top STREQUAL "bucks")
    list(JOIN share_top ", " shown)
    list(APPEND errors "share/ holds '${shown}', expected only bucks")
endif()

if(errors)
    string(REPLACE ";" "\n  " errors "${errors}")
    message(FATAL_ERROR "Install layout check failed:\n  ${errors}")
endif()
file(REMOVE_RECURSE "${PREFIX}")
list(LENGTH expected list_count)
message(STATUS "Install layout OK (${ui_count} UI files, ${list_count} word-list files)")
