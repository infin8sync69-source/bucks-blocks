add_executable(system_tests
    test_full_node.cpp
)

target_include_directories(system_tests PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests/util)
target_link_libraries(system_tests PRIVATE GTest::gtest_main bucks_core)

add_test(NAME system_tests COMMAND system_tests)
