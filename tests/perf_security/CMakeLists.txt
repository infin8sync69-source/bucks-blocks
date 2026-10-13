add_executable(perf_security_tests
    benchmark_hash.cpp
    fuzz_protocol.cpp
    fuzz_parsers.cpp
)

target_include_directories(perf_security_tests PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests/unit)
target_link_libraries(perf_security_tests PRIVATE GTest::gtest_main bucks_core)

add_test(NAME perf_security_tests COMMAND perf_security_tests)
