add_executable(unit_tests
    test_build_config.cpp
    test_serialize.cpp
    test_u256.cpp
    test_pow.cpp
    test_chainparams.cpp
    test_genesis.cpp
    test_script_templates.cpp
    test_sighash.cpp
    test_chain_index.cpp
    test_coins.cpp
    test_undo.cpp
    test_crypto.cpp
    test_crypto_ecdsa.cpp
    test_block.cpp
    test_merkle.cpp
    test_transaction.cpp
    test_hd_8192.cpp
    test_bip_8192.cpp
    test_wallet.cpp
    test_protocol_enc.cpp
    test_network.cpp
)

target_include_directories(unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(unit_tests PRIVATE GTest::gtest_main bucks_core)

add_test(NAME unit_tests COMMAND unit_tests)
