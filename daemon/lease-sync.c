/* Copyright (C) 2025-2026 Pierre Gaufillet <pierre.gaufillet@bergamote.eu>
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
 * Main daemon implementation
 *
 * This daemon synchronizes DHCP leases between multiple dnsmasq instances
 * in an active/active high availability configuration.
 *
 * Architecture:
 *   - Subscribes to local dnsmasq lease events via ubus
 *   - Maintains in-memory lease database
 *   - Broadcasts lease changes to peer nodes via UDP
 *   - Receives peer lease updates and injects into local dnsmasq
 *   - Handles conflict resolution using timestamps (LWW)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <libubox/uloop.h>
#include <libubus.h>

#include "common.h"
#include "crypto.h"

/* Global daemon state */
struct daemon_state *g_state = NULL;

/* uloop file descriptor for UDP sync socket */
static struct uloop_fd sync_uloop_fd;

/* uloop timers */
static struct uloop_timeout heartbeat_timer;
static struct uloop_timeout sync_request_timer;
static struct uloop_timeout peer_check_timer;
static struct uloop_timeout retry_timer;
static struct uloop_timeout stats_timer;
static struct uloop_timeout reconcile_timer;

static void print_usage(const char *prog)
{
  printf("lease-sync - DHCP Lease Synchronization Daemon v%s\n\n", LEASE_SYNC_VERSION);
  printf("Usage: %s [options]\n\n", prog);
  printf("Options:\n");
  printf("  -c, --config FILE      Configuration file (default: /etc/lease-sync/config)\n");
  printf("  -d, --debug            Enable debug logging\n");
  printf("  -v, --version          Print version and exit\n");
  printf("  -h, --help             Print this help message\n");
  printf("\n");
  printf("Signals:\n");
  printf("  SIGTERM, SIGINT        Graceful shutdown\n");
  printf("\n");
}

static int daemon_init(const char *config_file)
{
  /* Allocate global state */
  g_state = calloc(1, sizeof(struct daemon_state));
  if (!g_state)
    {
      fprintf(stderr, "Failed to allocate daemon state\n");
      return -1;
    }

  /* Set defaults */
  config_set_defaults(&g_state->config);

  /* Load configuration */
  if (config_load(&g_state->config, config_file) < 0)
    {
      log_warning("Failed to load config, using defaults");
    }

  /* Ensure directories exist */
  if (config_ensure_directories(&g_state->config) < 0)
    {
      log_error("Failed to create configuration directories");
      return -1;
    }

  /* Load or generate node ID */
  if (config_load_node_id(&g_state->config) < 0)
    {
      log_error("Failed to load node ID");
      return -1;
    }

  /* Initialize lease database */
  if (lease_db_init() < 0)
    {
      log_error("Failed to initialize lease database");
      return -1;
    }

  /* Initialize peer synchronization */
  if (peer_sync_init(g_state) < 0)
    {
      log_error("Failed to initialize peer sync");
      return -1;
    }

  /* Initialize ubus handler */
  if (ubus_handler_init(g_state) < 0)
    {
      log_error("Failed to initialize ubus handler");
      return -1;
    }

  /* Initialize retry queue */
  if (retry_queue_init(&g_state->retry_queue) < 0)
    {
      log_error("Failed to initialize retry queue");
      return -1;
    }

  g_state->start_time = time(NULL);
  g_state->message_sequence = 0;

  log_info("Daemon initialized successfully");
  log_info("  Node ID: %s", g_state->config.node_id);
  log_info("  Peers: %d", g_state->config.peer_count);
  log_info("  Sync port: %d", g_state->config.sync_port);

  /* Initialize security context */
  g_state->security_ctx = NULL;

  /* Verify security configuration */
  if (g_state->config.security_mode == SECURITY_MODE_ENCRYPTED)
    {
      /* PSK encryption mode */
      if (strlen(g_state->config.psk_key) == 0)
        {
          log_error("=====================================");
          log_error("SECURITY ERROR");
          log_error("=====================================");
          log_error("");
          log_error("Security mode is 'encrypted' but no psk_key is configured.");
          log_error("");
          log_error("To use PSK encryption, add to config:");
          log_error("  psk_key=<64-character hex key>");
          log_error("");
          log_error("Generate a key with:");
          log_error("  head -c 32 /dev/urandom | xxd -p -c 32");
          log_error("");
          log_error("Alternatively, for WireGuard/IPsec networks:");
          log_error("  security_mode=plain");
          log_error("  plain_mode_acknowledged=1");
          log_error("");
          log_error("See: SECURITY.md");
          log_error("=====================================");
          return -1;
        }

      /* Initialize encryption context */
      g_state->security_ctx = security_context_new(g_state->config.psk_key);
      if (!g_state->security_ctx)
        {
          log_error("Failed to initialize encryption context");
          log_error("Check that psk_key is a valid 64-character hex string");
          return -1;
        }

      log_info("  Security: Encrypted (AES-256-GCM PSK)");
    }
  else if (g_state->config.security_mode == SECURITY_MODE_PLAIN)
    {
      /* Require explicit acknowledgment */
      if (!g_state->config.plain_mode_acknowledged)
        {
          log_error("=====================================");
          log_error("SECURITY ERROR");
          log_error("=====================================");
          log_error("");
          log_error("Configuration specifies plain (unencrypted) mode,");
          log_error("but plain_mode_acknowledged is not set.");
          log_error("");
          log_error("Plain mode should ONLY be used when:");
          log_error("  - Traffic is protected by WireGuard/IPsec");
          log_error("  - Network is physically isolated");
          log_error("  - Other network-layer security is in place");
          log_error("");
          log_error("To proceed, add to /etc/lease-sync/config:");
          log_error("  plain_mode_acknowledged=1");
          log_error("");
          log_error("WARNING: This acknowledges unencrypted traffic!");
          log_error("See: SECURITY.md for VPN setup");
          log_error("=====================================");
          return -1;
        }

      /* Log prominent warning */
      log_warning("==============================================");
      log_warning("RUNNING IN PLAIN (UNENCRYPTED) MODE");
      log_warning("==============================================");
      log_warning("Ensure network-layer security is configured!");
      log_warning("- WireGuard tunnels between all peers");
      log_warning("- OR IPsec with ESP encryption");
      log_warning("- OR physical network isolation");
      log_warning("==============================================");
      log_info("  Security: Plain (network-layer delegated)");
    }

  return 0;
}

static void daemon_cleanup(void)
{
  if (!g_state)
    return;

  log_info("Cleaning up daemon...");

  /* Cleanup modules */
  retry_queue_cleanup(&g_state->retry_queue);
  ubus_handler_cleanup();
  peer_sync_cleanup();
  lease_db_cleanup();

  /* Free security context */
  if (g_state->security_ctx)
    {
      security_context_free(g_state->security_ctx);
      g_state->security_ctx = NULL;
    }

  /* Free state */
  free(g_state);
  g_state = NULL;

  log_cleanup();
}

/*
 * Startup Reconciliation
 *
 * After receiving sync responses from peers, compare dnsmasq's
 * current leases with our synchronized lease_db. Any lease in
 * dnsmasq that is NOT in lease_db is stale and must be deleted.
 *
 * This handles the scenario where a node was down while leases
 * were deleted from the cluster.
 *
 * IMPORTANT: We cannot call ubus_invoke (delete_lease) during the
 * get_leases callback because nested ubus_invoke corrupts the blob
 * iteration. Instead, we collect stale IPs during iteration and
 * delete them afterwards.
 */

/* Maximum number of stale leases to track during reconciliation */
#define MAX_STALE_LEASES 256

struct reconcile_ctx
{
  char stale_ips[MAX_STALE_LEASES][64];
  char stale_macs[MAX_STALE_LEASES][32];
  int count;
  int total;
};

static void reconcile_callback(const char *ip, const char *mac,
                               const char *hostname, time_t expires,
                               void *user_data)
{
  struct reconcile_ctx *ctx = (struct reconcile_ctx *)user_data;
  struct lease_entry *existing;

  (void)hostname;
  (void)expires;

  ctx->total++;

  /* Check if this dnsmasq lease exists in our synchronized database */
  existing = lease_db_find(ip);

  if (existing == NULL)
    {
      /* Lease in dnsmasq but NOT in cluster = STALE
       * Store for deletion after iteration completes */
      if (ctx->count < MAX_STALE_LEASES)
        {
          strncpy(ctx->stale_ips[ctx->count], ip, 63);
          ctx->stale_ips[ctx->count][63] = '\0';
          strncpy(ctx->stale_macs[ctx->count], mac ? mac : "", 31);
          ctx->stale_macs[ctx->count][31] = '\0';
          ctx->count++;
        }
      else
        {
          log_warning("Too many stale leases, skipping %s", ip);
        }
    }
}

static int startup_reconcile_leases(void)
{
  struct reconcile_ctx ctx = { .count = 0, .total = 0 };
  int dnsmasq_count;
  int deleted = 0;
  int i;

  log_info("Starting lease reconciliation (startup reconciliation)");

  dnsmasq_count = ubus_handler_get_leases(reconcile_callback, &ctx);

  if (dnsmasq_count < 0)
    {
      log_error("Failed to query dnsmasq leases for reconciliation");
      /* Mark reconciliation complete anyway - we'll try again on next restart */
      g_state->startup_phase = STARTUP_PHASE_COMPLETE;
      return -1;
    }

  /* Now delete all stale leases (after iteration is complete)
   * IMPORTANT: Set reconciliation_in_progress flag to prevent broadcasting
   * these deletes to peers. startup reconciliation deletes are local reconciliation, not
   * cluster-wide actions. */
  g_state->reconciliation_in_progress = true;

  for (i = 0; i < ctx.count; i++)
    {
      log_info("Reconciliation: deleting stale lease %s (mac=%s)",
               ctx.stale_ips[i], ctx.stale_macs[i]);

      if (ubus_handler_delete_lease(ctx.stale_ips[i]) == 0)
        {
          deleted++;
          g_state->total_leases_reconciled++;
        }
      else
        {
          log_warning("Failed to delete stale lease %s", ctx.stale_ips[i]);
        }
    }

  g_state->reconciliation_in_progress = false;

  log_info("Reconciliation complete: %d leases in dnsmasq, %d stale leases deleted",
           ctx.total, deleted);

  g_state->startup_phase = STARTUP_PHASE_COMPLETE;

  return deleted;
}

/* uloop callback: UDP sync socket is readable */
static void sync_socket_cb(struct uloop_fd *fd, unsigned int events)
{
  (void)events;

  if (fd->fd >= 0)
    peer_sync_handle_message(g_state);
}

/* uloop timer callbacks */
static void heartbeat_cb(struct uloop_timeout *t)
{
  peer_sync_send_heartbeat();
  uloop_timeout_set(t, 30 * 1000);
}

static void sync_request_cb(struct uloop_timeout *t)
{
  if (g_state->config.sync_interval > 0)
    {
      peer_sync_request_full_sync();
      uloop_timeout_set(t, g_state->config.sync_interval * 1000);
    }
}

static void peer_check_cb(struct uloop_timeout *t)
{
  peer_sync_update_status();
  uloop_timeout_set(t, 10 * 1000);
}

static void retry_cb(struct uloop_timeout *t)
{
  if (retry_queue_count() > 0)
    retry_queue_process();
  uloop_timeout_set(t, RETRY_INTERVAL_SECONDS * 1000);
}

static void stats_cb(struct uloop_timeout *t)
{
  log_info("Statistics: leases=%d (local=%d, peer=%d), "
           "added=%llu, updated=%llu, deleted=%llu, "
           "sent=%llu, recv=%llu, conflicts=%llu, "
           "retry_queue=%d, retries=%llu, drops=%llu, reconciled=%llu",
           g_state->lease_count,
           g_state->local_lease_count,
           g_state->peer_lease_count,
           (unsigned long long)g_state->total_leases_added,
           (unsigned long long)g_state->total_leases_updated,
           (unsigned long long)g_state->total_leases_deleted,
           (unsigned long long)g_state->total_sync_messages_sent,
           (unsigned long long)g_state->total_sync_messages_received,
           (unsigned long long)g_state->total_conflicts_resolved,
           retry_queue_count(),
           (unsigned long long)g_state->total_injection_retries,
           (unsigned long long)g_state->total_injection_drops,
           (unsigned long long)g_state->total_leases_reconciled);
  uloop_timeout_set(t, 300 * 1000);
}

/* One-shot: startup reconciliation after sync wait */
static void reconcile_cb(struct uloop_timeout *t)
{
  (void)t;

  if (g_state->startup_phase == STARTUP_PHASE_SYNC_REQUESTED)
    {
      g_state->startup_phase = STARTUP_PHASE_RECONCILING;
      startup_reconcile_leases();
    }
}

static void main_loop(void)
{
  log_info("Entering main event loop");

  /* Request full sync from peers on startup */
  peer_sync_request_full_sync();

  /* Initialize startup reconciliation phase tracking */
  g_state->startup_phase = STARTUP_PHASE_SYNC_REQUESTED;
  g_state->startup_sync_request_time = time(NULL);

  /* Register UDP sync socket with uloop */
  if (g_state->config.sync_socket >= 0)
    {
      sync_uloop_fd.fd = g_state->config.sync_socket;
      sync_uloop_fd.cb = sync_socket_cb;
      uloop_fd_add(&sync_uloop_fd, ULOOP_READ);
    }

  /* Register ubus with uloop */
  if (g_state->ubus_ctx)
    ubus_add_uloop(g_state->ubus_ctx);

  /* Arm periodic timers */
  heartbeat_timer.cb = heartbeat_cb;
  uloop_timeout_set(&heartbeat_timer, 30 * 1000);

  if (g_state->config.sync_interval > 0)
    {
      sync_request_timer.cb = sync_request_cb;
      uloop_timeout_set(&sync_request_timer, g_state->config.sync_interval * 1000);
    }

  peer_check_timer.cb = peer_check_cb;
  uloop_timeout_set(&peer_check_timer, 10 * 1000);

  retry_timer.cb = retry_cb;
  uloop_timeout_set(&retry_timer, RETRY_INTERVAL_SECONDS * 1000);

  stats_timer.cb = stats_cb;
  uloop_timeout_set(&stats_timer, 300 * 1000);

  /* One-shot: startup reconciliation after STARTUP_SYNC_WAIT_SECONDS */
  reconcile_timer.cb = reconcile_cb;
  uloop_timeout_set(&reconcile_timer, STARTUP_SYNC_WAIT_SECONDS * 1000);

  /* Run event loop (handles SIGTERM/SIGINT) */
  uloop_run();

  log_info("Exiting main event loop");
}

int main(int argc, char *argv[])
{
  const char *config_file = "/etc/lease-sync/config";
  bool debug = false;
  int opt;

  /* Parse command line arguments.
   * Daemonization (fork/setsid/stdio redirect) is the supervisor's job
   * (procd/systemd); lease-sync always runs in the foreground of its
   * supervising process and logs to syslog. */
  static struct option long_options[] =
  {
    {"config",     required_argument, 0, 'c'},
    {"debug",      no_argument,       0, 'd'},
    {"version",    no_argument,       0, 'v'},
    {"help",       no_argument,       0, 'h'},
    {0, 0, 0, 0}
  };

  while ((opt = getopt_long(argc, argv, "c:dvh", long_options, NULL)) != -1)
    {
      switch (opt)
        {
        case 'c':
          config_file = optarg;
          break;
        case 'd':
          debug = true;
          break;
        case 'v':
          printf("lease-sync version %s\n", LEASE_SYNC_VERSION);
          printf("Protocol version: %d\n", LEASE_SYNC_PROTOCOL_VERSION);
          return 0;
        case 'h':
          print_usage(argv[0]);
          return 0;
        default:
          print_usage(argv[0]);
          return 1;
        }
    }

  /* Initialize uloop */
  uloop_init();

  /* Initialize daemon (loads config - log calls go to stderr before log_init) */
  if (daemon_init(config_file) < 0)
    {
      log_error("Daemon initialization failed");
      daemon_cleanup();
      uloop_done();
      return 1;
    }

  /* Apply debug flag from command line */
  if (debug)
    {
      g_state->config.log_level = LOG_LEVEL_DEBUG;
    }

  /* Initialize logging module with final config */
  log_init("lease-sync", LOG_MODE_DAEMON, g_state->config.log_level);

  log_info("Starting lease-sync daemon v%s", LEASE_SYNC_VERSION);

  /* Run main loop (uloop handles signals and event dispatch) */
  main_loop();

  /* Cleanup */
  daemon_cleanup();
  uloop_done();

  log_info("lease-sync daemon terminated");

  return 0;
}
