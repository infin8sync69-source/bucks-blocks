add_executable(integration_tests
    test_server_network.cpp
    test_server_auth.cpp
    test_server_mining.cpp
    test_wallet_db.cpp
    test_chain_spend.cpp
    test_chainstate.cpp
    test_mempool.cpp
    test_mempool_file.cpp
    test_miner.cpp
    test_chaindb.cpp
    test_startup.cpp
    test_p2p.cpp
)

target_include_directories(integration_tests PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests/util)
target_link_libraries(integration_tests PRIVATE GTest::gtest_main bucks_core)

add_test(NAME integration_tests COMMAND integration_tests)
