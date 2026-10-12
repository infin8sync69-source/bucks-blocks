#!/bin/bash
# Simple build script for Bucks blockchain.
# CMake is the primary build; keep the source list and flags here in sync with
# CMakeLists.txt (tests/build_hygiene.cmake checks this).

set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

BUILD_DIR="build"
SRC_DIR="src"
INCLUDE_DIR="include"

echo "Building Bucks blockchain..."

# Find OpenSSL library paths
OPENSSL_BREW=$(brew --prefix openssl 2>/dev/null || echo "")

if [ -d "/opt/homebrew/opt/openssl" ]; then
    # Homebrew on Apple Silicon
    OPENSSL_INCLUDE="/opt/homebrew/opt/openssl/include"
    OPENSSL_LIB="/opt/homebrew/opt/openssl/lib"
elif [ -n "$OPENSSL_BREW" ] && [ -d "$OPENSSL_BREW/include" ]; then
    # Use brew --prefix result
    OPENSSL_INCLUDE="$OPENSSL_BREW/include"
    OPENSSL_LIB="$OPENSSL_BREW/lib"
elif [ -d "/usr/local/opt/openssl@3/include" ]; then
    # Homebrew OpenSSL 3.x on Intel Mac
    OPENSSL_INCLUDE="/usr/local/opt/openssl@3/include"
    OPENSSL_LIB="/usr/local/opt/openssl@3/lib"
elif [ -d "/usr/local/opt/openssl/include" ]; then
    # Homebrew on Intel Mac
    OPENSSL_INCLUDE="/usr/local/opt/openssl/include"
    OPENSSL_LIB="/usr/local/opt/openssl/lib"
else
    # System OpenSSL
    OPENSSL_INCLUDE="/usr/include"
    OPENSSL_LIB="/usr/lib"
fi

echo "Using OpenSSL include: $OPENSSL_INCLUDE"
echo "Using OpenSSL lib: $OPENSSL_LIB"

# Same as CMake's default RelWithDebInfo build plus its hardening flags.
OPT_FLAGS=(-O2 -g -DNDEBUG)
HARDEN_FLAGS=(-fstack-protector-strong -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2 -fPIE)
LINK_FLAGS=(-pie -fstack-protector-strong)
if [ "$(uname -s)" = "Linux" ]; then
    LINK_FLAGS+=(-Wl,-z,relro,-z,now)
fi
CXX_FLAGS=(-std=c++17 -Wall -Wextra "-DBUCKS_SOURCE_DIR=\"$ROOT_DIR\"")

mkdir -p "$BUILD_DIR"

# Compile C source (sqlite3)
echo "Compiling sqlite3..."
cc "${OPT_FLAGS[@]}" "${HARDEN_FLAGS[@]}" \
    -c "$SRC_DIR/sqlite3.c" -o "$BUILD_DIR/sqlite3.o" -DSQLITE_THREADSAFE=1

# Compile all C++ source files (same order as bucks_core in CMakeLists.txt,
# plus main for the executable)
echo "Compiling source files..."
CPP_SOURCES=(
    crypto serialize u256 chainparams sighash chain coins undo merkle utxo utxodb blockdb transaction block blockchain validation
    wallet hd_8192 server network bip_8192 protocol_enc genesis validation_rules gold_rules connect_block chainstate pow mempool
    script
    main
)
for src in "${CPP_SOURCES[@]}"; do
    echo "  $src.cpp"
    g++ "${CXX_FLAGS[@]}" "${OPT_FLAGS[@]}" "${HARDEN_FLAGS[@]}" \
        -I"$INCLUDE_DIR" -I"$OPENSSL_INCLUDE" \
        -c "$SRC_DIR/${src}.cpp" -o "$BUILD_DIR/${src}.o"
done

# Link executable
echo "Linking executable..."
OBJ_FILES=()
for src in "${CPP_SOURCES[@]}"; do
    OBJ_FILES+=("$BUILD_DIR/${src}.o")
done
g++ -std=c++17 "${LINK_FLAGS[@]}" \
    "${OBJ_FILES[@]}" \
    "$BUILD_DIR/sqlite3.o" \
    -L"$OPENSSL_LIB" -lssl -lcrypto \
    -o "$BUILD_DIR/bucks"

echo "Build complete! Executable: $BUILD_DIR/bucks"
