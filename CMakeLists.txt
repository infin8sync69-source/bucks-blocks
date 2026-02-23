cmake_minimum_required(VERSION 3.10)
project(Bucks)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Manual OpenSSL Setup
set(OPENSSL_INC "C:/Program Files/OpenSSL-Win64/include")
set(OPENSSL_LIBS "C:/Program Files/OpenSSL-Win64/lib/VC/x64/MD")
set(LIBCRYPTO "${OPENSSL_LIBS}/libcrypto.lib")
set(LIBSSL "${OPENSSL_LIBS}/libssl.lib")

find_package(Threads REQUIRED)


# Bucks core library
add_library(bucks_core STATIC
    src/sqlite3.c
    src/crypto.cpp
    src/merkle.cpp
    src/utxo.cpp
    src/utxodb.cpp
    src/blockdb.cpp
    src/transaction.cpp
    src/block.cpp
    src/blockchain.cpp
    src/validation.cpp
    src/wallet.cpp
    src/hd_8192.cpp
    src/server.cpp
    src/network.cpp
    src/bip_8192.cpp
    src/protocol_enc.cpp
)

target_compile_definitions(bucks_core PUBLIC _WIN32_WINNT=0x0A00)

target_include_directories(bucks_core PUBLIC ${CMAKE_SOURCE_DIR}/include ${OPENSSL_INC})
target_link_libraries(bucks_core
    PUBLIC
    ${LIBCRYPTO}
    ${LIBSSL}
    ws2_32
)

# Main executable
add_executable(bucks src/main.cpp)
target_link_libraries(bucks bucks_core Threads::Threads)

# Testing
enable_testing()
add_subdirectory(tests)

