#!/bin/sh
# run-peer-sync-tests.sh - Build and run lease-sync peer sync tests
#
# Copyright (C) 2025-2026 Pierre Gaufillet <pierre.gaufillet@bergamote.eu>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# Usage: ./run-peer-sync-tests.sh
#
# Prerequisites:
#   - GCC or compatible C compiler
#   - IPv6 support in kernel (loopback)
#
# This test links against daemon/peer-sync.c to test IPv4/IPv6 dual-stack support.

set -e

cd "$(dirname "$0")"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "========================================="
echo "lease-sync Peer Sync Unit Tests"
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

# Check IPv6 loopback
if ! ip -6 addr show lo 2>/dev/null | grep -q "::1"; then
    echo "${YELLOW}Warning: IPv6 loopback may not be available${NC}"
fi

# Build test - use current directory to avoid noexec /tmp issues
BUILD_DIR="$(pwd)/.build-$$"
mkdir -p "$BUILD_DIR"

echo ""
echo "Building test_peer_sync (linking daemon/peer-sync.c, util.c, log.c, config.c)..."

# We need to link several daemon files because peer-sync.c depends on them
# We also need stub implementations for functions we don't test

$CC -o "$BUILD_DIR/test_peer_sync" \
    -Wall -Wextra -Wno-comment -Wno-unused-function -Wno-unused-variable -std=gnu99 \
    -D_GNU_SOURCE \
    -I../daemon \
    -DTEST_MODE \
    test_peer_sync.c \
    test_framework.c \
    ../daemon/peer-sync.c \
    ../daemon/util.c \
    ../daemon/log.c \
    ../daemon/config.c \
    ../daemon/crypto.c \
    -lssl -lcrypto \
    2>&1 || {
        echo "${RED}Build failed!${NC}"
        rm -rf "$BUILD_DIR"
        exit 1
    }

# Ensure the binary is executable
chmod +x "$BUILD_DIR/test_peer_sync"
echo "${GREEN}Build successful${NC}"

# Run test
echo ""
echo "Running peer sync tests..."
echo ""

"$BUILD_DIR/test_peer_sync"
TEST_RESULT=$?

# Cleanup
rm -rf "$BUILD_DIR"

exit $TEST_RESULT
