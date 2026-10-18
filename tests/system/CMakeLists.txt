add_executable(system_tests
    test_full_node.cpp
    test_cli.cpp
)

target_include_directories(system_tests PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests/util)
target_link_libraries(system_tests PRIVATE GTest::gtest_main bucks_core)
# test_cli.cpp runs the node executable itself.
add_dependencies(system_tests bucks)
target_compile_definitions(system_tests PRIVATE BUCKS_NODE_BINARY="$<TARGET_FILE:bucks>")

add_test(NAME system_tests COMMAND system_tests)
