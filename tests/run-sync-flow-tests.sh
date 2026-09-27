#!/bin/sh
# run-sync-flow-tests.sh - Build and run lease-sync sync flow tests
#
# Copyright (C) 2026 Pierre Gaufillet <pierre.gaufillet@bergamote.eu>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# Usage: ./run-sync-flow-tests.sh
#
# Prerequisites:
#   - GCC or compatible C compiler
#   - OpenSSL development libraries
#   - IPv4 loopback (the fake peer talks to the daemon over UDP)
#
# Builds the daemon's lease-sync.c, ubus-handler.c, peer-sync.c,
# lease-db.c and retry-queue.c against the test stand-ins in stubs/
# (libubox/libubus are not needed).

set -e

cd "$(dirname "$0")"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

echo "========================================="
echo "lease-sync Sync Flow Tests"
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

# Build test - use current directory to avoid noexec /tmp issues
BUILD_DIR="$(pwd)/.build-$$"
mkdir -p "$BUILD_DIR"

echo ""
echo "Building test_sync_flow..."
$CC -o "$BUILD_DIR/test_sync_flow" \
    -Wall -Wextra -Wno-comment -Wno-unused-function \
    -Wno-format-zero-length -Wno-format-truncation -std=gnu99 \
    -D_GNU_SOURCE \
    -Istubs -I../daemon \
    test_sync_flow.c \
    test_framework.c \
    ../daemon/peer-sync.c \
    ../daemon/lease-db.c \
    ../daemon/retry-queue.c \
    ../daemon/config.c \
    ../daemon/crypto.c \
    ../daemon/util.c \
    ../daemon/log.c \
    -lssl -lcrypto \
    2>&1 || {
        echo "${RED}Build failed!${NC}"
        rm -rf "$BUILD_DIR"
        exit 1
    }
echo "${GREEN}Build successful${NC}"

# Run test
echo ""
echo "Running sync flow tests..."
echo ""

TEST_RESULT=0
"$BUILD_DIR/test_sync_flow" || TEST_RESULT=$?

# Cleanup
rm -rf "$BUILD_DIR"

exit $TEST_RESULT
