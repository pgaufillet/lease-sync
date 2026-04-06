/* test_peer_sync.c
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
 * test_peer_sync.c - Unit tests for peer sync functions (IPv4/IPv6)
 *
 * Build: See run-peer-sync-tests.sh
 * Run: ./run-peer-sync-tests.sh
 *
 * Tests dual-stack socket creation for peer communication.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Include the daemon's common.h for real structure definitions */
#include "../daemon/common.h"

/* ============================================
 * Stub implementations for functions we don't test
 * ============================================ */

/* Stub for lease_db functions */
struct lease_entry *lease_db_find(const char *ip __attribute__((unused)))
{
    return NULL;
}

int lease_db_add(struct lease_entry *entry __attribute__((unused)))
{
    return 0;
}

int lease_db_update(struct lease_entry *entry __attribute__((unused)))
{
    return 0;
}

int lease_db_delete(const char *ip __attribute__((unused)))
{
    return 0;
}

/* Stub for ubus_handler functions */
int ubus_handler_inject_lease(struct lease_entry *entry __attribute__((unused)))
{
    return 0;
}

int ubus_handler_delete_lease(const char *ip __attribute__((unused)))
{
    return 0;
}

/* Stub for retry_queue functions */
int retry_queue_add(struct retry_queue *queue __attribute__((unused)),
                    struct lease_entry *entry __attribute__((unused)),
                    uint8_t action __attribute__((unused)))
{
    return 0;
}

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
    g_state->config.sync_socket = -1;
    g_state->config.sync_port = 15353;  /* Use non-privileged port for testing */
}

/* ============================================
 * Test: Dual-stack socket creation
 * ============================================ */
void test_dual_stack_socket(void)
{
    printf("\n=== Test: Dual-stack socket creation ===\n");

    reset_state();

    /* Create IPv6 dual-stack socket */
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) {
        TEST_FAIL("create IPv6 socket", strerror(errno));
        return;
    }
    TEST_PASS("create IPv6 socket");

    /* Enable dual-stack mode */
    int v6only = 0;
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) < 0) {
        TEST_FAIL("set IPV6_V6ONLY=0", strerror(errno));
        close(sock);
        return;
    }
    TEST_PASS("set IPV6_V6ONLY=0 (dual-stack enabled)");

    /* Bind to any address */
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = htons(g_state->config.sync_port);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        TEST_FAIL("bind to port", strerror(errno));
        close(sock);
        return;
    }
    TEST_PASS("bind to IPv6 any address (::)");

    close(sock);
}

/* ============================================
 * Test: peer_sync_init with dual-stack
 * ============================================ */
/* Forward declaration - we link against peer-sync.c */
extern int peer_sync_init(struct daemon_state *state);
extern void peer_sync_cleanup(void);

void test_peer_sync_init_dual_stack(void)
{
    printf("\n=== Test: peer_sync_init dual-stack ===\n");

    reset_state();
    g_state->config.sync_port = 15354;  /* Different port to avoid conflict */

    int ret = peer_sync_init(g_state);
    if (ret != 0) {
        TEST_FAIL("peer_sync_init", "initialization failed");
        return;
    }
    TEST_PASS("peer_sync_init succeeds");

    if (g_state->config.sync_socket < 0) {
        TEST_FAIL("sync_socket valid", "socket is negative");
        return;
    }
    TEST_PASS("sync_socket is valid");

    /* Check if we got an IPv6 socket (dual-stack) or IPv4 fallback.
     * On systems with IPv6 support, we should get dual-stack.
     * On IPv4-only systems, getsockopt for IPV6_V6ONLY will fail.
     */
    int v6only;
    socklen_t optlen = sizeof(v6only);
    if (getsockopt(g_state->config.sync_socket, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, &optlen) < 0) {
        /* This means we got an IPv4 socket (fallback mode) - still valid */
        TEST_PASS("socket is IPv4 (fallback mode on IPv4-only system)");
    } else {
        TEST_PASS("socket is IPv6 (getsockopt IPV6_V6ONLY works)");

        if (v6only != 0) {
            TEST_FAIL("dual-stack enabled", "IPV6_V6ONLY is not 0");
            peer_sync_cleanup();
            return;
        }
        TEST_PASS("dual-stack enabled (IPV6_V6ONLY=0)");
    }

    peer_sync_cleanup();
    TEST_PASS("peer_sync_cleanup");
}

/* ============================================
 * Test: IPv4 peer address sendto compatibility
 * ============================================ */
void test_ipv4_peer_address_format(void)
{
    printf("\n=== Test: IPv4 peer address format ===\n");

    /* Test that we can parse IPv4 addresses into sockaddr_in */
    struct sockaddr_in addr4;
    memset(&addr4, 0, sizeof(addr4));
    addr4.sin_family = AF_INET;
    addr4.sin_port = htons(5378);

    if (inet_pton(AF_INET, "192.168.1.2", &addr4.sin_addr) != 1) {
        TEST_FAIL("parse IPv4 address", "inet_pton failed");
        return;
    }
    TEST_PASS("parse IPv4 address (192.168.1.2)");
}

/* ============================================
 * Test: IPv6 peer address sendto compatibility
 * ============================================ */
void test_ipv6_peer_address_format(void)
{
    printf("\n=== Test: IPv6 peer address format ===\n");

    /* Test that we can parse IPv6 addresses into sockaddr_in6 */
    struct sockaddr_in6 addr6;
    memset(&addr6, 0, sizeof(addr6));
    addr6.sin6_family = AF_INET6;
    addr6.sin6_port = htons(5378);

    if (inet_pton(AF_INET6, "fdeb:8804:e54c::2", &addr6.sin6_addr) != 1) {
        TEST_FAIL("parse IPv6 address", "inet_pton failed");
        return;
    }
    TEST_PASS("parse IPv6 address (fdeb:8804:e54c::2)");

    /* Also test link-local */
    if (inet_pton(AF_INET6, "fe80::1", &addr6.sin6_addr) != 1) {
        TEST_FAIL("parse link-local IPv6", "inet_pton failed");
        return;
    }
    TEST_PASS("parse link-local IPv6 (fe80::1)");
}

/* ============================================
 * Test: Send to IPv6 peer (loopback)
 * ============================================ */
void test_send_to_ipv6_peer(void)
{
    printf("\n=== Test: Send to IPv6 peer ===\n");

    reset_state();
    g_state->config.sync_port = 15355;

    /* Initialize peer sync */
    if (peer_sync_init(g_state) != 0) {
        TEST_FAIL("peer_sync_init", "initialization failed");
        return;
    }

    /* Create a receiver socket on loopback */
    int recv_sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (recv_sock < 0) {
        TEST_FAIL("create receiver socket", strerror(errno));
        peer_sync_cleanup();
        return;
    }

    struct sockaddr_in6 recv_addr;
    memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin6_family = AF_INET6;
    recv_addr.sin6_addr = in6addr_loopback;  /* ::1 */
    recv_addr.sin6_port = htons(15356);

    if (bind(recv_sock, (struct sockaddr*)&recv_addr, sizeof(recv_addr)) < 0) {
        TEST_FAIL("bind receiver", strerror(errno));
        close(recv_sock);
        peer_sync_cleanup();
        return;
    }

    /* Try to send a test message to the IPv6 loopback address */
    const char *test_msg = "test";
    struct sockaddr_in6 dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin6_family = AF_INET6;
    dest_addr.sin6_addr = in6addr_loopback;
    dest_addr.sin6_port = htons(15356);

    ssize_t sent = sendto(g_state->config.sync_socket, test_msg, strlen(test_msg), 0,
                          (struct sockaddr*)&dest_addr, sizeof(dest_addr));

    if (sent < 0) {
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "sendto failed: %s", strerror(errno));
        TEST_FAIL("send to IPv6 loopback (::1)", err_msg);
        close(recv_sock);
        peer_sync_cleanup();
        return;
    }
    TEST_PASS("send to IPv6 loopback (::1)");

    /* Verify message was received */
    char buf[64];
    struct sockaddr_in6 from_addr;
    socklen_t from_len = sizeof(from_addr);

    /* Set receive timeout to avoid hanging */
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(recv_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ssize_t received = recvfrom(recv_sock, buf, sizeof(buf), 0,
                                (struct sockaddr*)&from_addr, &from_len);

    if (received < 0) {
        TEST_FAIL("receive from IPv6", strerror(errno));
    } else if (received != (ssize_t)strlen(test_msg)) {
        TEST_FAIL("receive length", "wrong length");
    } else {
        TEST_PASS("receive from IPv6 loopback");
    }

    close(recv_sock);
    peer_sync_cleanup();
}

/* ============================================
 * Test: Send to IPv4 peer via dual-stack
 * ============================================ */
void test_send_to_ipv4_peer_via_dual_stack(void)
{
    printf("\n=== Test: Send to IPv4 peer via dual-stack ===\n");

    reset_state();
    g_state->config.sync_port = 15357;

    /* Initialize peer sync (creates IPv6 dual-stack socket) */
    if (peer_sync_init(g_state) != 0) {
        TEST_FAIL("peer_sync_init", "initialization failed");
        return;
    }

    /* Create a receiver socket on IPv4 loopback */
    int recv_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (recv_sock < 0) {
        TEST_FAIL("create IPv4 receiver socket", strerror(errno));
        peer_sync_cleanup();
        return;
    }

    struct sockaddr_in recv_addr;
    memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  /* 127.0.0.1 */
    recv_addr.sin_port = htons(15358);

    if (bind(recv_sock, (struct sockaddr*)&recv_addr, sizeof(recv_addr)) < 0) {
        TEST_FAIL("bind IPv4 receiver", strerror(errno));
        close(recv_sock);
        peer_sync_cleanup();
        return;
    }

    /* Send to IPv4 address - must use sockaddr_in directly
     * Note: The peer_sync code stores addresses in sockaddr_storage,
     * so when we have an IPv4 peer, we send with AF_INET sockaddr_in.
     * This works because the dual-stack socket can send to IPv4 via
     * IPv4-mapped addresses internally.
     */
    const char *test_msg = "test4";
    ssize_t sent = sendto(g_state->config.sync_socket, test_msg, strlen(test_msg), 0,
                          (struct sockaddr*)&recv_addr, sizeof(recv_addr));

    if (sent < 0) {
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "sendto failed: %s", strerror(errno));
        TEST_FAIL("send to IPv4 loopback (127.0.0.1)", err_msg);
        close(recv_sock);
        peer_sync_cleanup();
        return;
    }
    TEST_PASS("send to IPv4 loopback (127.0.0.1) via dual-stack socket");

    /* Verify message was received */
    char buf[64];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    /* Set receive timeout to avoid hanging */
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(recv_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ssize_t received = recvfrom(recv_sock, buf, sizeof(buf), 0,
                                (struct sockaddr*)&from_addr, &from_len);

    if (received < 0) {
        TEST_FAIL("receive from IPv4", strerror(errno));
    } else if (received != (ssize_t)strlen(test_msg)) {
        TEST_FAIL("receive length", "wrong length");
    } else {
        TEST_PASS("receive from IPv4 loopback via dual-stack");
    }

    close(recv_sock);
    peer_sync_cleanup();
}

/* ============================================
 * Main - Run All Tests
 * ============================================ */
int main(void)
{
    printf("========================================\n");
    printf("lease-sync Peer Sync Unit Tests\n");
    printf("========================================\n");

    /* Basic socket tests */
    test_dual_stack_socket();
    test_ipv4_peer_address_format();
    test_ipv6_peer_address_format();

    /* peer_sync_init tests */
    test_peer_sync_init_dual_stack();

    /* Communication tests */
    test_send_to_ipv6_peer();
    test_send_to_ipv4_peer_via_dual_stack();

    return print_test_summary();
}
