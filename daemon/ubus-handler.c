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
 * ubus-handler.c - ubus event handling and dnsmasq integration
 *
 * Handles:
 *   - Subscribing to dhcp.lease.* events from dhcp-script-ha.sh
 *   - Injecting leases into local dnsmasq via ubus
 *   - Processing local lease events and broadcasting to peers
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <libubox/blobmsg_json.h>
#include <libubus.h>

#include "common.h"
#include "retry-queue.h"

extern struct daemon_state *g_state;

static void handle_lease_event(struct ubus_context *ctx, struct ubus_event_handler *ev,
                               const char *type, struct blob_attr *msg)
{
  struct blob_attr *tb[20];
  static const struct blobmsg_policy event_policy[] =
  {
    [0] = { .name = "action", .type = BLOBMSG_TYPE_STRING },
    [1] = { .name = "ip", .type = BLOBMSG_TYPE_STRING },
    [2] = { .name = "mac", .type = BLOBMSG_TYPE_STRING },
    [3] = { .name = "hostname", .type = BLOBMSG_TYPE_STRING },
    [4] = { .name = "client_id", .type = BLOBMSG_TYPE_STRING },
    [5] = { .name = "expires", .type = BLOBMSG_TYPE_INT32 },
    [6] = { .name = "lease_length", .type = BLOBMSG_TYPE_INT32 },
    [7] = { .name = "interface", .type = BLOBMSG_TYPE_STRING },
    [8] = { .name = "iaid", .type = BLOBMSG_TYPE_INT32 },
    [9] = { .name = "is_temporary", .type = BLOBMSG_TYPE_INT32 },
  };
  const char *action;
  const char *ip;

  (void)ctx;
  (void)ev;

  if (!g_state || !msg)
    return;

  /* Parse event message */
  blobmsg_parse(event_policy, 10, tb, blob_data(msg), blob_len(msg));

  /* Extract action */
  action = tb[0] ? blobmsg_get_string(tb[0]) : NULL;
  ip = tb[1] ? blobmsg_get_string(tb[1]) : NULL;

  if (!action || !ip)
    {
      log_warning("Received incomplete lease event");
      return;
    }

  log_debug("Received ubus event: %s - %s %s", type, action, ip);

  /* startup reconciliation FIX: Skip "old" events during startup reconciliation phase.
   *
   * Problem: When dnsmasq starts, it reads leases from dhcp.leases and emits
   * "old" events for each one. If we add these to lease_db, startup reconciliation can't
   * distinguish between peer-synced leases (valid) and local stale leases.
   *
   * Solution: During startup (before startup reconciliation completes), only process "old" events
   * that are already in our database (from peer SYNC_RESPONSE). This ensures
   * startup reconciliation correctly identifies stale leases as "in dnsmasq but not in cluster".
   *
   * After startup completes, "old" events are processed normally for renewals.
   */
  if (strcmp(action, "old") == 0 &&
      g_state->startup_phase < STARTUP_PHASE_COMPLETE)
    {
      struct lease_entry *existing = lease_db_find(ip);
      if (existing == NULL)
        {
          log_debug("Skipping startup 'old' event for %s (not in peer-synced db)", ip);
          return;
        }
      /* Lease exists in db (from peer sync) - continue to update it */
      log_debug("Processing startup 'old' event for %s (exists in peer-synced db)", ip);
    }

  /* Handle DELETE action - only "del" means removal */
  if (strcmp(action, "del") == 0)
    {
      /* causality tracking: Look up lease timestamp BEFORE deleting for causality tracking.
       * This allows peers to detect if their local lease was renewed after
       * the original lease was created. */
      uint64_t expected_ts = 0;
      struct lease_entry *existing = lease_db_find(ip);

      if (existing)
        {
          expected_ts = existing->timestamp_ms;
          log_debug("DELETE for %s: expected_ts=%llu from local lease",
                    ip, (unsigned long long)expected_ts);
        }

      /* Delete from local database */
      lease_db_delete(ip);

      /* Broadcast DELETE to peers with expected_ts (causality tracking)
       * SKIP broadcast during startup reconciliation reconciliation - those deletes are local
       * cleanup, not cluster-wide actions. */
      if (!g_state->reconciliation_in_progress)
        {
          struct lease_entry entry;

          memset(&entry, 0, sizeof(entry));

          /* Parse IP address to set af_family and binary address */
          if (parse_ip_address(ip, &entry.af_family, &entry.addr) < 0)
            {
              log_error("Invalid IP address in delete event: %s", ip);
              return;
            }
          strncpy(entry.ip_str, ip, sizeof(entry.ip_str) - 1);
          strncpy(entry.source_node, g_state->config.node_id, MAX_NODE_ID_LEN - 1);
          entry.timestamp_ms = get_timestamp_ms();

          peer_sync_broadcast_delete(&entry, expected_ts);
        }
      else
        {
          log_debug("Skipping DELETE broadcast for %s (startup reconciliation reconciliation)", ip);
        }

      log_info("Deleted local lease: %s (expected_ts=%llu)",
               ip, (unsigned long long)expected_ts);
      return;
    }

  /* Handle ADD/UPDATE action - "old" is dnsmasq's term for renewal/refresh */
  if (strcmp(action, "add") == 0 || strcmp(action, "update") == 0 || strcmp(action, "old") == 0)
    {
      struct lease_entry entry;
      uint8_t sync_action;

      memset(&entry, 0, sizeof(entry));

      /* Parse IP address */
      if (parse_ip_address(ip, &entry.af_family, &entry.addr) < 0)
        {
          log_error("Invalid IP address in event: %s", ip);
          return;
        }
      strncpy(entry.ip_str, ip, sizeof(entry.ip_str) - 1);

      /* MAC address */
      if (tb[2])
        strncpy(entry.mac, blobmsg_get_string(tb[2]), sizeof(entry.mac) - 1);

      /* Hostname */
      if (tb[3])
        strncpy(entry.hostname, blobmsg_get_string(tb[3]), sizeof(entry.hostname) - 1);

      /* Client ID (hex string from dhcp-script) */
      if (tb[4])
        {
          const char *clid_str = blobmsg_get_string(tb[4]);
          int clid_len = 0;
          unsigned char clid_buf[MAX_CLIENT_ID_LEN];

          /* Parse hex string (format: "01:23:45:67:...") */
          parse_mac(clid_str, clid_buf, &clid_len);
          if (clid_len > 0)
            {
              memcpy(entry.client_id, clid_buf, clid_len);
              entry.client_id_len = clid_len;
            }
        }

      /* Lease timing */
      if (tb[5])
        entry.expires = (time_t)blobmsg_get_u32(tb[5]);
      if (tb[6])
        entry.lease_length = blobmsg_get_u32(tb[6]);

      /* Interface */
      if (tb[7])
        strncpy(entry.interface, blobmsg_get_string(tb[7]), sizeof(entry.interface) - 1);

      /* IPv6 specific */
      if (tb[8])
        entry.iaid = blobmsg_get_u32(tb[8]);
      if (tb[9])
        entry.is_temporary = (blobmsg_get_u32(tb[9]) != 0);

      /* Metadata */
      strncpy(entry.source_node, g_state->config.node_id, MAX_NODE_ID_LEN - 1);
      entry.timestamp_ms = get_timestamp_ms();

      /* Add/update in local database */
      if (strcmp(action, "add") == 0)
        lease_db_add(&entry);
      else
        lease_db_update(&entry);

      /* Broadcast to peers */
      sync_action = (strcmp(action, "add") == 0) ? SYNC_ACTION_ADD : SYNC_ACTION_UPDATE;
      peer_sync_broadcast_lease(&entry, sync_action);

      log_info("Processed local lease: %s (%s) - %s",
               entry.ip_str, entry.hostname, action);
    }
}

int ubus_handler_init(struct daemon_state *state)
{
  int ret;
  static struct ubus_event_handler lease_event_handler =
  {
    .cb = handle_lease_event,
  };

  if (!state)
    return -1;

  /* Connect to ubus */
  state->ubus_ctx = ubus_connect(NULL);
  if (!state->ubus_ctx)
    {
      log_error("Failed to connect to ubus");
      return -1;
    }

  /* Register event handler for dhcp.lease.* */
  ret = ubus_register_event_handler(state->ubus_ctx, &lease_event_handler, "dhcp.lease");
  if (ret)
    {
      log_error("Failed to register ubus event handler: %s", ubus_strerror(ret));
      ubus_free(state->ubus_ctx);
      state->ubus_ctx = NULL;
      return -1;
    }

  log_info("ubus handler initialized (subscribed to dhcp.lease)");

  return 0;
}

void ubus_handler_cleanup(void)
{
  if (g_state && g_state->ubus_ctx)
    {
      ubus_free(g_state->ubus_ctx);
      g_state->ubus_ctx = NULL;
    }

  log_info("ubus handler cleaned up");
}

int ubus_handler_inject_lease(struct lease_entry *entry)
{
  uint32_t id;
  int ret;
  struct blob_buf b = { 0 };
  time_t now;
  uint32_t duration;

  if (!g_state || !g_state->ubus_ctx || !entry)
    return -1;

  /* Lookup dnsmasq ubus object */
  ret = ubus_lookup_id(g_state->ubus_ctx, "dnsmasq", &id);
  if (ret)
    {
      log_error("Failed to find dnsmasq ubus object: %s", ubus_strerror(ret));
      return -1;
    }

  /* Build ubus message */
  blob_buf_init(&b, 0);

  blobmsg_add_string(&b, "ip", entry->ip_str);
  blobmsg_add_string(&b, "mac", entry->mac);
  blobmsg_add_string(&b, "hostname", entry->hostname);
  /* Convert absolute timestamp to duration (dnsmasq add_lease expects seconds remaining) */
  now = time(NULL);
  duration = (entry->expires > now) ? (uint32_t)(entry->expires - now) : 0;
  blobmsg_add_u32(&b, "expires", duration);

  if (entry->lease_length > 0)
    blobmsg_add_u32(&b, "lease_length", entry->lease_length);

  if (entry->interface[0])
    blobmsg_add_string(&b, "interface", entry->interface);

  /* IPv6 specific fields */
  if (entry->af_family == AF_INET6)
    {
      if (entry->iaid > 0)
        blobmsg_add_u32(&b, "iaid", entry->iaid);
      if (entry->is_temporary)
        blobmsg_add_u8(&b, "is_temporary", 1);
    }

  /* Client ID (convert to hex string) */
  if (entry->client_id_len > 0)
    {
      char clid_str[MAX_CLIENT_ID_LEN * 3];
      char mac_buf[64];

      format_mac(entry->client_id, entry->client_id_len, mac_buf);
      strncpy(clid_str, mac_buf, sizeof(clid_str) - 1);
      blobmsg_add_string(&b, "client_id", clid_str);
    }

  /* Call dnsmasq.add_lease */
  ret = ubus_invoke(g_state->ubus_ctx, id, "add_lease", b.head, NULL, NULL, 3000);
  blob_buf_free(&b);

  if (ret)
    {
      log_error("Failed to inject lease %s: %s", entry->ip_str, ubus_strerror(ret));
      return -1;
    }

  log_debug("Injected lease %s (%s) into dnsmasq",
            entry->ip_str, entry->hostname);

  return 0;
}

int ubus_handler_delete_lease(const char *ip)
{
  uint32_t id;
  int ret;
  struct blob_buf b = { 0 };

  if (!g_state || !g_state->ubus_ctx || !ip)
    return -1;

  /* Lookup dnsmasq ubus object */
  ret = ubus_lookup_id(g_state->ubus_ctx, "dnsmasq", &id);
  if (ret)
    {
      log_error("Failed to find dnsmasq ubus object: %s", ubus_strerror(ret));
      return -1;
    }

  /* Build ubus message */
  blob_buf_init(&b, 0);
  blobmsg_add_string(&b, "ip", ip);

  /* Call dnsmasq.delete_lease */
  ret = ubus_invoke(g_state->ubus_ctx, id, "delete_lease", b.head, NULL, NULL, 3000);
  blob_buf_free(&b);

  if (ret)
    {
      log_error("Failed to delete lease %s: %s", ip, ubus_strerror(ret));
      return -1;
    }

  log_debug("Deleted lease %s from dnsmasq", ip);

  return 0;
}

/* Callback context for get_leases response */
struct get_leases_ctx
{
  lease_enum_callback_t callback;
  void *user_data;
  int count;
};

/* Response handler for get_leases ubus call */
static void get_leases_response_cb(struct ubus_request *req, int type, struct blob_attr *msg)
{
  struct get_leases_ctx *ctx = (struct get_leases_ctx *)req->priv;
  enum
  {
    LEASES_ARRAY,
    __LEASES_MAX
  };
  static const struct blobmsg_policy leases_policy[] =
  {
    [LEASES_ARRAY] = { .name = "leases", .type = BLOBMSG_TYPE_ARRAY },
  };
  struct blob_attr *tb[__LEASES_MAX];
  struct blob_attr *cur;
  int rem;

  (void)type;

  if (!msg || !ctx || !ctx->callback)
    return;

  /* Parse response */
  blobmsg_parse(leases_policy, __LEASES_MAX, tb, blob_data(msg), blob_len(msg));

  if (!tb[LEASES_ARRAY])
    {
      log_warning("get_leases response: missing 'leases' array");
      return;
    }

  /* Iterate over each lease entry */
  blobmsg_for_each_attr(cur, tb[LEASES_ARRAY], rem)
  {
    enum
    {
      LEASE_IP,
      LEASE_MAC,
      LEASE_HOSTNAME,
      LEASE_EXPIRES,
      LEASE_CLIENT_ID,
      __LEASE_MAX
    };
    static const struct blobmsg_policy lease_policy[] =
    {
      [LEASE_IP] = { .name = "ip", .type = BLOBMSG_TYPE_STRING },
      [LEASE_MAC] = { .name = "mac", .type = BLOBMSG_TYPE_STRING },
      [LEASE_HOSTNAME] = { .name = "hostname", .type = BLOBMSG_TYPE_STRING },
      [LEASE_EXPIRES] = { .name = "expires", .type = BLOBMSG_TYPE_INT32 },
      [LEASE_CLIENT_ID] = { .name = "client_id", .type = BLOBMSG_TYPE_STRING },
    };
    struct blob_attr *lease_tb[__LEASE_MAX];
    const char *ip;
    const char *mac;
    const char *hostname;
    time_t expires;

    if (blobmsg_type(cur) != BLOBMSG_TYPE_TABLE)
      continue;

    /* Parse lease entry */
    blobmsg_parse(lease_policy, __LEASE_MAX, lease_tb,
                  blobmsg_data(cur), blobmsg_len(cur));

    /* Required fields */
    ip = lease_tb[LEASE_IP] ? blobmsg_get_string(lease_tb[LEASE_IP]) : NULL;
    if (!ip)
      continue;

    /* Optional fields */
    mac = lease_tb[LEASE_MAC] ? blobmsg_get_string(lease_tb[LEASE_MAC]) : "";
    hostname = lease_tb[LEASE_HOSTNAME] ? blobmsg_get_string(lease_tb[LEASE_HOSTNAME]) : "";
    /* get_leases returns duration (seconds remaining); convert to absolute for internal use.
       0xffffffff means infinite (stored as expires=0 internally, matching dnsmasq). */
    {
      uint32_t dur = lease_tb[LEASE_EXPIRES] ? blobmsg_get_u32(lease_tb[LEASE_EXPIRES]) : 0;
      if (dur == 0 || dur == 0xffffffff)
        expires = 0;
      else
        expires = time(NULL) + (time_t)dur;
    }

    /* Call user callback */
    ctx->callback(ip, mac, hostname, expires, ctx->user_data);
    ctx->count++;
  }
}

int ubus_handler_get_leases(lease_enum_callback_t callback, void *user_data)
{
  uint32_t id;
  int ret;
  struct get_leases_ctx ctx;

  if (!g_state || !g_state->ubus_ctx)
    return -1;

  if (!callback)
    {
      log_error("ubus_handler_get_leases: callback is NULL");
      return -1;
    }

  /* Lookup dnsmasq ubus object */
  ret = ubus_lookup_id(g_state->ubus_ctx, "dnsmasq", &id);
  if (ret)
    {
      log_error("Failed to find dnsmasq ubus object: %s", ubus_strerror(ret));
      return -1;
    }

  /* Prepare callback context */
  ctx.callback = callback;
  ctx.user_data = user_data;
  ctx.count = 0;

  /* Call dnsmasq.get_leases (no parameters needed) */
  ret = ubus_invoke(g_state->ubus_ctx, id, "get_leases", NULL,
                    get_leases_response_cb, &ctx, 5000);

  if (ret)
    {
      log_error("Failed to query dnsmasq leases: %s", ubus_strerror(ret));
      return -1;
    }

  log_debug("Retrieved %d leases from dnsmasq", ctx.count);

  return ctx.count;
}

/* ubus event processing is handled by ubus_add_uloop() in the main loop */
