cmake_minimum_required(VERSION 3.14)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  URL_HASH SHA256=1f357c27ca988c3f7c6b4bf68a9395005ac6761f034046e9dde0896e3aba00e4
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# Force shared CRT to match the main application (usually helpful on Windows)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
# googletest is only for the test binaries; `cmake --install` must not put it in the prefix.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)
include(GoogleTest)

add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(system)
add_subdirectory(perf_security)
if(BUCKS_FUZZ)
    add_subdirectory(fuzz)
endif()

add_custom_target(tests ALL
    DEPENDS unit_tests integration_tests system_tests perf_security_tests bucks-check-wordlists
)

# Every shipped word list is valid and the set has no ambiguous spellings.
add_test(NAME wordlists
    COMMAND bucks-check-wordlists ${CMAKE_SOURCE_DIR}/wordlists)

# Checks that build.sh mirrors CMakeLists.txt and .gitignore covers secrets.
add_test(NAME build_hygiene
    COMMAND ${CMAKE_COMMAND} "-DBUCKS_ROOT=${CMAKE_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/build_hygiene.cmake"
)

# Register a meta-test invocation if needed, or rely on individual add_test calls

# `cmake --install` gives the layout the node looks for (bin/, share/bucks/web, share/bucks/wordlists).
add_test(NAME install_layout
    COMMAND ${CMAKE_COMMAND} "-DBUCKS_ROOT=${CMAKE_SOURCE_DIR}" "-DBUCKS_BUILD=${CMAKE_BINARY_DIR}"
            "-DPREFIX=${CMAKE_CURRENT_BINARY_DIR}/install-layout-test" "-DCONFIG=$<CONFIG>"
            "-DCHECK_WORDLISTS=$<TARGET_FILE:bucks-check-wordlists>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/install_layout.cmake"
)

# deploy/ passes only options bucks knows, points at the install layout and keeps the API off the network.
add_test(NAME deploy_kit
    COMMAND ${CMAKE_COMMAND} "-DBUCKS_ROOT=${CMAKE_SOURCE_DIR}" "-DBUCKS_BIN=$<TARGET_FILE:bucks>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/deploy_kit.cmake"
)
find_program(SHELLCHECK shellcheck)
if(SHELLCHECK)
    add_test(NAME shellcheck
        COMMAND ${SHELLCHECK} --severity=warning
                deploy/bootstrap-ubuntu.sh deploy/bucks-backup.sh deploy/bucks-healthcheck.sh
                tools/soak.sh tools/ci.sh
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
else()
    message(STATUS "shellcheck not found: the deploy and tool scripts are only syntax-checked")
endif()
