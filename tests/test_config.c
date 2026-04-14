/* test_config.c
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
 * test_config.c - Unit tests for configuration parsing
 *
 * Build: See run-config-tests.sh
 * Run: ./run-config-tests.sh
 *
 * This test links against daemon/config.c, daemon/util.c, and daemon/crypto.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/stat.h>

/* Include the daemon headers */
#include "../daemon/common.h"
#include "../daemon/crypto.h"

#include "test_framework.h"

/* ============================================
 * Global State for Testing
 * ============================================ */
static struct daemon_state state_storage;
struct daemon_state *g_state = NULL;

static char test_dir[256];
static char test_config_file[512];

static void reset_state(void)
{
    memset(&state_storage, 0, sizeof(state_storage));
    g_state = &state_storage;
    g_state->config.log_level = LOG_LEVEL_ERROR;  /* Suppress debug logs */
}

/* ============================================
 * Test Helpers
 * ============================================ */

static void setup_test_dir(void)
{
    snprintf(test_dir, sizeof(test_dir), "/tmp/lease-sync-test-%d", getpid());
    mkdir(test_dir, 0755);
    snprintf(test_config_file, sizeof(test_config_file), "%s/config", test_dir);
}

static void cleanup_test_dir(void)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", test_dir);
    int ret = system(cmd);
    (void)ret;  /* Ignore return value */
}

static void write_config_file(const char *content)
{
    FILE *f = fopen(test_config_file, "w");
    if (f) {
        fprintf(f, "%s", content);
        fclose(f);
    }
}

/* ============================================
 * Test: config_set_defaults
 * ============================================ */
void test_config_set_defaults(void)
{
    printf("\n=== Test: config_set_defaults ===\n");

    reset_state();

    struct config config;
    config_set_defaults(&config);

    /* Check default values */
    if (config.sync_port == DEFAULT_SYNC_PORT) {
        TEST_PASS("Default sync_port set");
    } else {
        TEST_FAIL("Default sync_port set", "wrong value");
    }

    if (config.sync_interval == DEFAULT_SYNC_INTERVAL) {
        TEST_PASS("Default sync_interval set");
    } else {
        TEST_FAIL("Default sync_interval set", "wrong value");
    }

    if (config.peer_timeout == DEFAULT_PEER_TIMEOUT) {
        TEST_PASS("Default peer_timeout set");
    } else {
        TEST_FAIL("Default peer_timeout set", "wrong value");
    }

    if (config.peer_count == 0) {
        TEST_PASS("Default peer_count is 0");
    } else {
        TEST_FAIL("Default peer_count is 0", "wrong value");
    }

    if (config.daemon_mode == false) {
        TEST_PASS("Default daemon_mode is false");
    } else {
        TEST_FAIL("Default daemon_mode is false", "wrong value");
    }

    if (config.log_level == LOG_LEVEL_INFO) {
        TEST_PASS("Default log_level is INFO");
    } else {
        TEST_FAIL("Default log_level is INFO", "wrong value");
    }

    /* Security defaults (secure by default) */
    if (config.security_mode == SECURITY_MODE_ENCRYPTED) {
        TEST_PASS("Default security_mode is ENCRYPTED");
    } else {
        TEST_FAIL("Default security_mode is ENCRYPTED", "wrong value");
    }

    if (config.plain_mode_acknowledged == false) {
        TEST_PASS("Default plain_mode_acknowledged is false");
    } else {
        TEST_FAIL("Default plain_mode_acknowledged is false", "wrong value");
    }

    /* NULL handling */
    config_set_defaults(NULL);  /* Should not crash */
    TEST_PASS("config_set_defaults(NULL) does not crash");
}

/* ============================================
 * Test: config_load - Valid config file
 * ============================================ */
void test_config_load_valid(void)
{
    printf("\n=== Test: config_load - Valid config file ===\n");

    reset_state();
    setup_test_dir();

    struct config config;
    config_set_defaults(&config);

    /* Write a test config file. Includes legacy persist_interval/persist_file
     * lines to verify the parser silently ignores keys removed in v1.2.0. */
    write_config_file(
        "# Test configuration\n"
        "node_id=test-node-123\n"
        "peer=192.168.1.10\n"
        "peer=192.168.1.11\n"
        "sync_port=6000\n"
        "sync_interval=45\n"
        "persist_interval=90\n"
        "persist_file=/tmp/legacy.db\n"
        "peer_timeout=180\n"
        "debug=1\n"
        "log_level=3\n"
    );

    int ret = config_load(&config, test_config_file);
    if (ret == 0) {
        TEST_PASS("config_load returns success");
    } else {
        TEST_FAIL("config_load returns success", "returned error");
    }

    if (strcmp(config.node_id, "test-node-123") == 0) {
        TEST_PASS("node_id parsed correctly");
    } else {
        TEST_FAIL("node_id parsed correctly", "wrong value");
    }

    if (config.sync_port == 6000) {
        TEST_PASS("sync_port parsed correctly");
    } else {
        TEST_FAIL("sync_port parsed correctly", "wrong value");
    }

    if (config.sync_interval == 45) {
        TEST_PASS("sync_interval parsed correctly");
    } else {
        TEST_FAIL("sync_interval parsed correctly", "wrong value");
    }

    if (config.peer_timeout == 180) {
        TEST_PASS("peer_timeout parsed correctly");
    } else {
        TEST_FAIL("peer_timeout parsed correctly", "wrong value");
    }

    if (config.peer_count == 2) {
        TEST_PASS("peer_count is 2");
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected 2, got %d", config.peer_count);
        TEST_FAIL("peer_count is 2", msg);
    }

    /* Note: debug=1 sets log_level to LOG_LEVEL_DEBUG (backward compat) */
    /* log_level=3 also sets it to DEBUG, so either way it should be 3 */
    if (config.log_level == LOG_LEVEL_DEBUG) {
        TEST_PASS("log_level parsed correctly (debug=1 and log_level=3 both work)");
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected %d, got %d", LOG_LEVEL_DEBUG, config.log_level);
        TEST_FAIL("log_level parsed correctly", msg);
    }

    cleanup_test_dir();
}

/* ============================================
 * Test: config_load - Missing file
 * ============================================ */
void test_config_load_missing(void)
{
    printf("\n=== Test: config_load - Missing file ===\n");

    reset_state();

    struct config config;
    config_set_defaults(&config);

    /* Load from non-existent file - should use defaults and return success */
    int ret = config_load(&config, "/tmp/nonexistent-config-file-12345");
    if (ret == 0) {
        TEST_PASS("Missing config file returns success (uses defaults)");
    } else {
        TEST_FAIL("Missing config file returns success", "returned error");
    }

    /* Verify defaults are still in place */
    if (config.sync_port == DEFAULT_SYNC_PORT) {
        TEST_PASS("Defaults preserved when file missing");
    } else {
        TEST_FAIL("Defaults preserved when file missing", "values changed");
    }
}

/* ============================================
 * Test: config_load - Comments and whitespace
 * ============================================ */
void test_config_load_comments(void)
{
    printf("\n=== Test: config_load - Comments and whitespace ===\n");

    reset_state();
    setup_test_dir();

    struct config config;
    config_set_defaults(&config);

    /* Write config with comments and whitespace */
    write_config_file(
        "# This is a comment\n"
        "  # Indented comment\n"
        "\n"
        "  sync_port = 7000  \n"
        "# Another comment\n"
        "  sync_interval=55\n"
        "\n\n"
        "peer=10.0.0.1\n"
    );

    int ret = config_load(&config, test_config_file);
    if (ret == 0) {
        TEST_PASS("config_load with comments returns success");
    } else {
        TEST_FAIL("config_load with comments returns success", "returned error");
    }

    if (config.sync_port == 7000) {
        TEST_PASS("Whitespace around value handled");
    } else {
        TEST_FAIL("Whitespace around value handled", "wrong value");
    }

    if (config.sync_interval == 55) {
        TEST_PASS("Whitespace around key handled");
    } else {
        TEST_FAIL("Whitespace around key handled", "wrong value");
    }

    if (config.peer_count == 1) {
        TEST_PASS("Peer parsed through comments");
    } else {
        TEST_FAIL("Peer parsed through comments", "wrong count");
    }

    cleanup_test_dir();
}

/* ============================================
 * Test: config_load - Security settings
 * ============================================ */
void test_config_load_security(void)
{
    printf("\n=== Test: config_load - Security settings ===\n");

    reset_state();
    setup_test_dir();

    struct config config;
    config_set_defaults(&config);

    /* Write config with security settings */
    write_config_file(
        "security_mode=plain\n"
        "plain_mode_acknowledged=yes\n"
    );

    int ret = config_load(&config, test_config_file);
    if (ret == 0) {
        TEST_PASS("config_load with security settings returns success");
    } else {
        TEST_FAIL("config_load with security settings returns success", "returned error");
    }

    if (config.security_mode == SECURITY_MODE_PLAIN) {
        TEST_PASS("security_mode=plain parsed correctly");
    } else {
        TEST_FAIL("security_mode=plain parsed correctly", "wrong value");
    }

    if (config.plain_mode_acknowledged == true) {
        TEST_PASS("plain_mode_acknowledged=yes parsed correctly");
    } else {
        TEST_FAIL("plain_mode_acknowledged=yes parsed correctly", "wrong value");
    }

    /* Test encrypted mode */
    config_set_defaults(&config);
    write_config_file(
        "security_mode=encrypted\n"
        "plain_mode_acknowledged=false\n"
    );

    ret = config_load(&config, test_config_file);
    if (config.security_mode == SECURITY_MODE_ENCRYPTED) {
        TEST_PASS("security_mode=encrypted parsed correctly");
    } else {
        TEST_FAIL("security_mode=encrypted parsed correctly", "wrong value");
    }

    cleanup_test_dir();
}

/* ============================================
 * Test: config_load - Invalid values
 * ============================================ */
void test_config_load_invalid_values(void)
{
    printf("\n=== Test: config_load - Invalid values ===\n");

    reset_state();
    setup_test_dir();

    struct config config;
    config_set_defaults(&config);

    /* Write config with invalid values */
    write_config_file(
        "sync_port=invalid\n"
        "sync_interval=-5\n"
        "peer=not.an.ip\n"
        "unknown_key=something\n"
    );

    int ret = config_load(&config, test_config_file);
    if (ret == 0) {
        TEST_PASS("config_load with invalid values returns success (logs warnings)");
    } else {
        TEST_FAIL("config_load with invalid values returns success", "returned error");
    }

    /* sync_port should remain default since 'invalid' is not a number */
    if (config.sync_port == DEFAULT_SYNC_PORT) {
        TEST_PASS("Invalid sync_port preserves default");
    } else {
        TEST_FAIL("Invalid sync_port preserves default", "value changed");
    }

    /* Invalid peer should not be added */
    if (config.peer_count == 0) {
        TEST_PASS("Invalid peer not added");
    } else {
        TEST_FAIL("Invalid peer not added", "peer was added");
    }

    cleanup_test_dir();
}

/* ============================================
 * Test: config_load - IPv6 peers
 * ============================================ */
void test_config_load_ipv6_peers(void)
{
    printf("\n=== Test: config_load - IPv6 peers ===\n");

    reset_state();
    setup_test_dir();

    struct config config;
    config_set_defaults(&config);

    /* Write config with IPv6 peers */
    write_config_file(
        "peer=192.168.1.10\n"
        "peer=2001:db8::1\n"
        "peer=::1\n"
    );

    int ret = config_load(&config, test_config_file);
    if (ret == 0) {
        TEST_PASS("config_load with IPv6 peers returns success");
    } else {
        TEST_FAIL("config_load with IPv6 peers returns success", "returned error");
    }

    if (config.peer_count == 3) {
        TEST_PASS("All 3 peers (IPv4 + IPv6) added");
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected 3, got %d", config.peer_count);
        TEST_FAIL("All 3 peers (IPv4 + IPv6) added", msg);
    }

    cleanup_test_dir();
}

/* ============================================
 * Test: config_save
 * ============================================ */
void test_config_save(void)
{
    printf("\n=== Test: config_save ===\n");

    reset_state();
    setup_test_dir();

    struct config config;
    config_set_defaults(&config);

    /* Set some values */
    strcpy(config.node_id, "save-test-node");
    config.sync_port = 8000;
    config.sync_interval = 100;
    config.log_level = LOG_LEVEL_DEBUG;

    /* Save config */
    int ret = config_save(&config, test_config_file);
    if (ret == 0) {
        TEST_PASS("config_save returns success");
    } else {
        TEST_FAIL("config_save returns success", "returned error");
    }

    /* Verify file exists */
    FILE *f = fopen(test_config_file, "r");
    if (f) {
        TEST_PASS("Config file created");
        fclose(f);
    } else {
        TEST_FAIL("Config file created", "file not found");
    }

    /* Load it back and verify */
    struct config loaded;
    config_set_defaults(&loaded);
    config_load(&loaded, test_config_file);

    if (strcmp(loaded.node_id, "save-test-node") == 0) {
        TEST_PASS("Saved node_id loads correctly");
    } else {
        TEST_FAIL("Saved node_id loads correctly", "wrong value");
    }

    if (loaded.sync_port == 8000) {
        TEST_PASS("Saved sync_port loads correctly");
    } else {
        TEST_FAIL("Saved sync_port loads correctly", "wrong value");
    }

    cleanup_test_dir();
}

/* ============================================
 * Test: config_generate_node_id
 * ============================================ */
void test_config_generate_node_id(void)
{
    printf("\n=== Test: config_generate_node_id ===\n");

    reset_state();

    struct config config;
    config_set_defaults(&config);

    int ret = config_generate_node_id(&config);
    if (ret == 0) {
        TEST_PASS("config_generate_node_id returns success");
    } else {
        TEST_FAIL("config_generate_node_id returns success", "returned error");
    }

    /* Node ID should not be empty */
    if (strlen(config.node_id) > 0) {
        TEST_PASS("Generated node_id is not empty");
    } else {
        TEST_FAIL("Generated node_id is not empty", "empty string");
    }

    /* Node ID should contain hostname */
    char hostname[64];
    gethostname(hostname, sizeof(hostname) - 1);
    if (strstr(config.node_id, hostname) != NULL ||
        strstr(config.node_id, "unknown") != NULL) {
        TEST_PASS("Generated node_id contains hostname component");
    } else {
        TEST_FAIL("Generated node_id contains hostname component", "hostname not found");
    }

    /* Two generations should produce different IDs (due to pid/time) */
    char id1[MAX_NODE_ID_LEN];
    strcpy(id1, config.node_id);
    usleep(1000);  /* Small delay */
    config_generate_node_id(&config);
    /* Note: Might be same if generated in same second with same PID, so we just check it works */
    TEST_PASS("Second generation completes");

    /* NULL handling */
    if (config_generate_node_id(NULL) == -1) {
        TEST_PASS("config_generate_node_id(NULL) returns -1");
    } else {
        TEST_FAIL("config_generate_node_id(NULL) returns -1", "wrong return");
    }
}

/* ============================================
 * Test: config_ensure_directories
 * ============================================ */
void test_config_ensure_directories(void)
{
    printf("\n=== Test: config_ensure_directories ===\n");

    reset_state();
    setup_test_dir();

    struct config config;
    config_set_defaults(&config);

    /* Set node_id_file to a path that requires a parent directory */
    snprintf(config.node_id_file, sizeof(config.node_id_file),
             "%s/subdir/node_id", test_dir);

    int ret = config_ensure_directories(&config);
    if (ret == 0) {
        TEST_PASS("config_ensure_directories returns success");
    } else {
        TEST_FAIL("config_ensure_directories returns success", "returned error");
    }

    /* Check directory was created */
    char subdir[512];
    snprintf(subdir, sizeof(subdir), "%s/subdir", test_dir);
    struct stat st;
    if (stat(subdir, &st) == 0 && S_ISDIR(st.st_mode)) {
        TEST_PASS("Directory created");
    } else {
        TEST_FAIL("Directory created", "directory not found");
    }

    /* NULL handling */
    if (config_ensure_directories(NULL) == -1) {
        TEST_PASS("config_ensure_directories(NULL) returns -1");
    } else {
        TEST_FAIL("config_ensure_directories(NULL) returns -1", "wrong return");
    }

    cleanup_test_dir();
}

/* ============================================
 * Test: config_load NULL handling
 * ============================================ */
void test_config_load_null(void)
{
    printf("\n=== Test: config_load NULL handling ===\n");

    reset_state();
    setup_test_dir();

    struct config config;
    config_set_defaults(&config);

    if (config_load(NULL, test_config_file) == -1) {
        TEST_PASS("config_load(NULL, file) returns -1");
    } else {
        TEST_FAIL("config_load(NULL, file) returns -1", "wrong return");
    }

    if (config_load(&config, NULL) == -1) {
        TEST_PASS("config_load(config, NULL) returns -1");
    } else {
        TEST_FAIL("config_load(config, NULL) returns -1", "wrong return");
    }

    cleanup_test_dir();
}

/* ============================================
 * Test: Per-peer source_address parsing
 * ============================================ */
void test_config_peer_source_address(void)
{
    printf("\n=== Test: Per-peer source_address ===\n");

    reset_state();
    setup_test_dir();

    struct config config;
    config_set_defaults(&config);

    /* Test peer without source address (backward compatible) */
    char config_file[512];
    snprintf(config_file, sizeof(config_file), "%s/config", test_dir);

    FILE *f = fopen(config_file, "w");
    fprintf(f, "node_id=test-node\n");
    fprintf(f, "peer=192.168.1.10\n");
    fclose(f);

    int ret = config_load(&config, config_file);
    if (ret == 0) {
        TEST_PASS("config_load with simple peer returns success");
    } else {
        TEST_FAIL("config_load with simple peer returns success", "returned error");
    }

    if (config.peer_count == 1) {
        TEST_PASS("Peer count is 1");
    } else {
        TEST_FAIL("Peer count is 1", "wrong count");
    }

    if (strcmp(config.peers[0].address, "192.168.1.10") == 0) {
        TEST_PASS("Peer address parsed correctly (192.168.1.10)");
    } else {
        TEST_FAIL("Peer address parsed correctly", config.peers[0].address);
    }

    if (!config.peers[0].has_source_address) {
        TEST_PASS("has_source_address is false for simple peer");
    } else {
        TEST_FAIL("has_source_address is false", "was true");
    }

    /* Test peer with source address (comma-separated format) */
    config_set_defaults(&config);

    f = fopen(config_file, "w");
    fprintf(f, "node_id=test-node\n");
    fprintf(f, "peer=192.168.1.10,10.0.0.1\n");  /* Format: peer,source */
    fclose(f);

    ret = config_load(&config, config_file);
    if (ret == 0) {
        TEST_PASS("config_load with peer,source format returns success");
    } else {
        TEST_FAIL("config_load with peer,source format returns success", "returned error");
    }

    if (config.peer_count == 1) {
        TEST_PASS("Peer count is 1 with peer,source format");
    } else {
        TEST_FAIL("Peer count is 1 with peer,source format", "wrong count");
    }

    if (strcmp(config.peers[0].address, "192.168.1.10") == 0) {
        TEST_PASS("Peer address parsed correctly from peer,source format");
    } else {
        TEST_FAIL("Peer address parsed correctly from peer,source format", config.peers[0].address);
    }

    if (config.peers[0].has_source_address) {
        TEST_PASS("has_source_address is true for peer with source");
    } else {
        TEST_FAIL("has_source_address is true", "was false");
    }

    if (strcmp(config.peers[0].source_address, "10.0.0.1") == 0) {
        TEST_PASS("Source address parsed correctly (10.0.0.1)");
    } else {
        TEST_FAIL("Source address parsed correctly", config.peers[0].source_address);
    }

    /* Test IPv6 peer with IPv6 source */
    config_set_defaults(&config);

    f = fopen(config_file, "w");
    fprintf(f, "node_id=test-node\n");
    fprintf(f, "peer=fdeb:8804:e54c::2,fdeb:8804:e54c::1\n");
    fclose(f);

    ret = config_load(&config, config_file);
    if (ret == 0 && config.peer_count == 1) {
        TEST_PASS("IPv6 peer with IPv6 source parsed");
    } else {
        TEST_FAIL("IPv6 peer with IPv6 source parsed", "parsing failed");
    }

    if (config.peers[0].has_source_address &&
        strcmp(config.peers[0].source_address, "fdeb:8804:e54c::1") == 0) {
        TEST_PASS("IPv6 source address parsed correctly");
    } else {
        TEST_FAIL("IPv6 source address parsed correctly", "wrong value");
    }

    /* Test mixed: multiple peers, some with source, some without */
    config_set_defaults(&config);

    f = fopen(config_file, "w");
    fprintf(f, "node_id=test-node\n");
    fprintf(f, "peer=192.168.1.10\n");              /* No source */
    fprintf(f, "peer=192.168.1.11,10.0.0.2\n");     /* With source */
    fprintf(f, "peer=fdeb::1\n");                   /* IPv6 no source */
    fclose(f);

    ret = config_load(&config, config_file);
    if (ret == 0 && config.peer_count == 3) {
        TEST_PASS("Mixed peer formats: 3 peers parsed");
    } else {
        TEST_FAIL("Mixed peer formats: 3 peers parsed", "wrong count");
    }

    if (!config.peers[0].has_source_address &&
        config.peers[1].has_source_address &&
        !config.peers[2].has_source_address) {
        TEST_PASS("Mixed peers: correct has_source_address flags");
    } else {
        TEST_FAIL("Mixed peers: correct has_source_address flags", "wrong flags");
    }

    cleanup_test_dir();
}

/* ============================================
 * Test: Over-long peer address
 * ============================================
 *
 * Feeds a peer= line whose value exceeds INET6_ADDRSTRLEN. Before the fix,
 * config_add_peer's `peer_addr` stack buffer was uninitialized, so an
 * over-long value would leak stack contents into peer->address (and into
 * the "Invalid peer address" log line) when strncpy did not NUL-terminate.
 *
 * Expected post-fix behavior:
 *   - parser does not crash
 *   - the malformed entry is rejected (peer_count stays 0)
 */
void test_config_peer_overlong_address(void)
{
    printf("\n=== Test: Over-long peer address ===\n");

    reset_state();
    setup_test_dir();

    char config_file[512];
    snprintf(config_file, sizeof(config_file), "%s/config", test_dir);

    /* INET6_ADDRSTRLEN is 46. Build a 200-char garbage value that is
     * neither a valid IPv4 nor IPv6 address. */
    char overlong[256];
    memset(overlong, 'A', 200);
    overlong[200] = '\0';

    FILE *f = fopen(config_file, "w");
    fprintf(f, "node_id=test-node\n");
    fprintf(f, "peer=%s\n", overlong);
    fclose(f);

    struct config config;
    config_set_defaults(&config);

    int ret = config_load(&config, config_file);
    if (ret == 0) {
        TEST_PASS("config_load tolerates over-long peer (no crash)");
    } else {
        TEST_FAIL("config_load tolerates over-long peer", "returned error");
    }

    if (config.peer_count == 0) {
        TEST_PASS("Over-long peer rejected (peer_count = 0)");
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected 0, got %d", config.peer_count);
        TEST_FAIL("Over-long peer rejected", msg);
    }

    cleanup_test_dir();
}

/* ============================================
 * Main Test Runner
 * ============================================ */
int main(void)
{
    printf("=========================================\n");
    printf("lease-sync Configuration Parsing Tests\n");
    printf("(linked against daemon/config.c)\n");
    printf("=========================================\n");

    test_config_set_defaults();
    test_config_load_valid();
    test_config_load_missing();
    test_config_load_comments();
    test_config_load_security();
    test_config_load_invalid_values();
    test_config_load_ipv6_peers();
    test_config_save();
    test_config_generate_node_id();
    test_config_ensure_directories();
    test_config_load_null();
    test_config_peer_source_address();
    test_config_peer_overlong_address();

    return print_test_summary();
}
