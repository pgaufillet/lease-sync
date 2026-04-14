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
 * peer-sync.c - UDP peer synchronization
 *
 * Handles sending and receiving sync messages to/from peer nodes
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <endian.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"
#include "crypto.h"

extern struct daemon_state *g_state;

static void lease_to_sync_message(struct lease_entry *entry, struct sync_message *msg, uint8_t action)
{
  memset(msg, 0, sizeof(struct sync_message));

  /* Header */
  msg->magic = htonl(SYNC_MAGIC);
  msg->version = LEASE_SYNC_PROTOCOL_VERSION;
  msg->action = action;
  msg->sequence = htons(g_state->message_sequence++);

  /* Originator */
  strncpy(msg->node_id, g_state->config.node_id, MAX_NODE_ID_LEN - 1);
  msg->timestamp_ms = htobe64(entry->timestamp_ms);

  /* Lease data — convert platform AF to wire-format constants */
  msg->af_family = (entry->af_family == AF_INET) ? SYNC_AF_INET4 : SYNC_AF_INET6;

  if (entry->af_family == AF_INET)
    {
      memcpy(&msg->addr.addr4, &entry->addr.addr4, sizeof(struct in_addr));
    }
  else
    {
      memcpy(&msg->addr.addr6, &entry->addr.addr6, sizeof(struct in6_addr));
    }

  strncpy(msg->mac, entry->mac, sizeof(msg->mac) - 1);
  strncpy(msg->hostname, entry->hostname, sizeof(msg->hostname) - 1);

  if (entry->client_id_len > 0)
    {
      memcpy(msg->client_id, entry->client_id, min(entry->client_id_len, MAX_CLIENT_ID_LEN));
      msg->client_id_len = htons(entry->client_id_len);
    }

  msg->expires = htonl((uint32_t)entry->expires);
  msg->lease_length = htonl(entry->lease_length);

  /* IPv6 specific */
  msg->iaid = htonl(entry->iaid);
  msg->is_temporary = entry->is_temporary ? 1 : 0;

  strncpy(msg->interface, entry->interface, sizeof(msg->interface) - 1);

  /* Initialize expected_ts to 0 (not used for ADD/UPDATE) */
  msg->expected_ts = 0;
}

static void sync_message_to_lease(struct sync_message *msg, struct lease_entry *entry)
{
  uint16_t clid_len;

  memset(entry, 0, sizeof(struct lease_entry));

  /* IP address — convert wire-format constants to platform AF */
  entry->af_family = (msg->af_family == SYNC_AF_INET4) ? AF_INET : AF_INET6;

  if (entry->af_family == AF_INET)
    {
      memcpy(&entry->addr.addr4, &msg->addr.addr4, sizeof(struct in_addr));
      inet_ntop(AF_INET, &entry->addr.addr4, entry->ip_str, INET6_ADDRSTRLEN);
    }
  else
    {
      memcpy(&entry->addr.addr6, &msg->addr.addr6, sizeof(struct in6_addr));
      inet_ntop(AF_INET6, &entry->addr.addr6, entry->ip_str, INET6_ADDRSTRLEN);
    }

  /* Basic data */
  strncpy(entry->mac, msg->mac, sizeof(entry->mac) - 1);
  strncpy(entry->hostname, msg->hostname, sizeof(entry->hostname) - 1);
  strncpy(entry->interface, msg->interface, sizeof(entry->interface) - 1);

  /* Client ID */
  clid_len = ntohs(msg->client_id_len);
  if (clid_len > 0 && clid_len <= MAX_CLIENT_ID_LEN)
    {
      memcpy(entry->client_id, msg->client_id, clid_len);
      entry->client_id_len = clid_len;
    }

  /* Timing */
  entry->expires = ntohl(msg->expires);
  entry->lease_length = ntohl(msg->lease_length);

  /* IPv6 */
  entry->iaid = ntohl(msg->iaid);
  entry->is_temporary = msg->is_temporary ? true : false;

  /* Metadata */
  strncpy(entry->source_node, msg->node_id, MAX_NODE_ID_LEN - 1);
  entry->timestamp_ms = be64toh(msg->timestamp_ms);
  entry->sequence = ntohs(msg->sequence);
}

int peer_sync_init(struct daemon_state *state)
{
  int sock;
  int use_ipv6 = 1;
  int reuse = 1;

  /* Try IPv6 dual-stack socket first (supports both IPv4 and IPv6 peers).
   * Fall back to IPv4-only if IPv6 is not available on this system.
   */
  sock = socket(AF_INET6, SOCK_DGRAM, 0);
  if (sock < 0)
    {
      if (errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT)
        {
          /* IPv6 not supported, fall back to IPv4 */
          log_info("IPv6 not available, using IPv4-only mode");
          use_ipv6 = 0;
          sock = socket(AF_INET, SOCK_DGRAM, 0);
          if (sock < 0)
            {
              log_error("peer_sync_init: socket() failed: %s", strerror(errno));
              return -1;
            }
        }
      else
        {
          log_error("peer_sync_init: socket() failed: %s", strerror(errno));
          return -1;
        }
    }

  if (use_ipv6)
    {
      /* Enable dual-stack mode: allow IPv4 connections via IPv4-mapped addresses.
       * IPV6_V6ONLY=0 means the socket can receive/send both IPv4 and IPv6.
       */
      int v6only = 0;
      if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) < 0)
        {
          log_warning("peer_sync_init: setsockopt(IPV6_V6ONLY) failed: %s", strerror(errno));
          /* Continue anyway - dual-stack might still work on some systems */
        }
    }

  /* Set socket options */
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
      log_warning("peer_sync_init: setsockopt(SO_REUSEADDR) failed: %s", strerror(errno));
    }

  /* Bind to sync port - use explicit bind_address if configured */
  if (state->config.bind_address[0] != '\0')
    {
      /* Explicit bind address configured - parse and bind to it */
      struct sockaddr_storage bind_addr;
      socklen_t bind_len;
      struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&bind_addr;

      /* Try IPv6 first */
      if (inet_pton(AF_INET6, state->config.bind_address, &addr6->sin6_addr) == 1)
        {
          addr6->sin6_family = AF_INET6;
          addr6->sin6_port = htons(state->config.sync_port);
          bind_len = sizeof(struct sockaddr_in6);

          /* Need IPv6 socket for IPv6 bind address */
          if (!use_ipv6)
            {
              int v6only = 0;
              int reuse2 = 1;

              close(sock);
              sock = socket(AF_INET6, SOCK_DGRAM, 0);
              if (sock < 0)
                {
                  log_error("peer_sync_init: socket(AF_INET6) failed: %s", strerror(errno));
                  return -1;
                }
              use_ipv6 = 1;
              setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
              setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse2, sizeof(reuse2));
            }
        }
      /* Try IPv4 */
      else
        {
          struct sockaddr_in *addr4 = (struct sockaddr_in *)&bind_addr;

          if (inet_pton(AF_INET, state->config.bind_address, &addr4->sin_addr) == 1)
            {
              addr4->sin_family = AF_INET;
              addr4->sin_port = htons(state->config.sync_port);
              bind_len = sizeof(struct sockaddr_in);

              /* Need IPv4 socket for IPv4 bind address */
              if (use_ipv6)
                {
                  int reuse2 = 1;

                  close(sock);
                  sock = socket(AF_INET, SOCK_DGRAM, 0);
                  if (sock < 0)
                    {
                      log_error("peer_sync_init: socket(AF_INET) failed: %s", strerror(errno));
                      return -1;
                    }
                  use_ipv6 = 0;
                  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse2, sizeof(reuse2));
                }
            }
          else
            {
              log_error("peer_sync_init: invalid bind_address: %s", state->config.bind_address);
              close(sock);
              return -1;
            }
        }

      if (bind(sock, (struct sockaddr *)&bind_addr, bind_len) < 0)
        {
          log_error("peer_sync_init: bind(%s:%d) failed: %s",
                    state->config.bind_address, state->config.sync_port, strerror(errno));
          close(sock);
          return -1;
        }

      state->config.sync_socket = sock;
      log_info("Peer sync initialized (UDP %s:%d)",
               state->config.bind_address, state->config.sync_port);
    }
  else if (use_ipv6)
    {
      /* No explicit bind address - bind to all interfaces (dual-stack) */
      struct sockaddr_in6 addr6;

      memset(&addr6, 0, sizeof(addr6));
      addr6.sin6_family = AF_INET6;
      addr6.sin6_addr = in6addr_any;
      addr6.sin6_port = htons(state->config.sync_port);

      if (bind(sock, (struct sockaddr *)&addr6, sizeof(addr6)) < 0)
        {
          log_error("peer_sync_init: bind() failed on port %d: %s",
                    state->config.sync_port, strerror(errno));
          close(sock);
          return -1;
        }

      state->config.sync_socket = sock;
      log_info("Peer sync initialized (UDP port %d, dual-stack IPv4/IPv6)",
               state->config.sync_port);
    }
  else
    {
      /* No explicit bind address - bind to all interfaces (IPv4 only) */
      struct sockaddr_in addr4;

      memset(&addr4, 0, sizeof(addr4));
      addr4.sin_family = AF_INET;
      addr4.sin_addr.s_addr = INADDR_ANY;
      addr4.sin_port = htons(state->config.sync_port);

      if (bind(sock, (struct sockaddr *)&addr4, sizeof(addr4)) < 0)
        {
          log_error("peer_sync_init: bind() failed on port %d: %s",
                    state->config.sync_port, strerror(errno));
          close(sock);
          return -1;
        }

      state->config.sync_socket = sock;
      log_info("Peer sync initialized (UDP port %d, IPv4-only)",
               state->config.sync_port);
    }

  return 0;
}

void peer_sync_cleanup(void)
{
  if (g_state && g_state->config.sync_socket >= 0)
    {
      close(g_state->config.sync_socket);
      g_state->config.sync_socket = -1;
    }

  log_info("Peer sync cleaned up");
}

int peer_sync_send_to_peer(struct peer_info *peer, struct sync_message *msg)
{
  ssize_t sent;
  const void *send_buf;
  size_t send_len;
  uint8_t encrypted_buf[MAX_ENCRYPTED_MESSAGE_SIZE];
  size_t encrypted_len;

  if (!peer || !msg || g_state->config.sync_socket < 0)
    return -1;

  if (g_state->security_ctx)
    {
      /* Encrypted mode: encrypt the message */
      int ret = security_encrypt(g_state->security_ctx,
                                 (const uint8_t *)msg, sizeof(struct sync_message),
                                 encrypted_buf, &encrypted_len);
      if (ret != CRYPTO_OK)
        {
          log_error("Failed to encrypt message for peer %s", peer->address);
          peer->errors++;
          return -1;
        }
      send_buf = encrypted_buf;
      send_len = encrypted_len;
    }
  else
    {
      /* Plain mode: send message directly */
      send_buf = msg;
      send_len = sizeof(struct sync_message);
    }

  /* Use sendmsg() with IP_PKTINFO/IPV6_PKTINFO if source address specified */
  if (peer->has_source_address)
    {
      struct msghdr mhdr;
      struct iovec iov;
      char control[CMSG_SPACE(sizeof(struct in_pktinfo)) > CMSG_SPACE(sizeof(struct in6_pktinfo)) ?
                                            CMSG_SPACE(sizeof(struct in_pktinfo)) : CMSG_SPACE(sizeof(struct in6_pktinfo))];
      struct cmsghdr *cmsg;
      struct sockaddr_in *src4 = (struct sockaddr_in *)&peer->source_addr;
      struct sockaddr_in6 *src6 = (struct sockaddr_in6 *)&peer->source_addr;

      memset(&mhdr, 0, sizeof(mhdr));
      memset(control, 0, sizeof(control));

      iov.iov_base = (void *)send_buf;
      iov.iov_len = send_len;

      mhdr.msg_name = &peer->addr;
      mhdr.msg_namelen = peer->addr_len;
      mhdr.msg_iov = &iov;
      mhdr.msg_iovlen = 1;
      mhdr.msg_control = control;

      /* Set source address based on address family */
      if (src4->sin_family == AF_INET)
        {
          struct in_pktinfo *pktinfo;

          mhdr.msg_controllen = CMSG_SPACE(sizeof(struct in_pktinfo));
          cmsg = CMSG_FIRSTHDR(&mhdr);
          cmsg->cmsg_level = IPPROTO_IP;
          cmsg->cmsg_type = IP_PKTINFO;
          cmsg->cmsg_len = CMSG_LEN(sizeof(struct in_pktinfo));
          pktinfo = (struct in_pktinfo *)CMSG_DATA(cmsg);
          memset(pktinfo, 0, sizeof(*pktinfo));
          pktinfo->ipi_spec_dst = src4->sin_addr;
        }
      else if (src6->sin6_family == AF_INET6)
        {
          struct in6_pktinfo *pktinfo6;

          mhdr.msg_controllen = CMSG_SPACE(sizeof(struct in6_pktinfo));
          cmsg = CMSG_FIRSTHDR(&mhdr);
          cmsg->cmsg_level = IPPROTO_IPV6;
          cmsg->cmsg_type = IPV6_PKTINFO;
          cmsg->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
          pktinfo6 = (struct in6_pktinfo *)CMSG_DATA(cmsg);
          memset(pktinfo6, 0, sizeof(*pktinfo6));
          pktinfo6->ipi6_addr = src6->sin6_addr;
        }

      sent = sendmsg(g_state->config.sync_socket, &mhdr, 0);
    }
  else
    {
      /* No per-peer source address: use regular sendto() */
      sent = sendto(g_state->config.sync_socket, send_buf, send_len, 0,
                    (struct sockaddr *)&peer->addr, peer->addr_len);
    }

  if (sent < 0)
    {
      log_error("Failed to send to peer %s: %s", peer->address, strerror(errno));
      peer->errors++;
      return -1;
    }

  if ((size_t)sent != send_len)
    {
      log_warning("Partial send to peer %s: %zd/%zu bytes",
                  peer->address, sent, send_len);
    }

  peer->messages_sent++;
  g_state->total_sync_messages_sent++;

  log_debug("Sent %s message (seq %d) to %s%s%s",
            msg->action == SYNC_ACTION_ADD ? "ADD" :
            msg->action == SYNC_ACTION_UPDATE ? "UPDATE" :
            msg->action == SYNC_ACTION_DELETE ? "DELETE" : "OTHER",
            ntohs(msg->sequence),
            peer->address,
            peer->has_source_address ? " (src: " : "",
            peer->has_source_address ? peer->source_address : "");
  if (peer->has_source_address)
    log_debug("  [source: %s]%s", peer->source_address,
              g_state->security_ctx ? " [encrypted]" : "");
  else if (g_state->security_ctx)
    log_debug("  [encrypted]");

  return 0;
}

int peer_sync_broadcast_lease(struct lease_entry *entry, uint8_t action)
{
  struct sync_message msg;
  int sent_count = 0;
  int i;

  if (!entry || !g_state)
    return -1;

  lease_to_sync_message(entry, &msg, action);

  for (i = 0; i < g_state->config.peer_count; i++)
    {
      if (peer_sync_send_to_peer(&g_state->config.peers[i], &msg) == 0)
        sent_count++;
    }

  log_debug("Broadcast lease %s to %d/%d peers",
            entry->ip_str, sent_count, g_state->config.peer_count);

  return sent_count;
}

int peer_sync_broadcast_delete(struct lease_entry *entry, uint64_t expected_ts)
{
  struct sync_message msg;
  int sent_count = 0;
  int i;

  if (!entry || !g_state)
    return -1;

  lease_to_sync_message(entry, &msg, SYNC_ACTION_DELETE);

  /* Set expected_ts to the timestamp of the lease being deleted.
   * This allows receivers to detect if their local lease was renewed
   * after the original lease was created. */
  msg.expected_ts = htobe64(expected_ts);

  for (i = 0; i < g_state->config.peer_count; i++)
    {
      if (peer_sync_send_to_peer(&g_state->config.peers[i], &msg) == 0)
        sent_count++;
    }

  log_debug("Broadcast DELETE for %s to %d/%d peers (expected_ts=%llu)",
            entry->ip_str, sent_count, g_state->config.peer_count,
            (unsigned long long)expected_ts);

  return sent_count;
}

int peer_sync_send_heartbeat(void)
{
  struct sync_message msg;
  int sent_count = 0;
  int i;

  if (!g_state)
    return -1;

  memset(&msg, 0, sizeof(msg));

  msg.magic = htonl(SYNC_MAGIC);
  msg.version = LEASE_SYNC_PROTOCOL_VERSION;
  msg.action = SYNC_ACTION_HEARTBEAT;
  msg.sequence = htons(g_state->message_sequence++);
  strncpy(msg.node_id, g_state->config.node_id, MAX_NODE_ID_LEN - 1);
  msg.timestamp_ms = htobe64(get_timestamp_ms());

  for (i = 0; i < g_state->config.peer_count; i++)
    {
      if (peer_sync_send_to_peer(&g_state->config.peers[i], &msg) == 0)
        sent_count++;
    }

  log_debug("Sent heartbeat to %d peers", sent_count);

  return sent_count;
}

int peer_sync_request_full_sync(void)
{
  struct sync_message msg;
  int sent_count = 0;
  int i;

  if (!g_state)
    return -1;

  memset(&msg, 0, sizeof(msg));

  msg.magic = htonl(SYNC_MAGIC);
  msg.version = LEASE_SYNC_PROTOCOL_VERSION;
  msg.action = SYNC_ACTION_SYNC_REQUEST;
  msg.sequence = htons(g_state->message_sequence++);
  strncpy(msg.node_id, g_state->config.node_id, MAX_NODE_ID_LEN - 1);
  msg.timestamp_ms = htobe64(get_timestamp_ms());

  for (i = 0; i < g_state->config.peer_count; i++)
    {
      if (peer_sync_send_to_peer(&g_state->config.peers[i], &msg) == 0)
        sent_count++;
    }

  log_info("Requested full sync from %d peers", sent_count);

  return sent_count;
}

void peer_sync_handle_message(struct daemon_state *state)
{
  struct sync_message msg;
  struct sockaddr_storage from_addr;
  socklen_t from_len = sizeof(from_addr);
  uint8_t recv_buf[MAX_ENCRYPTED_MESSAGE_SIZE];
  ssize_t received;
  uint16_t seq;
  char from_ip[INET6_ADDRSTRLEN];
  int i;

  /* Receive message */
  received = recvfrom(state->config.sync_socket, recv_buf, sizeof(recv_buf), 0,
                      (struct sockaddr *)&from_addr, &from_len);

  if (received < 0)
    {
      if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
          log_error("recvfrom() failed: %s", strerror(errno));
        }
      return;
    }

  if (state->security_ctx)
    {
      /* Encrypted mode: decrypt the message */
      uint8_t decrypted_buf[sizeof(struct sync_message)];
      size_t decrypted_len;
      int ret;

      ret = security_decrypt(state->security_ctx,
                             recv_buf, (size_t)received,
                             decrypted_buf, &decrypted_len);
      if (ret != CRYPTO_OK)
        {
          if (ret == CRYPTO_ERROR_AUTH)
            {
              log_warning("Received message with invalid authentication tag (tampered or wrong key)");
            }
          else
            {
              log_warning("Failed to decrypt incoming message");
            }
          return;
        }

      if (decrypted_len != sizeof(struct sync_message))
        {
          log_warning("Decrypted message has wrong size: %zu/%zu bytes",
                      decrypted_len, sizeof(struct sync_message));
          return;
        }

      memcpy(&msg, decrypted_buf, sizeof(struct sync_message));
    }
  else
    {
      /* Plain mode: copy message directly */
      if ((size_t)received != sizeof(struct sync_message))
        {
          log_warning("Received incomplete message: %zd/%zu bytes", received, sizeof(msg));
          return;
        }
      memcpy(&msg, recv_buf, sizeof(struct sync_message));
    }

  /* Wire-format node_id is a fixed-size field; guarantee NUL-termination
   * before any string operation (strcmp, %s, strncpy) downstream. */
  msg.node_id[MAX_NODE_ID_LEN - 1] = '\0';

  /* Validate magic */
  if (ntohl(msg.magic) != SYNC_MAGIC)
    {
      log_warning("Invalid magic number in sync message");
      return;
    }

  /* Check protocol version */
  if (msg.version != LEASE_SYNC_PROTOCOL_VERSION)
    {
      log_warning("Unsupported protocol version: %d (expected %d)",
                  msg.version, LEASE_SYNC_PROTOCOL_VERSION);
      return;
    }

  /* Skip our own messages */
  if (strcmp(msg.node_id, state->config.node_id) == 0)
    {
      log_debug("Ignoring own message (seq %d)", ntohs(msg.sequence));
      return;
    }

  /* Deduplication */
  seq = ntohs(msg.sequence);
  if (is_seen_message(seq, msg.node_id))
    {
      log_debug("Ignoring duplicate message (seq %d from %s)", seq, msg.node_id);
      return;
    }
  mark_message_seen(seq, msg.node_id);

  /* Update peer last-seen
   *
   * IMPORTANT: Normalize IPv6-mapped IPv4 addresses to plain IPv4 format
   * for peer comparison. When using a dual-stack IPv6 socket (IPV6_V6ONLY=0),
   * IPv4 peers appear as ::ffff:a.b.c.d but the config has plain a.b.c.d
   */
  if (from_addr.ss_family == AF_INET)
    {
      struct sockaddr_in *addr_in = (struct sockaddr_in *)&from_addr;
      inet_ntop(AF_INET, &addr_in->sin_addr, from_ip, sizeof(from_ip));
    }
  else
    {
      struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)&from_addr;

      /* Check if this is an IPv6-mapped IPv4 address (::ffff:a.b.c.d) */
      if (IN6_IS_ADDR_V4MAPPED(&addr_in6->sin6_addr))
        {
          /* Extract the IPv4 address from the last 4 bytes */
          struct in_addr ipv4_addr;
          memcpy(&ipv4_addr, &addr_in6->sin6_addr.s6_addr[12], sizeof(ipv4_addr));
          inet_ntop(AF_INET, &ipv4_addr, from_ip, sizeof(from_ip));
        }
      else
        {
          /* Regular IPv6 address */
          inet_ntop(AF_INET6, &addr_in6->sin6_addr, from_ip, sizeof(from_ip));
        }
    }

  for (i = 0; i < state->config.peer_count; i++)
    {
      if (strcmp(state->config.peers[i].address, from_ip) == 0)
        {
          state->config.peers[i].last_seen = time(NULL);
          state->config.peers[i].connected = true;
          state->config.peers[i].messages_received++;
          break;
        }
    }

  state->total_sync_messages_received++;

  /* Handle different message types */
  switch (msg.action)
    {
    case SYNC_ACTION_ADD:
    case SYNC_ACTION_UPDATE:
    {
      struct lease_entry entry;
      struct lease_entry *existing;

      sync_message_to_lease(&msg, &entry);

      log_info("Received %s for %s from %s",
               msg.action == SYNC_ACTION_ADD ? "ADD" : "UPDATE",
               entry.ip_str, msg.node_id);

      /* Check if we have a newer lease (race condition protection) */
      existing = lease_db_find(entry.ip_str);
      if (existing && existing->timestamp_ms >= entry.timestamp_ms)
        {
          log_info("Ignoring %s for %s: local lease is same or newer (%llu >= %llu)",
                   msg.action == SYNC_ACTION_ADD ? "ADD" : "UPDATE",
                   entry.ip_str,
                   (unsigned long long)existing->timestamp_ms,
                   (unsigned long long)entry.timestamp_ms);
          break;
        }

      /* Add/update in database */
      if (msg.action == SYNC_ACTION_ADD)
        lease_db_add(&entry);
      else
        lease_db_update(&entry);

      /* Inject into local dnsmasq */
      if (ubus_handler_inject_lease(&entry) < 0)
        {
          log_warning("Failed to inject lease %s, queuing for retry", entry.ip_str);
          retry_queue_add(&g_state->retry_queue, &entry, RETRY_ACTION_ADD);
        }
    }
    break;

    case SYNC_ACTION_DELETE:
    {
      struct lease_entry entry;
      uint64_t expected_ts;
      struct lease_entry *existing;

      sync_message_to_lease(&msg, &entry);

      /* Extract expected_ts for causality check (causality tracking) */
      expected_ts = be64toh(msg.expected_ts);

      log_info("Received DELETE for %s from %s (expected_ts=%llu)",
               entry.ip_str, msg.node_id, (unsigned long long)expected_ts);

      /* Check if we have a local lease */
      existing = lease_db_find(entry.ip_str);

      if (existing)
        {
          /* causality tracking Causality check: If expected_ts is set, use it for comparison.
           * If local timestamp > expected_ts, the local lease was renewed
           * after the original lease (that sender deleted) was created.
           * In this case, don't delete - broadcast UPDATE to inform sender. */
          if (expected_ts > 0 && existing->timestamp_ms > expected_ts)
            {
              log_info("Local lease for %s is newer than deleted lease "
                       "(local=%llu > expected=%llu), broadcasting UPDATE",
                       entry.ip_str,
                       (unsigned long long)existing->timestamp_ms,
                       (unsigned long long)expected_ts);

              /* Broadcast UPDATE to inform all peers of the newer lease */
              peer_sync_broadcast_lease(existing, SYNC_ACTION_UPDATE);
              break;
            }

          /* Defensive: Handle case where expected_ts is not set (ADD/UPDATE reuse) */
          if (expected_ts == 0 && existing->timestamp_ms >= entry.timestamp_ms)
            {
              log_info("Ignoring DELETE for %s: local lease is same or newer (%llu >= %llu)",
                       entry.ip_str,
                       (unsigned long long)existing->timestamp_ms,
                       (unsigned long long)entry.timestamp_ms);
              break;
            }
        }

      /* Delete from database */
      lease_db_delete(entry.ip_str);

      /* Delete from local dnsmasq */
      if (ubus_handler_delete_lease(entry.ip_str) < 0)
        {
          log_warning("Failed to delete lease %s, queuing for retry", entry.ip_str);
          retry_queue_add(&g_state->retry_queue, &entry, RETRY_ACTION_DELETE);
        }
    }
    break;

    case SYNC_ACTION_SYNC_REQUEST:
    {
      struct peer_info *requester = NULL;
      int sent_count = 0;

      log_info("Received sync request from %s", msg.node_id);

      /* Find the peer that sent the request */
      for (i = 0; i < state->config.peer_count; i++)
        {
          if (strcmp(state->config.peers[i].address, from_ip) == 0)
            {
              requester = &state->config.peers[i];
              break;
            }
        }

      if (!requester)
        {
          log_warning("Sync request from unknown peer %s", from_ip);
          break;
        }

      /* Send all our leases to the requester */
      for (i = 0; i < HASH_TABLE_SIZE; i++)
        {
          struct lease_entry *entry = state->lease_table[i];
          while (entry)
            {
              struct sync_message response;
              lease_to_sync_message(entry, &response, SYNC_ACTION_SYNC_RESPONSE);
              if (peer_sync_send_to_peer(requester, &response) == 0)
                {
                  sent_count++;
                }
              entry = entry->next;
            }
        }

      log_info("Sent %d leases to %s in sync response", sent_count, from_ip);
    }
    break;

    case SYNC_ACTION_SYNC_RESPONSE:
    {
      struct lease_entry entry;
      struct lease_entry *existing;

      sync_message_to_lease(&msg, &entry);

      log_debug("Received SYNC_RESPONSE for %s from %s", entry.ip_str, msg.node_id);

      /* Check if we have a newer lease (same logic as ADD/UPDATE) */
      existing = lease_db_find(entry.ip_str);
      if (existing && existing->timestamp_ms >= entry.timestamp_ms)
        {
          log_debug("Ignoring SYNC_RESPONSE for %s: local lease is same or newer",
                    entry.ip_str);
          break;
        }

      /* Add/update in database */
      if (existing)
        lease_db_update(&entry);
      else
        lease_db_add(&entry);

      /* Inject into local dnsmasq */
      if (ubus_handler_inject_lease(&entry) < 0)
        {
          log_warning("Failed to inject lease %s from sync, queuing for retry", entry.ip_str);
          retry_queue_add(&g_state->retry_queue, &entry, RETRY_ACTION_ADD);
        }
    }
    break;

    case SYNC_ACTION_HEARTBEAT:
      log_debug("Received heartbeat from %s", msg.node_id);
      /* Already updated last_seen above */
      break;

    default:
      log_warning("Unknown sync action: %d from %s", msg.action, msg.node_id);
      break;
    }
}

void peer_sync_update_status(void)
{
  time_t now;
  int i;

  if (!g_state)
    return;

  now = time(NULL);

  for (i = 0; i < g_state->config.peer_count; i++)
    {
      struct peer_info *peer = &g_state->config.peers[i];

      /* Check if peer has timed out */
      if (peer->connected && (now - peer->last_seen) > g_state->config.peer_timeout)
        {
          peer->connected = false;
          log_warning("Peer %s timed out (last seen %ld seconds ago)",
                      peer->address, now - peer->last_seen);
        }
    }
}
