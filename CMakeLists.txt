cmake_minimum_required(VERSION 3.14)
project(Bucks)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to an optimized build with debug info. Multi-config generators
# (Xcode, Visual Studio) choose the configuration at build time instead.
get_property(BUCKS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT BUCKS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Build type: Debug, Release, RelWithDebInfo or MinSizeRel" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

option(BUCKS_WERROR "Treat warnings in Bucks C++ code as errors" OFF)
set(BUCKS_SANITIZE "" CACHE STRING
    "Sanitizers for every target, e.g. address,undefined (empty = none)")

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# Hardening for every target, including the vendored sqlite3.c and googletest.
# Windows (MinGW) is left on toolchain defaults.
if(NOT WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    include(CheckPIESupported)
    check_pie_supported()
    add_compile_options(-fstack-protector-strong)
    # FORTIFY needs optimization, and it hides calls from ASan/TSan/MSan
    # interceptors.
    if(NOT BUCKS_SANITIZE MATCHES "address|thread|memory")
        add_compile_options(
            "$<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE>"
            "$<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=2>"
        )
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_link_options(LINKER:-z,relro LINKER:-z,now)
    endif()
endif()

# Sanitizers apply to every target so googletest and sqlite3 are instrumented
# too. Findings abort the process, so ctest fails instead of just logging.
if(BUCKS_SANITIZE)
    add_compile_options(
        -fsanitize=${BUCKS_SANITIZE}
        -fno-sanitize-recover=all
        -fno-omit-frame-pointer
    )
    add_link_options(-fsanitize=${BUCKS_SANITIZE})
endif()

# Warnings for Bucks' own C++ code. bucks_core passes these on to everything
# that links it (the node and the test binaries); sqlite3.c (C) and
# googletest keep their own flags.
add_library(bucks_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(bucks_warnings INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall>
        $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
    )
    if(BUCKS_WERROR)
        target_compile_options(bucks_warnings INTERFACE
            $<$<COMPILE_LANGUAGE:CXX>:-Werror>)
    endif()
elseif(MSVC)
    target_compile_options(bucks_warnings INTERFACE /W4)
    if(BUCKS_WERROR)
        target_compile_options(bucks_warnings INTERFACE /WX)
    endif()
endif()

# Bucks core library
add_library(bucks_core STATIC
    src/sqlite3.c
    src/crypto.cpp
    src/serialize.cpp
    src/u256.cpp
    src/chainparams.cpp
    src/sighash.cpp
    src/chain.cpp
    src/coins.cpp
    src/undo.cpp
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
    src/genesis.cpp
    src/validation_rules.cpp
    src/gold_rules.cpp
    src/connect_block.cpp
    src/pow.cpp
    src/mempool.cpp
    src/script.cpp
)

if(WIN32)
    target_compile_definitions(bucks_core PUBLIC _WIN32_WINNT=0x0A00)
endif()

target_include_directories(bucks_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
# Lets the mnemonic code (and tests) find wordlists/ in the source tree.
target_compile_definitions(bucks_core PUBLIC BUCKS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(bucks_core
    PUBLIC
    OpenSSL::Crypto
    OpenSSL::SSL
    $<BUILD_INTERFACE:bucks_warnings>
)

if(WIN32)
    target_link_libraries(bucks_core PUBLIC ws2_32)
endif()

# Main executable
add_executable(bucks src/main.cpp)
target_link_libraries(bucks bucks_core Threads::Threads)

# Offline genesis miner (not part of the node)
add_executable(bucks-mine-genesis tools/mine_genesis.cpp)
target_link_libraries(bucks-mine-genesis bucks_core Threads::Threads)

# Testing
enable_testing()
add_subdirectory(tests)
