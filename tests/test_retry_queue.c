/* test_retry_queue.c
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
 * test_retry_queue.c - Unit tests for injection retry queue
 *
 * Build: See run-retry-queue-tests.sh
 * Run: ./run-retry-queue-tests.sh
 *
 * This test links against the actual daemon retry-queue.c code,
 * using stubs for ubus functions to enable standalone testing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <syslog.h>

/* Include the daemon's common.h for real structure definitions */
#include "../daemon/common.h"
#include "../daemon/retry-queue.h"

#include "test_framework.h"

/* ============================================
 * Global State Stub for Testing
 * ============================================ */
struct daemon_state *g_state = NULL;
static struct daemon_state state_storage;

/* ============================================
 * Stub for ubus injection functions
 * These stubs are called by retry-queue.c during processing
 * ============================================ */
static int inject_should_fail = 0;
static int inject_call_count = 0;

int ubus_handler_inject_lease(struct lease_entry *entry)
{
    (void)entry;
    inject_call_count++;
    return inject_should_fail ? -1 : 0;
}

int ubus_handler_delete_lease(const char *ip)
{
    (void)ip;
    inject_call_count++;
    return inject_should_fail ? -1 : 0;
}

/* ============================================
 * Helper Functions
 * ============================================ */

static void reset_state(void)
{
    memset(&state_storage, 0, sizeof(state_storage));
    g_state = &state_storage;
    retry_queue_init(&g_state->retry_queue);
    inject_should_fail = 0;
    inject_call_count = 0;
}

static void make_lease(struct lease_entry *lease, const char *ip, uint64_t timestamp)
{
    memset(lease, 0, sizeof(*lease));
    strncpy(lease->ip_str, ip, sizeof(lease->ip_str) - 1);
    lease->timestamp_ms = timestamp;
    lease->af_family = AF_INET;
}

/* ============================================
 * Test: Basic Initialization
 * ============================================ */
void test_init(void)
{
    printf("\n=== Test: Basic Initialization ===\n");

    reset_state();

    if (g_state->retry_queue.head == NULL &&
        g_state->retry_queue.tail == NULL &&
        g_state->retry_queue.count == 0) {
        TEST_PASS("Queue initialized empty");
    } else {
        TEST_FAIL("Queue initialized empty", "queue not empty after init");
    }

    if (retry_queue_count() == 0) {
        TEST_PASS("retry_queue_count returns 0");
    } else {
        TEST_FAIL("retry_queue_count returns 0", "returned non-zero");
    }
}

/* ============================================
 * Test: Basic Add Operation
 * ============================================ */
void test_basic_add(void)
{
    printf("\n=== Test: Basic Add Operation ===\n");

    reset_state();

    struct lease_entry lease;
    make_lease(&lease, "192.168.1.100", 1000);

    int ret = retry_queue_add(&g_state->retry_queue, &lease, RETRY_ACTION_ADD);
    if (ret == 0) {
        TEST_PASS("Add returns success");
    } else {
        TEST_FAIL("Add returns success", "returned error");
    }

    if (retry_queue_count() == 1) {
        TEST_PASS("Count is 1 after add");
    } else {
        TEST_FAIL("Count is 1 after add", "count mismatch");
    }

    if (g_state->retry_queue.head != NULL &&
        strcmp(g_state->retry_queue.head->lease.ip_str, "192.168.1.100") == 0) {
        TEST_PASS("Head contains correct IP");
    } else {
        TEST_FAIL("Head contains correct IP", "IP mismatch");
    }

    if (g_state->retry_queue.head == g_state->retry_queue.tail) {
        TEST_PASS("Head equals tail for single entry");
    } else {
        TEST_FAIL("Head equals tail for single entry", "pointers differ");
    }

    if (g_state->retry_queue.head->action == RETRY_ACTION_ADD) {
        TEST_PASS("Action is RETRY_ACTION_ADD");
    } else {
        TEST_FAIL("Action is RETRY_ACTION_ADD", "wrong action");
    }
}

/* ============================================
 * Test: Multiple Entries
 * ============================================ */
void test_multiple_entries(void)
{
    printf("\n=== Test: Multiple Entries ===\n");

    reset_state();

    struct lease_entry lease1, lease2, lease3;
    make_lease(&lease1, "192.168.1.101", 1000);
    make_lease(&lease2, "192.168.1.102", 2000);
    make_lease(&lease3, "192.168.1.103", 3000);

    retry_queue_add(&g_state->retry_queue, &lease1, RETRY_ACTION_ADD);
    retry_queue_add(&g_state->retry_queue, &lease2, RETRY_ACTION_DELETE);
    retry_queue_add(&g_state->retry_queue, &lease3, RETRY_ACTION_ADD);

    if (retry_queue_count() == 3) {
        TEST_PASS("Count is 3 after adding 3 entries");
    } else {
        TEST_FAIL("Count is 3 after adding 3 entries", "count mismatch");
    }

    /* Verify FIFO order */
    if (strcmp(g_state->retry_queue.head->lease.ip_str, "192.168.1.101") == 0) {
        TEST_PASS("Head is first entry (FIFO)");
    } else {
        TEST_FAIL("Head is first entry (FIFO)", "wrong order");
    }

    if (strcmp(g_state->retry_queue.tail->lease.ip_str, "192.168.1.103") == 0) {
        TEST_PASS("Tail is last entry (FIFO)");
    } else {
        TEST_FAIL("Tail is last entry (FIFO)", "wrong order");
    }

    /* Verify middle entry has correct action */
    if (g_state->retry_queue.head->next->action == RETRY_ACTION_DELETE) {
        TEST_PASS("Second entry has RETRY_ACTION_DELETE");
    } else {
        TEST_FAIL("Second entry has RETRY_ACTION_DELETE", "wrong action");
    }
}

/* ============================================
 * Test: Duplicate Handling (newer timestamp)
 * ============================================ */
void test_duplicate_newer(void)
{
    printf("\n=== Test: Duplicate Handling (newer timestamp) ===\n");

    reset_state();

    struct lease_entry lease1, lease2;
    make_lease(&lease1, "192.168.1.200", 1000);
    make_lease(&lease2, "192.168.1.200", 2000);  /* Same IP, newer timestamp */
    strcpy(lease2.hostname, "updated-host");

    retry_queue_add(&g_state->retry_queue, &lease1, RETRY_ACTION_ADD);
    retry_queue_add(&g_state->retry_queue, &lease2, RETRY_ACTION_DELETE);

    if (retry_queue_count() == 1) {
        TEST_PASS("Count remains 1 (duplicate updated in place)");
    } else {
        TEST_FAIL("Count remains 1 (duplicate updated in place)", "count increased");
    }

    if (g_state->retry_queue.head->lease.timestamp_ms == 2000) {
        TEST_PASS("Timestamp updated to newer value");
    } else {
        TEST_FAIL("Timestamp updated to newer value", "timestamp not updated");
    }

    if (strcmp(g_state->retry_queue.head->lease.hostname, "updated-host") == 0) {
        TEST_PASS("Hostname updated");
    } else {
        TEST_FAIL("Hostname updated", "hostname not updated");
    }

    if (g_state->retry_queue.head->action == RETRY_ACTION_DELETE) {
        TEST_PASS("Action updated to DELETE");
    } else {
        TEST_FAIL("Action updated to DELETE", "action not updated");
    }
}

/* ============================================
 * Test: Duplicate Handling (older timestamp)
 * ============================================ */
void test_duplicate_older(void)
{
    printf("\n=== Test: Duplicate Handling (older timestamp) ===\n");

    reset_state();

    struct lease_entry lease1, lease2;
    make_lease(&lease1, "192.168.1.201", 2000);  /* Newer timestamp first */
    make_lease(&lease2, "192.168.1.201", 1000);  /* Same IP, older timestamp */
    strcpy(lease2.hostname, "should-not-update");

    retry_queue_add(&g_state->retry_queue, &lease1, RETRY_ACTION_ADD);
    retry_queue_add(&g_state->retry_queue, &lease2, RETRY_ACTION_DELETE);

    if (retry_queue_count() == 1) {
        TEST_PASS("Count remains 1");
    } else {
        TEST_FAIL("Count remains 1", "count changed");
    }

    if (g_state->retry_queue.head->lease.timestamp_ms == 2000) {
        TEST_PASS("Timestamp NOT downgraded");
    } else {
        TEST_FAIL("Timestamp NOT downgraded", "timestamp was changed");
    }

    if (g_state->retry_queue.head->action == RETRY_ACTION_ADD) {
        TEST_PASS("Action NOT changed");
    } else {
        TEST_FAIL("Action NOT changed", "action was changed");
    }
}

/* ============================================
 * Test: Queue Overflow
 * ============================================ */
void test_queue_overflow(void)
{
    printf("\n=== Test: Queue Overflow ===\n");

    reset_state();

    /* Fill queue to capacity */
    for (int i = 0; i < RETRY_QUEUE_SIZE; i++) {
        struct lease_entry lease;
        char ip[32];
        snprintf(ip, sizeof(ip), "10.0.0.%d", i);
        make_lease(&lease, ip, 1000 + i);
        retry_queue_add(&g_state->retry_queue, &lease, RETRY_ACTION_ADD);
    }

    if (retry_queue_count() == RETRY_QUEUE_SIZE) {
        TEST_PASS("Queue at capacity (64)");
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected 64, got %d", retry_queue_count());
        TEST_FAIL("Queue at capacity (64)", msg);
    }

    /* Remember first entry IP */
    char first_ip[32];
    strncpy(first_ip, g_state->retry_queue.head->lease.ip_str, sizeof(first_ip));

    /* Add one more - should drop oldest */
    struct lease_entry overflow_lease;
    make_lease(&overflow_lease, "10.1.0.1", 9999);
    retry_queue_add(&g_state->retry_queue, &overflow_lease, RETRY_ACTION_ADD);

    if (retry_queue_count() == RETRY_QUEUE_SIZE) {
        TEST_PASS("Count stays at 64 after overflow");
    } else {
        TEST_FAIL("Count stays at 64 after overflow", "count changed");
    }

    if (strcmp(g_state->retry_queue.head->lease.ip_str, first_ip) != 0) {
        TEST_PASS("Oldest entry (head) was dropped");
    } else {
        TEST_FAIL("Oldest entry (head) was dropped", "head unchanged");
    }

    if (strcmp(g_state->retry_queue.tail->lease.ip_str, "10.1.0.1") == 0) {
        TEST_PASS("New entry added at tail");
    } else {
        TEST_FAIL("New entry added at tail", "tail not updated");
    }

    if (g_state->total_injection_drops == 1) {
        TEST_PASS("Drops counter incremented");
    } else {
        TEST_FAIL("Drops counter incremented", "counter not incremented");
    }
}

/* ============================================
 * Test: Cleanup
 * ============================================ */
void test_cleanup(void)
{
    printf("\n=== Test: Cleanup ===\n");

    reset_state();

    /* Add some entries */
    for (int i = 0; i < 10; i++) {
        struct lease_entry lease;
        char ip[32];
        snprintf(ip, sizeof(ip), "10.2.0.%d", i);
        make_lease(&lease, ip, 1000 + i);
        retry_queue_add(&g_state->retry_queue, &lease, RETRY_ACTION_ADD);
    }

    if (retry_queue_count() == 10) {
        TEST_PASS("Queue has 10 entries before cleanup");
    } else {
        TEST_FAIL("Queue has 10 entries before cleanup", "wrong count");
    }

    retry_queue_cleanup(&g_state->retry_queue);

    if (retry_queue_count() == 0) {
        TEST_PASS("Count is 0 after cleanup");
    } else {
        TEST_FAIL("Count is 0 after cleanup", "count not zero");
    }

    if (g_state->retry_queue.head == NULL) {
        TEST_PASS("Head is NULL after cleanup");
    } else {
        TEST_FAIL("Head is NULL after cleanup", "head not NULL");
    }

    if (g_state->retry_queue.tail == NULL) {
        TEST_PASS("Tail is NULL after cleanup");
    } else {
        TEST_FAIL("Tail is NULL after cleanup", "tail not NULL");
    }
}

/* ============================================
 * Test: Process with Successful Injection
 * ============================================ */
void test_process_success(void)
{
    printf("\n=== Test: Process with Successful Injection ===\n");

    reset_state();
    inject_should_fail = 0;

    struct lease_entry lease;
    make_lease(&lease, "192.168.2.1", 1000);
    retry_queue_add(&g_state->retry_queue, &lease, RETRY_ACTION_ADD);

    /* Set last_attempt to past to allow immediate processing */
    g_state->retry_queue.head->last_attempt = time(NULL) - RETRY_INTERVAL_SECONDS - 1;

    int count_before = retry_queue_count();
    retry_queue_process();
    int count_after = retry_queue_count();

    if (count_before == 1 && count_after == 0) {
        TEST_PASS("Entry removed after successful injection");
    } else {
        TEST_FAIL("Entry removed after successful injection", "entry not removed");
    }

    if (g_state->total_injection_retries == 1) {
        TEST_PASS("Retries counter incremented");
    } else {
        TEST_FAIL("Retries counter incremented", "counter not incremented");
    }

    if (inject_call_count == 1) {
        TEST_PASS("Inject function called once");
    } else {
        TEST_FAIL("Inject function called once", "wrong call count");
    }
}

/* ============================================
 * Test: Process with Failed Injection (retry)
 * ============================================ */
void test_process_failure_retry(void)
{
    printf("\n=== Test: Process with Failed Injection (retry) ===\n");

    reset_state();
    inject_should_fail = 1;

    struct lease_entry lease;
    make_lease(&lease, "192.168.2.2", 1000);
    retry_queue_add(&g_state->retry_queue, &lease, RETRY_ACTION_ADD);

    /* Set last_attempt to past */
    g_state->retry_queue.head->last_attempt = time(NULL) - RETRY_INTERVAL_SECONDS - 1;

    retry_queue_process();

    if (retry_queue_count() == 1) {
        TEST_PASS("Entry remains in queue after failed injection");
    } else {
        TEST_FAIL("Entry remains in queue after failed injection", "entry removed");
    }

    if (g_state->retry_queue.head->retry_count == 1) {
        TEST_PASS("Retry count incremented to 1");
    } else {
        TEST_FAIL("Retry count incremented to 1", "wrong retry count");
    }
}

/* ============================================
 * Test: Process Max Retries Exceeded
 * ============================================ */
void test_process_max_retries(void)
{
    printf("\n=== Test: Process Max Retries Exceeded ===\n");

    reset_state();
    inject_should_fail = 1;

    struct lease_entry lease;
    make_lease(&lease, "192.168.2.3", 1000);
    retry_queue_add(&g_state->retry_queue, &lease, RETRY_ACTION_ADD);

    /* Set retry_count to max - 1 */
    g_state->retry_queue.head->retry_count = RETRY_MAX_ATTEMPTS - 1;
    g_state->retry_queue.head->last_attempt = time(NULL) - RETRY_INTERVAL_SECONDS - 1;

    uint64_t drops_before = g_state->total_injection_drops;
    retry_queue_process();
    uint64_t drops_after = g_state->total_injection_drops;

    if (retry_queue_count() == 0) {
        TEST_PASS("Entry removed after max retries");
    } else {
        TEST_FAIL("Entry removed after max retries", "entry not removed");
    }

    if (drops_after == drops_before + 1) {
        TEST_PASS("Drops counter incremented");
    } else {
        TEST_FAIL("Drops counter incremented", "counter not incremented");
    }
}

/* ============================================
 * Test: Process Respects Interval
 * ============================================ */
void test_process_respects_interval(void)
{
    printf("\n=== Test: Process Respects Interval ===\n");

    reset_state();
    inject_should_fail = 0;
    inject_call_count = 0;

    struct lease_entry lease;
    make_lease(&lease, "192.168.2.4", 1000);
    retry_queue_add(&g_state->retry_queue, &lease, RETRY_ACTION_ADD);

    /* Leave last_attempt at current time (just added) */

    retry_queue_process();

    if (inject_call_count == 0) {
        TEST_PASS("Inject not called when interval not elapsed");
    } else {
        TEST_FAIL("Inject not called when interval not elapsed", "inject was called");
    }

    if (retry_queue_count() == 1) {
        TEST_PASS("Entry remains in queue");
    } else {
        TEST_FAIL("Entry remains in queue", "entry removed prematurely");
    }
}

/* ============================================
 * Test: Delete Action
 * ============================================ */
void test_delete_action(void)
{
    printf("\n=== Test: Delete Action ===\n");

    reset_state();
    inject_should_fail = 0;
    inject_call_count = 0;

    struct lease_entry lease;
    make_lease(&lease, "192.168.2.5", 1000);
    retry_queue_add(&g_state->retry_queue, &lease, RETRY_ACTION_DELETE);

    g_state->retry_queue.head->last_attempt = time(NULL) - RETRY_INTERVAL_SECONDS - 1;

    retry_queue_process();

    if (inject_call_count == 1) {
        TEST_PASS("Delete handler called");
    } else {
        TEST_FAIL("Delete handler called", "handler not called");
    }

    if (retry_queue_count() == 0) {
        TEST_PASS("Entry removed after successful delete");
    } else {
        TEST_FAIL("Entry removed after successful delete", "entry not removed");
    }
}

/* ============================================
 * Test: Pointer Isolation (lease.next cleared)
 * ============================================ */
void test_pointer_isolation(void)
{
    printf("\n=== Test: Pointer Isolation ===\n");

    reset_state();

    struct lease_entry lease;
    make_lease(&lease, "192.168.3.1", 1000);
    lease.next = (struct lease_entry *)0xDEADBEEF;  /* Simulate hash table chain */

    retry_queue_add(&g_state->retry_queue, &lease, RETRY_ACTION_ADD);

    if (g_state->retry_queue.head->lease.next == NULL) {
        TEST_PASS("lease.next pointer cleared in queue entry");
    } else {
        TEST_FAIL("lease.next pointer cleared in queue entry", "pointer not cleared");
    }
}

/* ============================================
 * Test: NULL Parameter Handling
 * ============================================ */
void test_null_handling(void)
{
    printf("\n=== Test: NULL Parameter Handling ===\n");

    reset_state();

    struct lease_entry lease;
    make_lease(&lease, "192.168.4.1", 1000);

    if (retry_queue_init(NULL) == -1) {
        TEST_PASS("retry_queue_init(NULL) returns -1");
    } else {
        TEST_FAIL("retry_queue_init(NULL) returns -1", "didn't return -1");
    }

    if (retry_queue_add(NULL, &lease, RETRY_ACTION_ADD) == -1) {
        TEST_PASS("retry_queue_add(NULL queue) returns -1");
    } else {
        TEST_FAIL("retry_queue_add(NULL queue) returns -1", "didn't return -1");
    }

    if (retry_queue_add(&g_state->retry_queue, NULL, RETRY_ACTION_ADD) == -1) {
        TEST_PASS("retry_queue_add(NULL lease) returns -1");
    } else {
        TEST_FAIL("retry_queue_add(NULL lease) returns -1", "didn't return -1");
    }

    /* retry_queue_cleanup(NULL) should not crash */
    retry_queue_cleanup(NULL);
    TEST_PASS("retry_queue_cleanup(NULL) does not crash");
}

/* ============================================
 * Main Test Runner
 * ============================================ */
int main(void)
{
    printf("=========================================\n");
    printf("lease-sync Retry Queue Unit Tests\n");
    printf("(linked against daemon/retry-queue.c)\n");
    printf("=========================================\n");

    test_init();
    test_basic_add();
    test_multiple_entries();
    test_duplicate_newer();
    test_duplicate_older();
    test_queue_overflow();
    test_cleanup();
    test_process_success();
    test_process_failure_retry();
    test_process_max_retries();
    test_process_respects_interval();
    test_delete_action();
    test_pointer_isolation();
    test_null_handling();

    return print_test_summary();
}
