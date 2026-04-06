#!/bin/sh
# run-tests.sh - Master test runner for lease-sync test suite
#
# Copyright (C) 2025-2026 Pierre Gaufillet <pierre.gaufillet@bergamote.eu>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# Usage: ./run-tests.sh [options]
#   --unit      Run only unit tests (any Linux)
#   --openwrt   Run only OpenWrt integration tests
#   --all       Run all tests (default when on OpenWrt)
#   --help      Show this help
#
# Exit codes:
#   0 - All tests passed
#   1 - Some tests failed

set -e

cd "$(dirname "$0")"

# Source test helpers
. ./lib/test-helpers.sh

# ============================================
# Configuration
# ============================================

RUN_UNIT_TESTS=1
RUN_OPENWRT_TESTS=0

# Track test suite results
TOTAL_SUITES=0
TOTAL_SUITES_PASSED=0
TOTAL_SUITES_FAILED=0
TOTAL_SUITES_SKIPPED=0

# ============================================
# Helper Functions
# ============================================

show_help() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  --unit      Run only unit tests (any Linux)"
    echo "  --openwrt   Run only OpenWrt integration tests"
    echo "  --all       Run all tests (default when on OpenWrt)"
    echo "  --help      Show this help"
    echo ""
    echo "Unit tests (run on any Linux with GCC and OpenSSL):"
    echo "  - Crypto module tests"
    echo "  - Retry queue tests"
    echo "  - Utility function tests (if available)"
    echo "  - Config parsing tests (if available)"
    echo "  - Lease database tests (if available)"
    echo ""
    echo "OpenWrt integration tests (require ubus and dnsmasq):"
    echo "  - DHCP script tests"
    echo "  - ubus API tests"
}

run_test_suite() {
    local name="$1"
    local script="$2"
    local required="${3:-yes}"

    TOTAL_SUITES=$((TOTAL_SUITES + 1))

    if [ ! -x "$script" ]; then
        if [ "$required" = "yes" ]; then
            printf "${RED}✗${NC} %s - test script not found: %s\n" "$name" "$script"
            TOTAL_SUITES_FAILED=$((TOTAL_SUITES_FAILED + 1))
        else
            printf "${BLUE}○${NC} %s - skipped (script not found)\n" "$name"
            TOTAL_SUITES_SKIPPED=$((TOTAL_SUITES_SKIPPED + 1))
        fi
        return 1
    fi

    printf "\n"
    printf "Running: %s\n" "$name"
    printf "==========================================\n"

    if "$script"; then
        printf "${GREEN}✓${NC} %s - PASSED\n" "$name"
        TOTAL_SUITES_PASSED=$((TOTAL_SUITES_PASSED + 1))
        return 0
    else
        printf "${RED}✗${NC} %s - FAILED\n" "$name"
        TOTAL_SUITES_FAILED=$((TOTAL_SUITES_FAILED + 1))
        return 1
    fi
}

# ============================================
# Parse Command Line Arguments
# ============================================

while [ $# -gt 0 ]; do
    case "$1" in
        --unit)
            RUN_UNIT_TESTS=1
            RUN_OPENWRT_TESTS=0
            ;;
        --openwrt)
            RUN_UNIT_TESTS=0
            RUN_OPENWRT_TESTS=1
            ;;
        --all)
            RUN_UNIT_TESTS=1
            RUN_OPENWRT_TESTS=1
            ;;
        --help|-h)
            show_help
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
    shift
done

# Auto-detect OpenWrt
if is_openwrt && [ "$RUN_OPENWRT_TESTS" -eq 0 ] && [ "$RUN_UNIT_TESTS" -eq 1 ]; then
    # On OpenWrt with default options, run both
    RUN_OPENWRT_TESTS=1
fi

# ============================================
# Main Test Execution
# ============================================

echo "=========================================="
echo "lease-sync Test Suite"
echo "=========================================="
echo ""
echo "Platform: $(uname -s) $(uname -m)"
if is_openwrt; then
    echo "Environment: OpenWrt"
else
    echo "Environment: Standard Linux"
fi
echo ""

# ============================================
# Unit Tests (Any Linux)
# ============================================

if [ "$RUN_UNIT_TESTS" -eq 1 ]; then
    header "Unit Tests (C)"

    # Check for compiler
    if has_command gcc || has_command clang || has_command cc; then
        # Required unit tests
        run_test_suite "Crypto Module Tests" "./run-crypto-tests.sh" "yes" || true
        run_test_suite "Retry Queue Tests" "./run-retry-queue-tests.sh" "yes" || true

        # Optional unit tests (added in later sessions)
        if [ -x "./run-util-tests.sh" ]; then
            run_test_suite "Utility Function Tests" "./run-util-tests.sh" "no" || true
        fi
        if [ -x "./run-config-tests.sh" ]; then
            run_test_suite "Config Parsing Tests" "./run-config-tests.sh" "no" || true
        fi
        if [ -x "./run-lease-db-tests.sh" ]; then
            run_test_suite "Lease Database Tests" "./run-lease-db-tests.sh" "no" || true
        fi
    else
        echo "No C compiler found (gcc, clang, cc) - skipping unit tests"
        TOTAL_SUITES_SKIPPED=$((TOTAL_SUITES_SKIPPED + 3))
    fi
fi

# ============================================
# OpenWrt Integration Tests
# ============================================

if [ "$RUN_OPENWRT_TESTS" -eq 1 ]; then
    header "Integration Tests (OpenWrt)"

    if has_ubus; then
        # DHCP script tests
        run_test_suite "DHCP Script Tests" "./test-dhcp-script.sh" "no" || true

        # ubus API tests (requires dnsmasq on ubus)
        if ubus list 2>/dev/null | grep -q "^dnsmasq$"; then
            run_test_suite "ubus API Tests" "./run-all-tests.sh" "no" || true
        else
            printf "${BLUE}○${NC} ubus API Tests - skipped (dnsmasq not on ubus)\n"
            TOTAL_SUITES=$((TOTAL_SUITES + 1))
            TOTAL_SUITES_SKIPPED=$((TOTAL_SUITES_SKIPPED + 1))
        fi
    else
        echo "ubus not available - skipping OpenWrt integration tests"
        if [ "$RUN_UNIT_TESTS" -eq 0 ]; then
            echo "Run with --unit to execute unit tests on this system"
        fi
    fi
fi

# ============================================
# Final Summary
# ============================================

echo ""
echo "=========================================="
echo "Final Results"
echo "=========================================="
echo "Test Suites Run:     $TOTAL_SUITES"
echo "Test Suites Passed:  $TOTAL_SUITES_PASSED"
echo "Test Suites Failed:  $TOTAL_SUITES_FAILED"
echo "Test Suites Skipped: $TOTAL_SUITES_SKIPPED"
echo "=========================================="

if [ "$TOTAL_SUITES_FAILED" -eq 0 ]; then
    if [ "$TOTAL_SUITES_PASSED" -eq 0 ]; then
        printf "${YELLOW}No tests were run${NC}\n"
        exit 0
    else
        printf "${GREEN}All test suites passed!${NC}\n"
        exit 0
    fi
else
    printf "${RED}%d test suite(s) failed${NC}\n" "$TOTAL_SUITES_FAILED"
    exit 1
fi
