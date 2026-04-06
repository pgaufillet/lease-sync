#!/bin/sh
# test-helpers.sh - Shared test helper functions for lease-sync test suite
#
# Copyright (C) 2025-2026 Pierre Gaufillet <pierre.gaufillet@bergamote.eu>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# Usage: Source this file in test scripts:
#   . ./lib/test-helpers.sh

# ============================================
# Color Codes (ANSI escape sequences)
# ============================================
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'  # No Color

# ============================================
# Test Counters
# ============================================
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_WARNINGS=0
TESTS_SKIPPED=0

# ============================================
# Test Result Functions
# ============================================

# Mark a test as passed
# Usage: pass "Test description"
pass() {
    printf "${GREEN}✓${NC} %s\n" "$1"
    TESTS_PASSED=$((TESTS_PASSED + 1))
}

# Mark a test as failed
# Usage: fail "Test description"
fail() {
    printf "${RED}✗${NC} %s\n" "$1"
    TESTS_FAILED=$((TESTS_FAILED + 1))
}

# Mark a test with a warning
# Usage: warn "Warning message"
warn() {
    printf "${YELLOW}⚠${NC} %s\n" "$1"
    TESTS_WARNINGS=$((TESTS_WARNINGS + 1))
}

# Mark a test as skipped
# Usage: skip "Test description" "Reason"
skip() {
    printf "${BLUE}○${NC} %s (skipped: %s)\n" "$1" "$2"
    TESTS_SKIPPED=$((TESTS_SKIPPED + 1))
}

# Print informational message
# Usage: info "Message"
info() {
    printf "${YELLOW}→${NC} %s\n" "$1"
}

# ============================================
# Section Headers
# ============================================

# Print a test section header
# Usage: header "Section Name"
header() {
    printf "\n"
    printf "==========================================\n"
    printf "%s\n" "$1"
    printf "==========================================\n"
}

# Print a subsection header
# Usage: subheader "Subsection Name"
subheader() {
    printf "\n--- %s ---\n" "$1"
}

# ============================================
# Test Summary
# ============================================

# Print test summary and return appropriate exit code
# Usage: summary
# Returns: 0 if all tests passed, 1 if any failed
summary() {
    printf "\n"
    printf "==========================================\n"
    printf "Test Summary\n"
    printf "==========================================\n"
    printf "Passed:   %d\n" "$TESTS_PASSED"
    printf "Failed:   %d\n" "$TESTS_FAILED"
    printf "Warnings: %d\n" "$TESTS_WARNINGS"
    printf "Skipped:  %d\n" "$TESTS_SKIPPED"
    printf "Total:    %d\n" "$((TESTS_PASSED + TESTS_FAILED + TESTS_SKIPPED))"

    if [ "$TESTS_FAILED" -eq 0 ]; then
        printf "${GREEN}All tests passed!${NC}\n"
        return 0
    else
        printf "${RED}Some tests failed!${NC}\n"
        return 1
    fi
}

# ============================================
# Assertion Helpers
# ============================================

# Assert that a command succeeds (returns 0)
# Usage: assert_success "Test name" command arg1 arg2 ...
assert_success() {
    local test_name="$1"
    shift
    if "$@" >/dev/null 2>&1; then
        pass "$test_name"
    else
        fail "$test_name (command failed: $*)"
    fi
}

# Assert that a command fails (returns non-zero)
# Usage: assert_failure "Test name" command arg1 arg2 ...
assert_failure() {
    local test_name="$1"
    shift
    if "$@" >/dev/null 2>&1; then
        fail "$test_name (expected failure but succeeded)"
    else
        pass "$test_name"
    fi
}

# Assert that two strings are equal
# Usage: assert_equal "Test name" "expected" "actual"
assert_equal() {
    local test_name="$1"
    local expected="$2"
    local actual="$3"
    if [ "$expected" = "$actual" ]; then
        pass "$test_name"
    else
        fail "$test_name (expected: '$expected', got: '$actual')"
    fi
}

# Assert that a string contains a substring
# Usage: assert_contains "Test name" "haystack" "needle"
assert_contains() {
    local test_name="$1"
    local haystack="$2"
    local needle="$3"
    case "$haystack" in
        *"$needle"*)
            pass "$test_name"
            ;;
        *)
            fail "$test_name (expected '$haystack' to contain '$needle')"
            ;;
    esac
}

# Assert that a file exists
# Usage: assert_file_exists "Test name" "/path/to/file"
assert_file_exists() {
    local test_name="$1"
    local file_path="$2"
    if [ -f "$file_path" ]; then
        pass "$test_name"
    else
        fail "$test_name (file not found: $file_path)"
    fi
}

# Assert that a file does not exist
# Usage: assert_file_not_exists "Test name" "/path/to/file"
assert_file_not_exists() {
    local test_name="$1"
    local file_path="$2"
    if [ ! -f "$file_path" ]; then
        pass "$test_name"
    else
        fail "$test_name (file exists but should not: $file_path)"
    fi
}

# ============================================
# Platform Detection
# ============================================

# Check if running on OpenWrt
# Usage: if is_openwrt; then ... fi
is_openwrt() {
    [ -f /etc/openwrt_release ]
}

# Check if ubus is available
# Usage: if has_ubus; then ... fi
has_ubus() {
    command -v ubus >/dev/null 2>&1
}

# Check if a command exists
# Usage: if has_command "gcc"; then ... fi
has_command() {
    command -v "$1" >/dev/null 2>&1
}

# ============================================
# Utility Functions
# ============================================

# Create a temporary directory and echo its path
# Usage: TMPDIR=$(make_temp_dir)
make_temp_dir() {
    mktemp -d 2>/dev/null || {
        local dir="/tmp/test-$$-$(date +%s)"
        mkdir -p "$dir"
        echo "$dir"
    }
}

# Clean up a temporary directory
# Usage: cleanup_temp_dir "$TMPDIR"
cleanup_temp_dir() {
    if [ -n "$1" ] && [ -d "$1" ]; then
        rm -rf "$1"
    fi
}

# Reset test counters (useful for running multiple test suites)
# Usage: reset_counters
reset_counters() {
    TESTS_PASSED=0
    TESTS_FAILED=0
    TESTS_WARNINGS=0
    TESTS_SKIPPED=0
}
