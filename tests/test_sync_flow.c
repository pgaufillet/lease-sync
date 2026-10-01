/*
 * Copyright (C) 2026 Pierre Gaufillet <pierre.gaufillet@bergamote.eu>
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
 * test_sync_flow.c - Lease flow between dnsmasq, lease_db and a peer
 *
 * Build: See run-sync-flow-tests.sh
 * Run: ./run-sync-flow-tests.sh
 *
 * Drives the real daemon code (lease-sync.c and ubus-handler.c are
 * included for their static callbacks; peer-sync.c, lease-db.c and
 * retry-queue.c are linked) against:
 *   - a fake dnsmasq behind ubus_invoke, mirroring the add_lease /
 *     delete_lease / get_leases semantics of the dnsmasq ubus patch
 *     (add overwrites by IP, deleting an absent lease is NOT_FOUND),
 *     whose lease events are delivered to the daemon's event handler;
 *   - a fake peer on a loopback UDP socket (plain mode) that sends sync
 *     messages to the daemon and records what the daemon sends back.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <endian.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define main lease_sync_main
#include "../daemon/lease-sync.c"
#undef main
#include "../daemon/ubus-handler.c"

#include "test_framework.h"

#define DAEMON_PORT 25401
#define PEER_PORT 25402
#define NODE_ID "node-a"
#define PEER_NODE_ID "node-b"

/* ============================================
 * Fake dnsmasq (ubus object)
 * ============================================ */
#define FAKE_MAX_LEASES 16

struct fake_lease
{
    char ip[64];
    char mac[32];
    char hostname[64];
    uint32_t expires;   /* seconds remaining, as get_leases reports it */
};

static struct fake_lease fake_leases[FAKE_MAX_LEASES];
static int fake_count;
static struct fake_lease fake_deleted[FAKE_MAX_LEASES];
static int fake_deleted_count;
static int fake_delete_calls;
static int fake_fail_status;    /* forced ubus_invoke status, 0 = none */
static char fake_ctx;

static int fake_find(const char *ip)
{
    int i;

    for (i = 0; i < fake_count; i++)
        if (strcmp(fake_leases[i].ip, ip) == 0)
            return i;
    return -1;
}

static void fake_put(const char *ip, const char *mac, uint32_t expires)
{
    int i = fake_find(ip);

    if (i < 0)
    {
        if (fake_count >= FAKE_MAX_LEASES)
            return;
        i = fake_count++;
    }
    memset(&fake_leases[i], 0, sizeof(fake_leases[i]));
    strncpy(fake_leases[i].ip, ip, sizeof(fake_leases[i].ip) - 1);
    strncpy(fake_leases[i].mac, mac, sizeof(fake_leases[i].mac) - 1);
    strncpy(fake_leases[i].hostname, "host", sizeof(fake_leases[i].hostname) - 1);
    fake_leases[i].expires = expires;
}

static void fake_remove(int i)
{
    fake_leases[i] = fake_leases[--fake_count];
}

static void fake_reset(void)
{
    fake_count = 0;
    fake_deleted_count = 0;
    fake_delete_calls = 0;
    fake_fail_status = 0;
}

static int fake_add_lease(struct blob_attr *msg)
{
    static const struct blobmsg_policy policy[] =
    {
        { .name = "ip", .type = BLOBMSG_TYPE_STRING },
        { .name = "mac", .type = BLOBMSG_TYPE_STRING },
        { .name = "expires", .type = BLOBMSG_TYPE_INT32 },
    };
    struct blob_attr *tb[3];

    blobmsg_parse(policy, 3, tb, blob_data(msg), blob_len(msg));
    if (!tb[0] || !tb[1] || !tb[2] || blobmsg_get_u32(tb[2]) == 0)
        return UBUS_STATUS_INVALID_ARGUMENT;

    fake_put(blobmsg_get_string(tb[0]), blobmsg_get_string(tb[1]),
             blobmsg_get_u32(tb[2]));
    return UBUS_STATUS_OK;
}

static int fake_delete_lease(struct blob_attr *msg)
{
    static const struct blobmsg_policy policy[] =
    {
        { .name = "ip", .type = BLOBMSG_TYPE_STRING },
    };
    struct blob_attr *tb[1];
    int i;

    blobmsg_parse(policy, 1, tb, blob_data(msg), blob_len(msg));
    if (!tb[0])
        return UBUS_STATUS_INVALID_ARGUMENT;

    fake_delete_calls++;
    i = fake_find(blobmsg_get_string(tb[0]));
    if (i < 0)
        return UBUS_STATUS_NOT_FOUND;

    if (fake_deleted_count < FAKE_MAX_LEASES)
        fake_deleted[fake_deleted_count++] = fake_leases[i];
    fake_remove(i);
    return UBUS_STATUS_OK;
}

static int fake_get_leases(ubus_data_handler_t cb, void *priv)
{
    struct blob_attr *tables = calloc(fake_count ? fake_count : 1, sizeof(*tables));
    struct blob_attr *fields = calloc(fake_count ? fake_count * 4 : 1, sizeof(*fields));
    struct blob_attr leases = { .name = "leases", .type = BLOBMSG_TYPE_ARRAY };
    struct blob_attr reply = { .type = BLOBMSG_TYPE_TABLE };
    struct ubus_request req = { .priv = priv };
    int i;

    if (!tables || !fields)
    {
        free(tables);
        free(fields);
        return UBUS_STATUS_UNKNOWN_ERROR;
    }

    for (i = 0; i < fake_count; i++)
    {
        struct blob_attr *f = &fields[i * 4];

        f[0] = (struct blob_attr) { .name = "ip", .type = BLOBMSG_TYPE_STRING,
                                    .str = fake_leases[i].ip };
        f[1] = (struct blob_attr) { .name = "mac", .type = BLOBMSG_TYPE_STRING,
                                    .str = fake_leases[i].mac };
        f[2] = (struct blob_attr) { .name = "hostname", .type = BLOBMSG_TYPE_STRING,
                                    .str = fake_leases[i].hostname };
        f[3] = (struct blob_attr) { .name = "expires", .type = BLOBMSG_TYPE_INT32,
                                    .u32 = fake_leases[i].expires };
        tables[i] = (struct blob_attr) { .type = BLOBMSG_TYPE_TABLE,
                                         .children = f, .n_children = 4 };
    }
    leases.children = tables;
    leases.n_children = fake_count;
    reply.children = &leases;
    reply.n_children = 1;

    if (cb)
        cb(&req, 0, &reply);

    free(tables);
    free(fields);
    return UBUS_STATUS_OK;
}

int ubus_invoke(struct ubus_context *ctx, uint32_t obj, const char *method,
                struct blob_attr *msg, ubus_data_handler_t cb, void *priv,
                int timeout)
{
    (void)ctx;
    (void)obj;
    (void)timeout;

    if (fake_fail_status)
        return fake_fail_status;
    if (strcmp(method, "add_lease") == 0)
        return fake_add_lease(msg);
    if (strcmp(method, "delete_lease") == 0)
        return fake_delete_lease(msg);
    if (strcmp(method, "get_leases") == 0)
        return fake_get_leases(cb, priv);
    return UBUS_STATUS_METHOD_NOT_FOUND;
}

int ubus_lookup_id(struct ubus_context *ctx, const char *path, uint32_t *id)
{
    (void)ctx;
    (void)path;
    *id = 1;
    return UBUS_STATUS_OK;
}

const char *ubus_strerror(int error)
{
    switch (error)
    {
    case UBUS_STATUS_NOT_FOUND:
        return "Not found";
    case UBUS_STATUS_TIMEOUT:
        return "Request timed out (simulated)";
    default:
        return "Error";
    }
}

struct ubus_context *ubus_connect(const char *path)
{
    (void)path;
    return (struct ubus_context *)&fake_ctx;
}

void ubus_free(struct ubus_context *ctx)
{
    (void)ctx;
}

void ubus_add_uloop(struct ubus_context *ctx)
{
    (void)ctx;
}

int ubus_register_event_handler(struct ubus_context *ctx,
                                struct ubus_event_handler *ev,
                                const char *pattern)
{
    (void)ctx;
    (void)ev;
    (void)pattern;
    return 0;
}

int uloop_init(void) { return 0; }
int uloop_run(void) { return 0; }
void uloop_done(void) { }
int uloop_fd_add(struct uloop_fd *sock, unsigned int flags)
{
    (void)sock;
    (void)flags;
    return 0;
}
int uloop_timeout_set(struct uloop_timeout *timeout, int msecs)
{
    (void)timeout;
    (void)msecs;
    return 0;
}

/* Lease event, as dhcp-script-ha.sh publishes it on ubus */
static void fake_event(const char *action, const char *ip, const char *mac)
{
    static struct blob_buf b;

    blob_buf_init(&b, 0);
    blobmsg_add_string(&b, "action", action);
    blobmsg_add_string(&b, "ip", ip);
    if (mac)
        blobmsg_add_string(&b, "mac", mac);
    blobmsg_add_u32(&b, "expires", (uint32_t)(time(NULL) + 3600));
    blobmsg_add_u32(&b, "lease_length", 3600);
    handle_lease_event(NULL, NULL, "dhcp.lease", b.head);
}

/* "del" events dnsmasq emits, asynchronously, for the leases removed
 * through delete_lease */
static void fake_deliver_del_events(void)
{
    int i;

    for (i = 0; i < fake_deleted_count; i++)
        fake_event("del", fake_deleted[i].ip, fake_deleted[i].mac);
}

/* ============================================
 * Fake peer (loopback UDP, plain mode)
 * ============================================ */
static int peer_sock = -1;
static uint16_t peer_seq = 1;

struct peer_rx
{
    int deletes;
    int adds_updates;
};

static void peer_send(uint8_t action, const char *ip, const char *mac,
                      uint64_t ts, uint64_t expected_ts)
{
    struct sync_message msg;
    struct sockaddr_in to;

    memset(&msg, 0, sizeof(msg));
    msg.magic = htonl(SYNC_MAGIC);
    msg.version = LEASE_SYNC_PROTOCOL_VERSION;
    msg.action = action;
    msg.sequence = htons(peer_seq++);
    strncpy(msg.node_id, PEER_NODE_ID, sizeof(msg.node_id) - 1);
    msg.timestamp_ms = htobe64(ts);
    msg.af_family = SYNC_AF_INET4;
    inet_pton(AF_INET, ip, &msg.addr.addr4);
    if (mac)
        strncpy(msg.mac, mac, sizeof(msg.mac) - 1);
    strncpy(msg.hostname, "peer-host", sizeof(msg.hostname) - 1);
    msg.expires = htonl((uint32_t)(time(NULL) + 3600));
    msg.lease_length = htonl(3600);
    msg.expected_ts = htobe64(expected_ts);

    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(DAEMON_PORT);
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (sendto(peer_sock, &msg, sizeof(msg), 0, (struct sockaddr *)&to,
               sizeof(to)) != (ssize_t)sizeof(msg))
    {
        perror("peer_send: sendto");
        exit(1);
    }

    peer_sync_handle_message(g_state);
}

/* Collect everything the daemon sent to the peer since the last call */
static void peer_drain(struct peer_rx *rx)
{
    struct sync_message msg;
    struct peer_rx scratch;

    if (!rx)
        rx = &scratch;
    memset(rx, 0, sizeof(*rx));

    while (recv(peer_sock, &msg, sizeof(msg), MSG_DONTWAIT) == (ssize_t)sizeof(msg))
    {
        switch (msg.action)
        {
        case SYNC_ACTION_DELETE:
            rx->deletes++;
            break;
        case SYNC_ACTION_ADD:
        case SYNC_ACTION_UPDATE:
            rx->adds_updates++;
            break;
        default:
            break;
        }
    }
}

/* ============================================
 * Daemon state helpers
 * ============================================ */
static struct daemon_state state_storage;

static void reset_state(void)
{
    struct config saved = g_state->config;
    struct ubus_context *ctx = g_state->ubus_ctx;

    lease_db_cleanup();
    retry_queue_cleanup(&g_state->retry_queue);
    memset(g_state, 0, sizeof(*g_state));
    g_state->config = saved;
    g_state->ubus_ctx = ctx;
    lease_db_init();
    retry_queue_init(&g_state->retry_queue);
    g_state->startup_phase = STARTUP_PHASE_SYNC_REQUESTED;

    fake_reset();
    peer_drain(NULL);
}

static int setup(void)
{
    struct sockaddr_in addr;
    struct sockaddr_in *peer_addr;

    g_state = &state_storage;
    memset(g_state, 0, sizeof(*g_state));
    log_set_level(LOG_LEVEL_ERROR);

    strcpy(g_state->config.node_id, NODE_ID);
    strcpy(g_state->config.bind_address, "127.0.0.1");
    g_state->config.sync_port = DAEMON_PORT;
    g_state->config.sync_socket = -1;
    g_state->config.sync_interval = DEFAULT_SYNC_INTERVAL;
    g_state->config.peer_timeout = DEFAULT_PEER_TIMEOUT;
    g_state->config.security_mode = SECURITY_MODE_PLAIN;
    g_state->config.plain_mode_acknowledged = true;

    g_state->config.peer_count = 1;
    strcpy(g_state->config.peers[0].address, "127.0.0.1");
    peer_addr = (struct sockaddr_in *)&g_state->config.peers[0].addr;
    peer_addr->sin_family = AF_INET;
    peer_addr->sin_port = htons(PEER_PORT);
    peer_addr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    g_state->config.peers[0].addr_len = sizeof(struct sockaddr_in);

    g_state->ubus_ctx = ubus_connect(NULL);

    if (peer_sync_init(g_state) != 0)
    {
        fprintf(stderr, "peer_sync_init failed\n");
        return -1;
    }

    peer_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (peer_sock < 0)
        return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PEER_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(peer_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        fprintf(stderr, "bind peer socket: %s\n", strerror(errno));
        close(peer_sock);
        return -1;
    }

    lease_db_init();
    retry_queue_init(&g_state->retry_queue);
    return 0;
}

static void teardown(void)
{
    lease_db_cleanup();
    retry_queue_cleanup(&g_state->retry_queue);
    peer_sync_cleanup();
    if (peer_sock >= 0)
        close(peer_sock);
}

/* ============================================
 * Startup reconciliation
 * ============================================ */
static void test_startup_keeps_unreported_leases(void)
{
    printf("\n=== Test: startup without peer answer keeps dnsmasq leases ===\n");
    reset_state();

    fake_put("10.0.0.70", "02:00:00:00:00:70", 3600);
    fake_put("10.0.0.71", "02:00:00:00:00:71", 3600);

    startup_timer_cb(&startup_timer);

    ASSERT_EQ_INT("startup completes", g_state->startup_phase, STARTUP_PHASE_COMPLETE);
    ASSERT_EQ_INT("dnsmasq was not asked to delete", fake_delete_calls, 0);
    ASSERT_TRUE("first lease still in dnsmasq", fake_find("10.0.0.70") >= 0);
    ASSERT_TRUE("second lease still in dnsmasq", fake_find("10.0.0.71") >= 0);
}

static void test_startup_sends_no_delete(void)
{
    struct peer_rx rx;

    printf("\n=== Test: startup without peer answer sends no DELETE ===\n");
    reset_state();

    fake_put("10.0.0.72", "02:00:00:00:00:72", 3600);
    fake_put("10.0.0.73", "02:00:00:00:00:73", 3600);

    startup_timer_cb(&startup_timer);
    fake_deliver_del_events();
    peer_drain(&rx);

    ASSERT_EQ_INT("peer received no DELETE", rx.deletes, 0);
}

static void test_untracked_lease_announced_on_renewal(void)
{
    struct peer_rx rx;

    printf("\n=== Test: untracked lease is announced at its renewal ===\n");
    reset_state();

    fake_put("10.0.0.74", "02:00:00:00:00:74", 3600);
    startup_timer_cb(&startup_timer);
    peer_drain(NULL);

    fake_event("old", "10.0.0.74", "02:00:00:00:00:74");
    peer_drain(&rx);

    ASSERT_EQ_INT("renewal is announced to the peer", rx.adds_updates, 1);
    ASSERT_NOT_NULL("renewed lease is tracked", lease_db_find("10.0.0.74"));
}

static void test_startup_old_event_not_announced(void)
{
    struct peer_rx rx;

    printf("\n=== Test: startup 'old' event for an untracked lease ===\n");
    reset_state();

    fake_put("10.0.0.75", "02:00:00:00:00:75", 3600);
    fake_event("old", "10.0.0.75", "02:00:00:00:00:75");
    peer_drain(&rx);

    ASSERT_EQ_INT("lease is not announced", rx.adds_updates, 0);
    ASSERT_NULL("lease is not tracked", lease_db_find("10.0.0.75"));
}

static void test_startup_old_event_for_tracked_lease(void)
{
    struct peer_rx rx;
    struct lease_entry *e;

    printf("\n=== Test: startup 'old' event for a lease a peer reported ===\n");
    reset_state();

    peer_send(SYNC_ACTION_ADD, "10.0.0.76", "02:00:00:00:00:76",
              get_timestamp_ms() - 1000, 0);
    peer_drain(NULL);
    ASSERT_TRUE("peer lease injected into dnsmasq", fake_find("10.0.0.76") >= 0);

    fake_event("old", "10.0.0.76", "02:00:00:00:00:76");
    peer_drain(&rx);

    ASSERT_EQ_INT("lease is announced", rx.adds_updates, 1);
    e = lease_db_find("10.0.0.76");
    ASSERT_NOT_NULL("lease is tracked", e);
    if (e)
        ASSERT_EQ_STR("lease_db takes the local event", e->source_node, NODE_ID);
}

/* ============================================
 * Deleting a lease dnsmasq does not hold
 * ============================================ */
static void test_peer_delete_absent_lease_not_retried(void)
{
    printf("\n=== Test: peer DELETE for a lease absent from dnsmasq ===\n");
    reset_state();

    peer_send(SYNC_ACTION_DELETE, "10.0.0.60", "02:00:00:00:00:60",
              get_timestamp_ms(), 0);

    ASSERT_EQ_INT("dnsmasq was asked to delete it", fake_delete_calls, 1);
    ASSERT_EQ_INT("absent lease is not queued for retry", retry_queue_count(), 0);
}

static void test_retry_of_absent_delete_succeeds(void)
{
    struct lease_entry e;

    printf("\n=== Test: queued DELETE for a lease already gone ===\n");
    reset_state();

    memset(&e, 0, sizeof(e));
    parse_ip_address("10.0.0.62", &e.af_family, &e.addr);
    strcpy(e.ip_str, "10.0.0.62");
    e.timestamp_ms = get_timestamp_ms();
    retry_queue_add(&g_state->retry_queue, &e, RETRY_ACTION_DELETE);
    ASSERT_NOT_NULL("deletion is queued", g_state->retry_queue.head);
    if (!g_state->retry_queue.head)
        return;
    g_state->retry_queue.head->last_attempt -= RETRY_INTERVAL_SECONDS;

    retry_queue_process();

    ASSERT_EQ_INT("entry leaves the queue", retry_queue_count(), 0);
    ASSERT_EQ_INT("not counted as a drop", (int)g_state->total_injection_drops, 0);
}

static void test_delete_failures_still_retried(void)
{
    printf("\n=== Test: other delete failures are still retried ===\n");
    reset_state();

    fake_put("10.0.0.63", "02:00:00:00:00:63", 3600);
    fake_fail_status = UBUS_STATUS_TIMEOUT;
    peer_send(SYNC_ACTION_DELETE, "10.0.0.63", "02:00:00:00:00:63",
              get_timestamp_ms(), 0);

    ASSERT_EQ_INT("timed-out delete is queued for retry", retry_queue_count(), 1);

    fake_fail_status = 0;
    if (!g_state->retry_queue.head)
        return;
    g_state->retry_queue.head->last_attempt -= RETRY_INTERVAL_SECONDS;
    retry_queue_process();
    ASSERT_EQ_INT("retry deletes the lease", fake_find("10.0.0.63"), -1);
    ASSERT_EQ_INT("retry queue drained", retry_queue_count(), 0);
}

int main(void)
{
    printf("========================================\n");
    printf("lease-sync Sync Flow Tests\n");
    printf("========================================\n");

    if (setup() < 0)
    {
        printf(RED "FAIL" NC ": test setup\n");
        return 1;
    }

    test_startup_keeps_unreported_leases();
    test_startup_sends_no_delete();
    test_untracked_lease_announced_on_renewal();
    test_startup_old_event_not_announced();
    test_startup_old_event_for_tracked_lease();
    test_peer_delete_absent_lease_not_retried();
    test_retry_of_absent_delete_succeeds();
    test_delete_failures_still_retried();

    teardown();
    return print_test_summary();
}
