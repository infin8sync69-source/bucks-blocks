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

FetchContent_MakeAvailable(googletest)
include(GoogleTest)

add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(system)
add_subdirectory(perf_security)

add_custom_target(tests ALL
    DEPENDS unit_tests integration_tests system_tests perf_security_tests
)

# Checks that build.sh mirrors CMakeLists.txt and .gitignore covers secrets.
add_test(NAME build_hygiene
    COMMAND ${CMAKE_COMMAND} "-DBUCKS_ROOT=${CMAKE_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/build_hygiene.cmake"
)

# Register a meta-test invocation if needed, or rely on individual add_test calls
