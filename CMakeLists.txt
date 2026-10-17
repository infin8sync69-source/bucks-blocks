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
# OFF builds only the node and its tools: no googletest download, nothing for ctest. CI and
# developers keep the default.
option(BUCKS_BUILD_TESTS "Build the test suites (fetches googletest v1.14.0)" ON)
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

# Fuzz targets (tests/fuzz). With a clang that has the libFuzzer runtime the whole library is
# instrumented for coverage (fuzzer-no-link) and sanitized; otherwise the targets only replay
# their corpus. Apple's clang has no libFuzzer; Homebrew LLVM and Linux clang do.
option(BUCKS_FUZZ "Build the fuzz targets in tests/fuzz" OFF)
if(BUCKS_FUZZ)
    if(NOT BUCKS_BUILD_TESTS)
        message(FATAL_ERROR "BUCKS_FUZZ needs BUCKS_BUILD_TESTS (the fuzz targets live in tests/)")
    endif()
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer")
    check_cxx_source_compiles(
        "extern \"C\" int LLVMFuzzerTestOneInput(const unsigned char*, unsigned long) { return 0; }"
        BUCKS_HAVE_LIBFUZZER)
    unset(CMAKE_REQUIRED_FLAGS)
    if(BUCKS_HAVE_LIBFUZZER)
        if(NOT BUCKS_SANITIZE)
            set(BUCKS_SANITIZE "address,undefined" CACHE STRING "" FORCE)
        endif()
        add_compile_options(-fsanitize=fuzzer-no-link)
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

# Vendored SQLite: same definitions as build.sh (checked by build_hygiene).
set_source_files_properties(src/sqlite3.c PROPERTIES COMPILE_DEFINITIONS
    "SQLITE_THREADSAFE=1;SQLITE_DQS=0;SQLITE_OMIT_LOAD_EXTENSION")

# Bucks core library
add_library(bucks_core STATIC
    src/sqlite3.c
    src/crypto.cpp
    src/serialize.cpp
    src/u256.cpp
    src/chainparams.cpp
    src/sighash.cpp
    src/chain.cpp
    src/policy.cpp
    src/fsutil.cpp
    src/mempool_file.cpp
    src/chaindb.cpp
    src/node.cpp
    src/miner.cpp
    src/coins.cpp
    src/undo.cpp
    src/merkle.cpp
    src/transaction.cpp
    src/block.cpp
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
    src/chainstate.cpp
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

add_executable(bucks-check-wordlists tools/check_wordlists.cpp)
target_link_libraries(bucks-check-wordlists bucks_core)

# Install layout: <prefix>/bin/{bucks,bucks-mine-genesis} and <prefix>/share/bucks/{web,wordlists}.
# When the source tree it was built from is gone, bucks looks for its UI and word lists at
# <exe dir>/../share/bucks/, so these paths are fixed (not GNUInstallDirs) to always match it.
install(TARGETS bucks bucks-mine-genesis RUNTIME DESTINATION bin)
install(DIRECTORY web/ DESTINATION share/bucks/web
    PATTERN test EXCLUDE
    PATTERN node_modules EXCLUDE
    PATTERN package.json EXCLUDE)
# Only the lists the manifest names (not wordlists/review/ or retired lists), so an installed
# node reads exactly the bytes the source tree pins.
file(READ "${CMAKE_SOURCE_DIR}/wordlists/manifest.json" bucks_manifest)
string(REGEX MATCHALL "\"file\" *: *\"[^\"]+\"" bucks_manifest_files "${bucks_manifest}")
set(bucks_wordlist_files "${CMAKE_SOURCE_DIR}/wordlists/manifest.json")
foreach(entry IN LISTS bucks_manifest_files)
    string(REGEX REPLACE ".*\"([^\"]+)\"$" "\\1" list_file "${entry}")
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/wordlists/${list_file}")
        message(FATAL_ERROR "wordlists/manifest.json names ${list_file}, which does not exist")
    endif()
    list(APPEND bucks_wordlist_files "${CMAKE_SOURCE_DIR}/wordlists/${list_file}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS wordlists/manifest.json)
install(FILES ${bucks_wordlist_files} DESTINATION share/bucks/wordlists)

# Testing
if(BUCKS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
