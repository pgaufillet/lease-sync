/* test_lease_db.c
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
 * test_lease_db.c - Unit tests for lease database
 *
 * Build: See run-lease-db-tests.sh
 * Run: ./run-lease-db-tests.sh
 *
 * This test links against daemon/lease-db.c and daemon/util.c.
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
    strcpy(g_state->config.node_id, "test-node");
    g_state->config.log_level = LOG_LEVEL_ERROR;  /* Suppress debug logs */
    lease_db_init();
}

/* ============================================
 * Test Helpers
 * ============================================ */

static void make_lease(struct lease_entry *lease, const char *ip, const char *mac,
                       const char *hostname, uint64_t timestamp, const char *source)
{
    memset(lease, 0, sizeof(*lease));
    strncpy(lease->ip_str, ip, sizeof(lease->ip_str) - 1);
    strncpy(lease->mac, mac, sizeof(lease->mac) - 1);
    strncpy(lease->hostname, hostname, sizeof(lease->hostname) - 1);
    strncpy(lease->source_node, source, sizeof(lease->source_node) - 1);
    lease->timestamp_ms = timestamp;
    lease->af_family = AF_INET;
    lease->expires = time(NULL) + 3600;  /* 1 hour from now */
}

/* Callback for iteration test */
static int iteration_count = 0;
static void count_callback(struct lease_entry *entry, void *user_data)
{
    (void)entry;
    int *count = (int *)user_data;
    (*count)++;
}

/* ============================================
 * Test: lease_db_init
 * ============================================ */
void test_lease_db_init(void)
{
    printf("\n=== Test: lease_db_init ===\n");

    reset_state();

    /* Verify database is initialized empty */
    if (lease_db_count() == 0) {
        TEST_PASS("Database initialized with count 0");
    } else {
        TEST_FAIL("Database initialized with count 0", "count not 0");
    }

    /* Verify counters are reset */
    if (g_state->local_lease_count == 0 && g_state->peer_lease_count == 0) {
        TEST_PASS("Local and peer counts initialized to 0");
    } else {
        TEST_FAIL("Local and peer counts initialized to 0", "counts not 0");
    }

    /* NULL g_state should fail */
    g_state = NULL;
    if (lease_db_init() == -1) {
        TEST_PASS("lease_db_init with NULL g_state returns -1");
    } else {
        TEST_FAIL("lease_db_init with NULL g_state returns -1", "did not fail");
    }
    g_state = &state_storage;
}

/* ============================================
 * Test: lease_db_add - Basic operation
 * ============================================ */
void test_lease_db_add_basic(void)
{
    printf("\n=== Test: lease_db_add - Basic operation ===\n");

    reset_state();

    struct lease_entry lease;
    make_lease(&lease, "192.168.1.100", "aa:bb:cc:dd:ee:ff",
               "testhost", 1000, "test-node");

    int ret = lease_db_add(&lease);
    if (ret == 0) {
        TEST_PASS("lease_db_add returns success");
    } else {
        TEST_FAIL("lease_db_add returns success", "returned error");
    }

    if (lease_db_count() == 1) {
        TEST_PASS("Count is 1 after add");
    } else {
        TEST_FAIL("Count is 1 after add", "wrong count");
    }

    /* Find the lease */
    struct lease_entry *found = lease_db_find("192.168.1.100");
    if (found != NULL) {
        TEST_PASS("lease_db_find returns non-NULL");
    } else {
        TEST_FAIL("lease_db_find returns non-NULL", "returned NULL");
    }

    if (found && strcmp(found->hostname, "testhost") == 0) {
        TEST_PASS("Found lease has correct hostname");
    } else {
        TEST_FAIL("Found lease has correct hostname", "wrong hostname");
    }

    if (found && strcmp(found->mac, "aa:bb:cc:dd:ee:ff") == 0) {
        TEST_PASS("Found lease has correct MAC");
    } else {
        TEST_FAIL("Found lease has correct MAC", "wrong MAC");
    }

    /* Statistics check */
    if (g_state->local_lease_count == 1) {
        TEST_PASS("Local lease count incremented");
    } else {
        TEST_FAIL("Local lease count incremented", "wrong count");
    }

    if (g_state->total_leases_added == 1) {
        TEST_PASS("Total leases added incremented");
    } else {
        TEST_FAIL("Total leases added incremented", "wrong count");
    }
}

/* ============================================
 * Test: lease_db_add - Multiple leases
 * ============================================ */
void test_lease_db_add_multiple(void)
{
    printf("\n=== Test: lease_db_add - Multiple leases ===\n");

    reset_state();

    /* Add multiple leases */
    for (int i = 0; i < 100; i++) {
        struct lease_entry lease;
        char ip[32], mac[32], hostname[64];
        snprintf(ip, sizeof(ip), "192.168.1.%d", i);
        snprintf(mac, sizeof(mac), "aa:bb:cc:dd:ee:%02x", i);
        snprintf(hostname, sizeof(hostname), "host-%d", i);
        make_lease(&lease, ip, mac, hostname, 1000 + i, "test-node");
        lease_db_add(&lease);
    }

    if (lease_db_count() == 100) {
        TEST_PASS("Database has 100 leases");
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected 100, got %d", lease_db_count());
        TEST_FAIL("Database has 100 leases", msg);
    }

    /* Find specific leases */
    struct lease_entry *found = lease_db_find("192.168.1.50");
    if (found && strcmp(found->hostname, "host-50") == 0) {
        TEST_PASS("Can find specific lease (192.168.1.50)");
    } else {
        TEST_FAIL("Can find specific lease (192.168.1.50)", "not found or wrong data");
    }

    found = lease_db_find("192.168.1.99");
    if (found && strcmp(found->hostname, "host-99") == 0) {
        TEST_PASS("Can find last lease (192.168.1.99)");
    } else {
        TEST_FAIL("Can find last lease (192.168.1.99)", "not found or wrong data");
    }
}

/* ============================================
 * Test: lease_db_find - Non-existent
 * ============================================ */
void test_lease_db_find_nonexistent(void)
{
    printf("\n=== Test: lease_db_find - Non-existent ===\n");

    reset_state();

    /* Find non-existent IP */
    struct lease_entry *found = lease_db_find("10.0.0.1");
    if (found == NULL) {
        TEST_PASS("Non-existent IP returns NULL");
    } else {
        TEST_FAIL("Non-existent IP returns NULL", "returned non-NULL");
    }

    /* NULL IP */
    found = lease_db_find(NULL);
    if (found == NULL) {
        TEST_PASS("NULL IP returns NULL");
    } else {
        TEST_FAIL("NULL IP returns NULL", "returned non-NULL");
    }
}

/* ============================================
 * Test: lease_db_update - Basic update
 * ============================================ */
void test_lease_db_update_basic(void)
{
    printf("\n=== Test: lease_db_update - Basic update ===\n");

    reset_state();

    /* Add initial lease */
    struct lease_entry lease;
    make_lease(&lease, "192.168.1.200", "aa:bb:cc:dd:ee:ff",
               "oldhost", 1000, "test-node");
    lease_db_add(&lease);

    /* Update with newer timestamp */
    make_lease(&lease, "192.168.1.200", "aa:bb:cc:dd:ee:ff",
               "newhost", 2000, "test-node");
    int ret = lease_db_update(&lease);

    if (ret == 0) {
        TEST_PASS("lease_db_update returns success");
    } else {
        TEST_FAIL("lease_db_update returns success", "returned error");
    }

    /* Count should still be 1 */
    if (lease_db_count() == 1) {
        TEST_PASS("Count remains 1 after update");
    } else {
        TEST_FAIL("Count remains 1 after update", "count changed");
    }

    /* Verify hostname updated */
    struct lease_entry *found = lease_db_find("192.168.1.200");
    if (found && strcmp(found->hostname, "newhost") == 0) {
        TEST_PASS("Hostname updated to newhost");
    } else {
        TEST_FAIL("Hostname updated to newhost", "wrong hostname");
    }

    /* Statistics check */
    if (g_state->total_leases_updated == 1) {
        TEST_PASS("Total leases updated incremented");
    } else {
        TEST_FAIL("Total leases updated incremented", "wrong count");
    }
}

/* ============================================
 * Test: lease_db_update - Conflict resolution (newer wins)
 * ============================================ */
void test_lease_db_update_conflict_newer_wins(void)
{
    printf("\n=== Test: lease_db_update - Conflict resolution ===\n");

    reset_state();

    /* Add initial lease */
    struct lease_entry lease;
    make_lease(&lease, "192.168.1.201", "aa:bb:cc:dd:ee:ff",
               "host1", 2000, "test-node");
    lease_db_add(&lease);

    /* Try to update with older timestamp - should be ignored */
    make_lease(&lease, "192.168.1.201", "aa:bb:cc:dd:ee:ff",
               "host-old", 1000, "peer-node");
    lease_db_update(&lease);

    /* Verify original hostname preserved */
    struct lease_entry *found = lease_db_find("192.168.1.201");
    if (found && strcmp(found->hostname, "host1") == 0) {
        TEST_PASS("Older update ignored - hostname preserved");
    } else {
        TEST_FAIL("Older update ignored", "hostname changed");
    }

    if (found && found->timestamp_ms == 2000) {
        TEST_PASS("Older update ignored - timestamp preserved");
    } else {
        TEST_FAIL("Older update ignored", "timestamp changed");
    }

    /* Update with newer timestamp - should succeed */
    make_lease(&lease, "192.168.1.201", "aa:bb:cc:dd:ee:ff",
               "host-new", 3000, "peer-node");
    lease_db_update(&lease);

    found = lease_db_find("192.168.1.201");
    if (found && strcmp(found->hostname, "host-new") == 0) {
        TEST_PASS("Newer update applied - hostname changed");
    } else {
        TEST_FAIL("Newer update applied", "hostname not changed");
    }
}

/* ============================================
 * Test: lease_db_update - MAC conflict detection
 * ============================================ */
void test_lease_db_update_mac_conflict(void)
{
    printf("\n=== Test: lease_db_update - MAC conflict detection ===\n");

    reset_state();

    /* Add initial lease */
    struct lease_entry lease;
    make_lease(&lease, "192.168.1.202", "aa:aa:aa:aa:aa:aa",
               "host1", 1000, "test-node");
    lease_db_add(&lease);

    /* Update with different MAC (and newer timestamp) */
    make_lease(&lease, "192.168.1.202", "bb:bb:bb:bb:bb:bb",
               "host2", 2000, "peer-node");
    lease_db_update(&lease);

    /* Verify MAC was updated (conflict resolved by timestamp) */
    struct lease_entry *found = lease_db_find("192.168.1.202");
    if (found && strcmp(found->mac, "bb:bb:bb:bb:bb:bb") == 0) {
        TEST_PASS("MAC conflict resolved - newer MAC applied");
    } else {
        TEST_FAIL("MAC conflict resolved", "MAC not updated");
    }

    /* Check conflict counter */
    if (g_state->total_conflicts_resolved == 1) {
        TEST_PASS("Conflict counter incremented");
    } else {
        TEST_FAIL("Conflict counter incremented", "counter not incremented");
    }
}

/* ============================================
 * Test: lease_db_update - Non-existent creates new
 * ============================================ */
void test_lease_db_update_creates_new(void)
{
    printf("\n=== Test: lease_db_update - Non-existent creates new ===\n");

    reset_state();

    /* Update non-existent lease should add it */
    struct lease_entry lease;
    make_lease(&lease, "192.168.1.203", "cc:cc:cc:cc:cc:cc",
               "newhost", 1000, "test-node");

    int initial_count = lease_db_count();
    int ret = lease_db_update(&lease);

    if (ret == 0) {
        TEST_PASS("lease_db_update for non-existent returns success");
    } else {
        TEST_FAIL("lease_db_update for non-existent returns success", "returned error");
    }

    if (lease_db_count() == initial_count + 1) {
        TEST_PASS("Non-existent update adds new entry");
    } else {
        TEST_FAIL("Non-existent update adds new entry", "count not incremented");
    }

    /* Verify it exists */
    struct lease_entry *found = lease_db_find("192.168.1.203");
    if (found != NULL) {
        TEST_PASS("New entry can be found");
    } else {
        TEST_FAIL("New entry can be found", "not found");
    }
}

/* ============================================
 * Test: lease_db_delete
 * ============================================ */
void test_lease_db_delete(void)
{
    printf("\n=== Test: lease_db_delete ===\n");

    reset_state();

    /* Add lease */
    struct lease_entry lease;
    make_lease(&lease, "192.168.1.204", "dd:dd:dd:dd:dd:dd",
               "deletehost", 1000, "test-node");
    lease_db_add(&lease);

    if (lease_db_count() == 1) {
        TEST_PASS("Setup: lease added");
    }

    /* Delete it */
    int ret = lease_db_delete("192.168.1.204");
    if (ret == 0) {
        TEST_PASS("lease_db_delete returns success");
    } else {
        TEST_FAIL("lease_db_delete returns success", "returned error");
    }

    if (lease_db_count() == 0) {
        TEST_PASS("Count is 0 after delete");
    } else {
        TEST_FAIL("Count is 0 after delete", "count not 0");
    }

    /* Verify cannot find deleted lease */
    struct lease_entry *found = lease_db_find("192.168.1.204");
    if (found == NULL) {
        TEST_PASS("Deleted lease not found");
    } else {
        TEST_FAIL("Deleted lease not found", "still found");
    }

    /* Delete non-existent should fail */
    ret = lease_db_delete("192.168.1.204");
    if (ret == -1) {
        TEST_PASS("Delete non-existent returns -1");
    } else {
        TEST_FAIL("Delete non-existent returns -1", "wrong return");
    }

    /* Statistics check */
    if (g_state->total_leases_deleted == 1) {
        TEST_PASS("Total leases deleted incremented");
    } else {
        TEST_FAIL("Total leases deleted incremented", "wrong count");
    }
}

/* ============================================
 * Test: lease_db_cleanup
 * ============================================ */
void test_lease_db_cleanup(void)
{
    printf("\n=== Test: lease_db_cleanup ===\n");

    reset_state();

    /* Add multiple leases */
    for (int i = 0; i < 50; i++) {
        struct lease_entry lease;
        char ip[32];
        snprintf(ip, sizeof(ip), "10.0.0.%d", i);
        make_lease(&lease, ip, "aa:bb:cc:dd:ee:ff", "host", 1000, "test-node");
        lease_db_add(&lease);
    }

    if (lease_db_count() == 50) {
        TEST_PASS("Setup: 50 leases added");
    }

    /* Cleanup */
    lease_db_cleanup();

    if (lease_db_count() == 0) {
        TEST_PASS("Count is 0 after cleanup");
    } else {
        TEST_FAIL("Count is 0 after cleanup", "count not 0");
    }

    /* Verify counters reset */
    if (g_state->local_lease_count == 0 && g_state->peer_lease_count == 0) {
        TEST_PASS("Local and peer counts reset to 0");
    } else {
        TEST_FAIL("Local and peer counts reset to 0", "counts not 0");
    }

    /* Verify cannot find any lease */
    struct lease_entry *found = lease_db_find("10.0.0.25");
    if (found == NULL) {
        TEST_PASS("Cannot find lease after cleanup");
    } else {
        TEST_FAIL("Cannot find lease after cleanup", "still found");
    }
}

/* ============================================
 * Test: lease_db_foreach
 * ============================================ */
void test_lease_db_foreach(void)
{
    printf("\n=== Test: lease_db_foreach ===\n");

    reset_state();

    /* Add some leases */
    for (int i = 0; i < 25; i++) {
        struct lease_entry lease;
        char ip[32];
        snprintf(ip, sizeof(ip), "172.16.0.%d", i);
        make_lease(&lease, ip, "aa:bb:cc:dd:ee:ff", "host", 1000, "test-node");
        lease_db_add(&lease);
    }

    /* Count using foreach */
    int count = 0;
    lease_db_foreach(count_callback, &count);

    if (count == 25) {
        TEST_PASS("foreach iterates over all 25 leases");
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected 25, got %d", count);
        TEST_FAIL("foreach iterates over all 25 leases", msg);
    }

    /* Empty database */
    lease_db_cleanup();
    count = 0;
    lease_db_foreach(count_callback, &count);

    if (count == 0) {
        TEST_PASS("foreach on empty database iterates 0 times");
    } else {
        TEST_FAIL("foreach on empty database iterates 0 times", "iterated");
    }

    /* NULL callback should not crash */
    lease_db_foreach(NULL, NULL);
    TEST_PASS("foreach with NULL callback does not crash");
}

/* ============================================
 * Test: Local vs Peer statistics
 * ============================================ */
void test_local_peer_statistics(void)
{
    printf("\n=== Test: Local vs Peer statistics ===\n");

    reset_state();

    /* Add local lease (same node_id as config) */
    struct lease_entry lease;
    make_lease(&lease, "192.168.1.1", "aa:bb:cc:dd:ee:ff",
               "local-host", 1000, "test-node");
    lease_db_add(&lease);

    /* Add peer lease (different node_id) */
    make_lease(&lease, "192.168.1.2", "11:22:33:44:55:66",
               "peer-host", 1000, "other-node");
    lease_db_add(&lease);

    if (g_state->local_lease_count == 1) {
        TEST_PASS("Local lease count is 1");
    } else {
        TEST_FAIL("Local lease count is 1", "wrong count");
    }

    if (g_state->peer_lease_count == 1) {
        TEST_PASS("Peer lease count is 1");
    } else {
        TEST_FAIL("Peer lease count is 1", "wrong count");
    }

    /* Update local to peer */
    make_lease(&lease, "192.168.1.1", "aa:bb:cc:dd:ee:ff",
               "local-host", 2000, "other-node");
    lease_db_update(&lease);

    if (g_state->local_lease_count == 0) {
        TEST_PASS("Local count decreased after source change");
    } else {
        TEST_FAIL("Local count decreased after source change", "wrong count");
    }

    if (g_state->peer_lease_count == 2) {
        TEST_PASS("Peer count increased after source change");
    } else {
        TEST_FAIL("Peer count increased after source change", "wrong count");
    }
}

/* ============================================
 * Test: NULL parameter handling
 * ============================================ */
void test_null_handling(void)
{
    printf("\n=== Test: NULL parameter handling ===\n");

    reset_state();

    struct lease_entry lease;
    make_lease(&lease, "192.168.1.1", "aa:bb:cc:dd:ee:ff",
               "host", 1000, "test-node");

    /* NULL entry */
    if (lease_db_add(NULL) == -1) {
        TEST_PASS("lease_db_add(NULL) returns -1");
    } else {
        TEST_FAIL("lease_db_add(NULL) returns -1", "wrong return");
    }

    if (lease_db_update(NULL) == -1) {
        TEST_PASS("lease_db_update(NULL) returns -1");
    } else {
        TEST_FAIL("lease_db_update(NULL) returns -1", "wrong return");
    }

    if (lease_db_delete(NULL) == -1) {
        TEST_PASS("lease_db_delete(NULL) returns -1");
    } else {
        TEST_FAIL("lease_db_delete(NULL) returns -1", "wrong return");
    }

    /* NULL g_state */
    g_state = NULL;
    if (lease_db_count() == 0) {
        TEST_PASS("lease_db_count with NULL g_state returns 0");
    } else {
        TEST_FAIL("lease_db_count with NULL g_state returns 0", "wrong return");
    }
    g_state = &state_storage;
}

/* ============================================
 * Test: Hash collision handling
 * ============================================ */
void test_hash_collision(void)
{
    printf("\n=== Test: Hash collision handling ===\n");

    reset_state();

    /* Add many leases to increase collision likelihood */
    for (int i = 0; i < HASH_TABLE_SIZE + 100; i++) {
        struct lease_entry lease;
        char ip[32];
        /* Use a pattern likely to cause collisions */
        snprintf(ip, sizeof(ip), "10.%d.%d.%d", (i >> 16) & 0xFF, (i >> 8) & 0xFF, i & 0xFF);
        make_lease(&lease, ip, "aa:bb:cc:dd:ee:ff", "host", 1000 + i, "test-node");
        lease_db_add(&lease);
    }

    int expected = HASH_TABLE_SIZE + 100;
    if (lease_db_count() == expected) {
        TEST_PASS("All entries stored despite collisions");
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected %d, got %d", expected, lease_db_count());
        TEST_FAIL("All entries stored despite collisions", msg);
    }

    /* Verify we can find specific entries */
    struct lease_entry *found = lease_db_find("10.0.0.50");
    if (found != NULL) {
        TEST_PASS("Can find entry in collision chain");
    } else {
        TEST_FAIL("Can find entry in collision chain", "not found");
    }

    found = lease_db_find("10.0.4.99");  /* Last entry (i = 1123) */
    if (found != NULL) {
        TEST_PASS("Can find high-index entry");
    } else {
        TEST_FAIL("Can find high-index entry", "not found");
    }
}

/* ============================================
 * Main Test Runner
 * ============================================ */
int main(void)
{
    printf("=========================================\n");
    printf("lease-sync Lease Database Unit Tests\n");
    printf("(linked against daemon/lease-db.c)\n");
    printf("=========================================\n");

    test_lease_db_init();
    test_lease_db_add_basic();
    test_lease_db_add_multiple();
    test_lease_db_find_nonexistent();
    test_lease_db_update_basic();
    test_lease_db_update_conflict_newer_wins();
    test_lease_db_update_mac_conflict();
    test_lease_db_update_creates_new();
    test_lease_db_delete();
    test_lease_db_cleanup();
    test_lease_db_foreach();
    test_local_peer_statistics();
    test_null_handling();
    test_hash_collision();

    return print_test_summary();
}
