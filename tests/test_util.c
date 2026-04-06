/* test_util.c
/*
 * Copyright (C) 2025-2026 Pierre Gaufillet <pierre.gaufillet@bergamote.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
/*
 * lease-sync - DHCP Lease Synchronization Daemon
 * test_util.c - Unit tests for utility functions
 *
 * Build: See run-util-tests.sh
 * Run: ./run-util-tests.sh
 *
 * This test links against the actual daemon/util.c code.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <syslog.h>
#include <unistd.h>

/* Include the daemon's common.h for real structure definitions */
#include "../daemon/common.h"

#include "test_framework.h"

/* ============================================
 * Global State for Testing
 * ============================================ */
static struct daemon_state state_storage;
struct daemon_state *g_state = NULL;

static void reset_state(void)
{
    memset(&state_storage, 0, sizeof(state_storage));
    g_state = &state_storage;
    g_state->config.log_level = LOG_LEVEL_ERROR;  /* Suppress debug logs */
}

/* ============================================
 * Test: get_timestamp_ms
 * ============================================ */
void test_get_timestamp_ms(void)
{
    printf("\n=== Test: get_timestamp_ms ===\n");

    reset_state();

    /* Test that timestamp returns a reasonable value */
    uint64_t ts1 = get_timestamp_ms();
    if (ts1 > 0) {
        TEST_PASS("Timestamp returns non-zero value");
    } else {
        TEST_FAIL("Timestamp returns non-zero value", "returned 0");
    }

    /* Test that timestamp is monotonically increasing */
    usleep(1000);  /* Wait 1ms */
    uint64_t ts2 = get_timestamp_ms();
    if (ts2 >= ts1) {
        TEST_PASS("Timestamp is monotonically increasing");
    } else {
        TEST_FAIL("Timestamp is monotonically increasing", "timestamp decreased");
    }

    /* Test that timestamp is in reasonable range (since 2020) */
    /* 2020-01-01 00:00:00 UTC = 1577836800 seconds = 1577836800000 ms */
    uint64_t min_expected = 1577836800000ULL;
    if (ts1 > min_expected) {
        TEST_PASS("Timestamp is in reasonable range (> 2020)");
    } else {
        TEST_FAIL("Timestamp is in reasonable range (> 2020)", "timestamp too old");
    }
}

/* ============================================
 * Test: hash_string
 * ============================================ */
void test_hash_string(void)
{
    printf("\n=== Test: hash_string (djb2) ===\n");

    reset_state();

    /* Test consistency - same input should produce same output */
    uint32_t hash1 = hash_string("test");
    uint32_t hash2 = hash_string("test");
    if (hash1 == hash2) {
        TEST_PASS("Hash is consistent (same input = same output)");
    } else {
        TEST_FAIL("Hash is consistent", "different outputs for same input");
    }

    /* Test distribution - different inputs should produce different outputs */
    uint32_t hash_test = hash_string("test");
    uint32_t hash_tset = hash_string("tset");
    uint32_t hash_hello = hash_string("hello");
    if (hash_test != hash_tset && hash_test != hash_hello && hash_tset != hash_hello) {
        TEST_PASS("Hash produces different values for different inputs");
    } else {
        TEST_FAIL("Hash produces different values", "collision detected");
    }

    /* Test empty string */
    uint32_t hash_empty = hash_string("");
    if (hash_empty == 5381) {  /* djb2 initial value */
        TEST_PASS("Empty string produces initial hash value (5381)");
    } else {
        TEST_FAIL("Empty string hash", "unexpected value");
    }

    /* Test known value (djb2 of "hello" is 261238937) */
    uint32_t hash_known = hash_string("hello");
    if (hash_known == 261238937) {
        TEST_PASS("Known value test (djb2 'hello' = 261238937)");
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected 261238937, got %u", hash_known);
        TEST_FAIL("Known value test", msg);
    }
}

/* ============================================
 * Test: parse_ip_address
 * ============================================ */
void test_parse_ip_address(void)
{
    printf("\n=== Test: parse_ip_address ===\n");

    reset_state();

    int af_family;
    union {
        struct in_addr addr4;
        struct in6_addr addr6;
    } addr;

    /* Test valid IPv4 */
    if (parse_ip_address("192.168.1.100", &af_family, &addr) == 0 && af_family == AF_INET) {
        TEST_PASS("Valid IPv4 parsing (192.168.1.100)");
    } else {
        TEST_FAIL("Valid IPv4 parsing", "failed or wrong family");
    }

    /* Test valid IPv6 */
    if (parse_ip_address("2001:db8::1", &af_family, &addr) == 0 && af_family == AF_INET6) {
        TEST_PASS("Valid IPv6 parsing (2001:db8::1)");
    } else {
        TEST_FAIL("Valid IPv6 parsing", "failed or wrong family");
    }

    /* Test invalid IP */
    if (parse_ip_address("not.an.ip", &af_family, &addr) != 0) {
        TEST_PASS("Invalid IP rejected (not.an.ip)");
    } else {
        TEST_FAIL("Invalid IP rejected", "should have failed");
    }

    /* Test NULL string */
    if (parse_ip_address(NULL, &af_family, &addr) != 0) {
        TEST_PASS("NULL string rejected");
    } else {
        TEST_FAIL("NULL string rejected", "should have failed");
    }

    /* Test compressed IPv6 */
    if (parse_ip_address("::1", &af_family, &addr) == 0 && af_family == AF_INET6) {
        TEST_PASS("Compressed IPv6 parsing (::1)");
    } else {
        TEST_FAIL("Compressed IPv6 parsing", "failed or wrong family");
    }

    /* Test full IPv6 */
    if (parse_ip_address("fe80:0000:0000:0000:0000:0000:0000:0001", &af_family, &addr) == 0 &&
        af_family == AF_INET6) {
        TEST_PASS("Full IPv6 parsing");
    } else {
        TEST_FAIL("Full IPv6 parsing", "failed or wrong family");
    }
}

/* ============================================
 * Test: format_mac
 * ============================================ */
void test_format_mac(void)
{
    printf("\n=== Test: format_mac ===\n");

    reset_state();

    char buf[64];
    unsigned char mac[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};

    /* Test standard 6-byte MAC */
    const char *result = format_mac(mac, 6, buf);
    if (result && strcmp(result, "aa:bb:cc:dd:ee:ff") == 0) {
        TEST_PASS("Standard MAC formatting");
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "expected 'aa:bb:cc:dd:ee:ff', got '%s'", result ? result : "NULL");
        TEST_FAIL("Standard MAC formatting", msg);
    }

    /* Test NULL MAC */
    result = format_mac(NULL, 6, buf);
    if (result == NULL) {
        TEST_PASS("NULL MAC returns NULL");
    } else {
        TEST_FAIL("NULL MAC returns NULL", "should return NULL");
    }

    /* Test NULL buffer */
    result = format_mac(mac, 6, NULL);
    if (result == NULL) {
        TEST_PASS("NULL buffer returns NULL");
    } else {
        TEST_FAIL("NULL buffer returns NULL", "should return NULL");
    }

    /* Test zero length */
    result = format_mac(mac, 0, buf);
    if (result == NULL) {
        TEST_PASS("Zero length returns NULL");
    } else {
        TEST_FAIL("Zero length returns NULL", "should return NULL");
    }
}

/* ============================================
 * Test: parse_mac
 * ============================================ */
void test_parse_mac(void)
{
    printf("\n=== Test: parse_mac ===\n");

    reset_state();

    unsigned char mac[32];
    int len;

    /* Test colon-separated format */
    if (parse_mac("aa:bb:cc:dd:ee:ff", mac, &len) == 0 && len == 6 &&
        mac[0] == 0xaa && mac[1] == 0xbb && mac[5] == 0xff) {
        TEST_PASS("Colon-separated MAC parsing");
    } else {
        TEST_FAIL("Colon-separated MAC parsing", "failed or wrong values");
    }

    /* Test dash-separated format */
    if (parse_mac("11-22-33-44-55-66", mac, &len) == 0 && len == 6 &&
        mac[0] == 0x11 && mac[5] == 0x66) {
        TEST_PASS("Dash-separated MAC parsing");
    } else {
        TEST_FAIL("Dash-separated MAC parsing", "failed or wrong values");
    }

    /* Test no-separator format */
    if (parse_mac("aabbccddeeff", mac, &len) == 0 && len == 6 &&
        mac[0] == 0xaa && mac[5] == 0xff) {
        TEST_PASS("No-separator MAC parsing");
    } else {
        TEST_FAIL("No-separator MAC parsing", "failed or wrong values");
    }

    /* Test NULL string */
    if (parse_mac(NULL, mac, &len) != 0) {
        TEST_PASS("NULL string rejected");
    } else {
        TEST_FAIL("NULL string rejected", "should have failed");
    }
}

/* ============================================
 * Test: trim_whitespace
 * ============================================ */
void test_trim_whitespace(void)
{
    printf("\n=== Test: trim_whitespace ===\n");

    reset_state();

    char buf[64];

    /* Test leading whitespace */
    strcpy(buf, "   hello");
    char *result = trim_whitespace(buf);
    if (strcmp(result, "hello") == 0) {
        TEST_PASS("Leading whitespace trimmed");
    } else {
        TEST_FAIL("Leading whitespace trimmed", "whitespace not removed");
    }

    /* Test trailing whitespace */
    strcpy(buf, "hello   ");
    result = trim_whitespace(buf);
    if (strcmp(result, "hello") == 0) {
        TEST_PASS("Trailing whitespace trimmed");
    } else {
        TEST_FAIL("Trailing whitespace trimmed", "whitespace not removed");
    }

    /* Test both leading and trailing */
    strcpy(buf, "  hello  ");
    result = trim_whitespace(buf);
    if (strcmp(result, "hello") == 0) {
        TEST_PASS("Both leading and trailing trimmed");
    } else {
        TEST_FAIL("Both leading and trailing trimmed", "whitespace not removed");
    }

    /* Test no whitespace */
    strcpy(buf, "hello");
    result = trim_whitespace(buf);
    if (strcmp(result, "hello") == 0) {
        TEST_PASS("No whitespace unchanged");
    } else {
        TEST_FAIL("No whitespace unchanged", "string modified");
    }

    /* Test all whitespace */
    strcpy(buf, "   ");
    result = trim_whitespace(buf);
    if (strcmp(result, "") == 0) {
        TEST_PASS("All whitespace returns empty string");
    } else {
        TEST_FAIL("All whitespace returns empty string", "not empty");
    }

    /* Test tabs and newlines */
    strcpy(buf, "\t\nhello\t\n");
    result = trim_whitespace(buf);
    if (strcmp(result, "hello") == 0) {
        TEST_PASS("Tabs and newlines trimmed");
    } else {
        TEST_FAIL("Tabs and newlines trimmed", "whitespace not removed");
    }
}

/* ============================================
 * Test: parse_int
 * ============================================ */
void test_parse_int(void)
{
    printf("\n=== Test: parse_int ===\n");

    reset_state();

    int value;

    /* Test valid positive integer */
    if (parse_int("123", &value) == 0 && value == 123) {
        TEST_PASS("Valid positive integer parsing");
    } else {
        TEST_FAIL("Valid positive integer parsing", "failed or wrong value");
    }

    /* Test valid negative integer */
    if (parse_int("-456", &value) == 0 && value == -456) {
        TEST_PASS("Valid negative integer parsing");
    } else {
        TEST_FAIL("Valid negative integer parsing", "failed or wrong value");
    }

    /* Test zero */
    if (parse_int("0", &value) == 0 && value == 0) {
        TEST_PASS("Zero parsing");
    } else {
        TEST_FAIL("Zero parsing", "failed or wrong value");
    }

    /* Test invalid (text) */
    if (parse_int("abc", &value) != 0) {
        TEST_PASS("Invalid text rejected");
    } else {
        TEST_FAIL("Invalid text rejected", "should have failed");
    }

    /* Test invalid (mixed) */
    if (parse_int("123abc", &value) != 0) {
        TEST_PASS("Mixed text/number rejected");
    } else {
        TEST_FAIL("Mixed text/number rejected", "should have failed");
    }

    /* Test NULL string */
    if (parse_int(NULL, &value) != 0) {
        TEST_PASS("NULL string rejected");
    } else {
        TEST_FAIL("NULL string rejected", "should have failed");
    }
}

/* ============================================
 * Test: Message Deduplication
 * ============================================ */
void test_message_deduplication(void)
{
    printf("\n=== Test: Message Deduplication ===\n");

    reset_state();

    /* Fresh message should not be seen */
    if (!is_seen_message(1234, "node1")) {
        TEST_PASS("Fresh message not seen");
    } else {
        TEST_FAIL("Fresh message not seen", "reported as seen");
    }

    /* Mark message as seen */
    mark_message_seen(1234, "node1");

    /* Same message should now be seen */
    if (is_seen_message(1234, "node1")) {
        TEST_PASS("Marked message is seen");
    } else {
        TEST_FAIL("Marked message is seen", "not reported as seen");
    }

    /* Different sequence should not be seen */
    if (!is_seen_message(5678, "node1")) {
        TEST_PASS("Different sequence not seen");
    } else {
        TEST_FAIL("Different sequence not seen", "reported as seen");
    }

    /* Different node should not be seen */
    if (!is_seen_message(1234, "node2")) {
        TEST_PASS("Different node not seen");
    } else {
        TEST_FAIL("Different node not seen", "reported as seen");
    }
}

/* ============================================
 * Main Test Runner
 * ============================================ */
int main(void)
{
    printf("=========================================\n");
    printf("lease-sync Utility Functions Unit Tests\n");
    printf("(linked against daemon/util.c)\n");
    printf("=========================================\n");

    test_get_timestamp_ms();
    test_hash_string();
    test_parse_ip_address();
    test_format_mac();
    test_parse_mac();
    test_trim_whitespace();
    test_parse_int();
    test_message_deduplication();

    return print_test_summary();
}
