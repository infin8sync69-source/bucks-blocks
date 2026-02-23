add_executable(integration_tests
    test_server_network.cpp
    test_wallet_db.cpp
)

target_include_directories(integration_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(integration_tests PRIVATE GTest::gtest_main bucks_core)

add_test(NAME integration_tests COMMAND integration_tests)
