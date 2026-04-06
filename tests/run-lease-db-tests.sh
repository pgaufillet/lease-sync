#!/bin/sh
# run-lease-db-tests.sh - Build and run lease-sync lease database tests
#
# Copyright (C) 2025-2026 Pierre Gaufillet <pierre.gaufillet@bergamote.eu>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# Usage: ./run-lease-db-tests.sh
#
# Prerequisites:
#   - GCC or compatible C compiler
#
# This test links against daemon/lease-db.c and daemon/util.c.

set -e

cd "$(dirname "$0")"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "========================================="
echo "lease-sync Lease Database Unit Tests"
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
echo "Building test_lease_db (linking daemon/lease-db.c, util.c, log.c)..."
$CC -o "$BUILD_DIR/test_lease_db" \
    -Wall -Wextra -Wno-comment -std=gnu99 \
    -I../daemon \
    test_lease_db.c \
    test_framework.c \
    ../daemon/lease-db.c \
    ../daemon/util.c \
    ../daemon/log.c

if [ $? -ne 0 ]; then
    echo "${RED}Build failed!${NC}"
    rm -rf "$BUILD_DIR"
    exit 1
fi

# Ensure the binary is executable
chmod +x "$BUILD_DIR/test_lease_db"
echo "${GREEN}Build successful${NC}"

# Run test
echo ""
echo "Running lease database tests..."
echo ""

"$BUILD_DIR/test_lease_db"
TEST_RESULT=$?

# Cleanup
rm -rf "$BUILD_DIR"

exit $TEST_RESULT
