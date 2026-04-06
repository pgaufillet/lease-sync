#!/bin/sh
# run-config-tests.sh - Build and run lease-sync configuration parsing tests
#
# Copyright (C) 2025-2026 Pierre Gaufillet <pierre.gaufillet@bergamote.eu>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# Usage: ./run-config-tests.sh
#
# Prerequisites:
#   - GCC or compatible C compiler
#   - OpenSSL development libraries (libssl-dev)
#
# This test links against daemon/config.c, daemon/util.c, daemon/crypto.c, and daemon/log.c.

set -e

cd "$(dirname "$0")"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "========================================="
echo "lease-sync Configuration Parsing Tests"
echo "========================================="
echo ""

# Check for compiler
if command -v gcc >/dev/null 2>&1; then
    CC=gcc
elif command -v clang >/dev/null 2>&1; then
    CC=clang
elif command -v cc >/dev/null 2>&1; then
    CC=cc
else
    echo "${RED}Error: No C compiler found (gcc, clang, or cc)${NC}"
    exit 1
fi
echo "Using compiler: $CC"

# Check for OpenSSL (required by crypto.c which is needed by config.c)
if ! pkg-config --exists openssl 2>/dev/null; then
    echo "${YELLOW}Warning: pkg-config --exists openssl failed${NC}"
    echo "Trying to compile anyway..."
    OPENSSL_CFLAGS=""
    OPENSSL_LIBS="-lssl -lcrypto"
else
    OPENSSL_CFLAGS=$(pkg-config --cflags openssl)
    OPENSSL_LIBS=$(pkg-config --libs openssl)
    echo "OpenSSL found via pkg-config"
fi

# Build test - use current directory to avoid noexec /tmp issues
BUILD_DIR="$(pwd)/.build-$$"
mkdir -p "$BUILD_DIR"

echo ""
echo "Building test_config (linking daemon/config.c, util.c, crypto.c, log.c)..."
$CC -o "$BUILD_DIR/test_config" \
    -Wall -Wextra -Wno-comment -std=gnu99 \
    -I../daemon \
    $OPENSSL_CFLAGS \
    test_config.c \
    test_framework.c \
    ../daemon/config.c \
    ../daemon/util.c \
    ../daemon/crypto.c \
    ../daemon/log.c \
    $OPENSSL_LIBS

if [ $? -ne 0 ]; then
    echo "${RED}Build failed!${NC}"
    rm -rf "$BUILD_DIR"
    exit 1
fi

# Ensure the binary is executable
chmod +x "$BUILD_DIR/test_config"
echo "${GREEN}Build successful${NC}"

# Run test
echo ""
echo "Running configuration parsing tests..."
echo ""

"$BUILD_DIR/test_config"
TEST_RESULT=$?

# Cleanup
rm -rf "$BUILD_DIR"

exit $TEST_RESULT
