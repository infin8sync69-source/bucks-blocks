add_executable(unit_tests
    test_crypto.cpp
    test_block.cpp
    test_transaction.cpp
    test_hd_8192.cpp
    test_bip_8192.cpp
    test_protocol_enc.cpp
    test_network.cpp
)

target_include_directories(unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(unit_tests PRIVATE GTest::gtest_main bucks_core)

add_test(NAME unit_tests COMMAND unit_tests)
