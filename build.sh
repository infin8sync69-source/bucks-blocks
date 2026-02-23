#!/bin/bash
# Simple build script for Bucks blockchain

set -e

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

mkdir -p "$BUILD_DIR"

# Compile source files
echo "Compiling source files..."
g++ -std=c++17 -I"$INCLUDE_DIR" -I"$OPENSSL_INCLUDE" \
    -c "$SRC_DIR/crypto.cpp" -o "$BUILD_DIR/crypto.o"
g++ -std=c++17 -I"$INCLUDE_DIR" -I"$OPENSSL_INCLUDE" \
    -c "$SRC_DIR/utxo.cpp" -o "$BUILD_DIR/utxo.o"
g++ -std=c++17 -I"$INCLUDE_DIR" -I"$OPENSSL_INCLUDE" \
    -c "$SRC_DIR/transaction.cpp" -o "$BUILD_DIR/transaction.o"
g++ -std=c++17 -I"$INCLUDE_DIR" -I"$OPENSSL_INCLUDE" \
    -c "$SRC_DIR/merkle.cpp" -o "$BUILD_DIR/merkle.o"
g++ -std=c++17 -I"$INCLUDE_DIR" -I"$OPENSSL_INCLUDE" \
    -c "$SRC_DIR/block.cpp" -o "$BUILD_DIR/block.o"
g++ -std=c++17 -I"$INCLUDE_DIR" -I"$OPENSSL_INCLUDE" \
    -c "$SRC_DIR/blockchain.cpp" -o "$BUILD_DIR/blockchain.o"
g++ -std=c++17 -I"$INCLUDE_DIR" -I"$OPENSSL_INCLUDE" \
    -c "$SRC_DIR/wallet.cpp" -o "$BUILD_DIR/wallet.o"
g++ -std=c++17 -I"$INCLUDE_DIR" -I"$OPENSSL_INCLUDE" \
    -c "$SRC_DIR/server.cpp" -o "$BUILD_DIR/server.o"
g++ -std=c++17 -I"$INCLUDE_DIR" -I"$OPENSSL_INCLUDE" \
    -c "$SRC_DIR/network.cpp" -o "$BUILD_DIR/network.o"
g++ -std=c++17 -I"$INCLUDE_DIR" -I"$OPENSSL_INCLUDE" \
    -c "$SRC_DIR/main.cpp" -o "$BUILD_DIR/main.o"

# Link executable
echo "Linking executable..."
g++ -std=c++17 \
    "$BUILD_DIR/crypto.o" \
    "$BUILD_DIR/utxo.o" \
    "$BUILD_DIR/transaction.o" \
    "$BUILD_DIR/merkle.o" \
    "$BUILD_DIR/block.o" \
    "$BUILD_DIR/blockchain.o" \
    "$BUILD_DIR/wallet.o" \
    "$BUILD_DIR/server.o" \
    "$BUILD_DIR/network.o" \
    "$BUILD_DIR/main.o" \
    -L"$OPENSSL_LIB" -lssl -lcrypto \
    -o "$BUILD_DIR/bucks"

echo "Build complete! Executable: $BUILD_DIR/bucks"
