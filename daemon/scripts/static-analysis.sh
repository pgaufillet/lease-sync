#!/bin/sh
# static-analysis.sh - Static analysis for lease-sync daemon
#
# This script runs cppcheck on the lease-sync source code to detect potential issues:
# - Memory leaks
# - Null pointer dereferences
# - Buffer overflows
# - Uninitialized variables
# - Resource leaks (file handles, sockets)
# - OpenSSL API misuse
# - ubus memory management issues
#
# OPTIONAL: This script gracefully skips if cppcheck is not installed.
#
# Copyright (C) 2025-2026 Pierre Gaufillet <pierre.gaufillet@bergamote.eu>
#
# Usage: ./scripts/static-analysis.sh [--strict]
#   --strict: Treat warnings as errors (exit code 1 on any issue)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Parse arguments
STRICT_MODE=false
for arg in "$@"; do
    case "$arg" in
        --strict)
            STRICT_MODE=true
            ;;
    esac
done

echo "=========================================="
echo "Static Analysis: lease-sync daemon"
echo "=========================================="

# Check if cppcheck is available
if ! command -v cppcheck >/dev/null 2>&1; then
    echo "${YELLOW}SKIP: cppcheck not installed (optional)${NC}"
    echo "Install with: apt install cppcheck (Debian/Ubuntu)"
    echo "             brew install cppcheck (macOS)"
    exit 0
fi

CPPCHECK_VERSION=$(cppcheck --version 2>/dev/null | head -1)
echo "Using: $CPPCHECK_VERSION"
echo ""

# Verify source files exist
if [ ! -f "$PROJECT_DIR/lease-sync.c" ]; then
    echo "${RED}ERROR: Source files not found in: $PROJECT_DIR${NC}"
    exit 1
fi

# Run cppcheck with comprehensive checks
echo "Running cppcheck..."
echo ""

# Build the file list (lease-sync has all .c and .h files in the daemon directory)
SOURCE_FILES=""
for f in "$PROJECT_DIR"/*.c "$PROJECT_DIR"/*.h; do
    if [ -f "$f" ]; then
        SOURCE_FILES="$SOURCE_FILES $f"
    fi
done

CPPCHECK_ARGS="--enable=all"
CPPCHECK_ARGS="$CPPCHECK_ARGS --suppress=missingIncludeSystem"
CPPCHECK_ARGS="$CPPCHECK_ARGS --suppress=unusedFunction"
CPPCHECK_ARGS="$CPPCHECK_ARGS --inline-suppr"
CPPCHECK_ARGS="$CPPCHECK_ARGS --std=c11"
CPPCHECK_ARGS="$CPPCHECK_ARGS --force"

# Suppress some OpenWrt-specific false positives
CPPCHECK_ARGS="$CPPCHECK_ARGS --suppress=constParameterPointer"
CPPCHECK_ARGS="$CPPCHECK_ARGS --suppress=constParameterCallback"

# Error exit code for strict mode
if [ "$STRICT_MODE" = "true" ]; then
    CPPCHECK_ARGS="$CPPCHECK_ARGS --error-exitcode=1"
fi

# Run cppcheck
cppcheck $CPPCHECK_ARGS $SOURCE_FILES 2>&1 | while IFS= read -r line; do
    # Colorize output
    case "$line" in
        *error:*)
            printf "${RED}%s${NC}\n" "$line"
            ;;
        *warning:*)
            printf "${YELLOW}%s${NC}\n" "$line"
            ;;
        *style:*|*performance:*)
            printf "%s\n" "$line"
            ;;
        *)
            printf "%s\n" "$line"
            ;;
    esac
done

RESULT=$?

echo ""
echo "=========================================="
if [ $RESULT -eq 0 ]; then
    echo "${GREEN}Static analysis completed successfully${NC}"
else
    echo "${RED}Static analysis found issues${NC}"
fi
echo "=========================================="

# Additional manual checks for common issues
echo ""
echo "Manual checks for common patterns:"
echo "----------------------------------"

# Check for potential memory leaks in crypto.c
echo -n "Checking crypto.c for EVP_CIPHER_CTX cleanup... "
if grep -q "EVP_CIPHER_CTX_free" "$PROJECT_DIR/crypto.c" 2>/dev/null; then
    echo "${GREEN}OK${NC}"
else
    echo "${YELLOW}WARN: EVP_CIPHER_CTX_free not found${NC}"
fi

# Check for socket cleanup on error paths
echo -n "Checking for close() calls after socket errors... "
SOCKET_CREATES=$(grep -c "socket(" "$PROJECT_DIR"/*.c 2>/dev/null || echo "0")
CLOSE_CALLS=$(grep -c "close(" "$PROJECT_DIR"/*.c 2>/dev/null || echo "0")
if [ "$CLOSE_CALLS" -ge "$SOCKET_CREATES" ]; then
    echo "${GREEN}OK ($CLOSE_CALLS close calls for $SOCKET_CREATES socket calls)${NC}"
else
    echo "${YELLOW}WARN: Fewer close() than socket() calls${NC}"
fi

# Check retry-queue.c for malloc/free balance
echo -n "Checking retry-queue.c for malloc/free balance... "
if [ -f "$PROJECT_DIR/retry-queue.c" ]; then
    MALLOCS=$(grep -c "malloc\|calloc\|strdup" "$PROJECT_DIR/retry-queue.c" 2>/dev/null || echo "0")
    FREES=$(grep -c "free(" "$PROJECT_DIR/retry-queue.c" 2>/dev/null || echo "0")
    echo "${GREEN}$MALLOCS allocations, $FREES frees${NC}"
fi

exit $RESULT
