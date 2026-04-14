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

#ifndef LEASE_SYNC_COMMON_H
#define LEASE_SYNC_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "log.h"

/* Forward declaration for encryption context */
struct security_context;

#define LEASE_SYNC_VERSION "1.2.0"
#define LEASE_SYNC_PROTOCOL_VERSION 1

#define MAX_PEERS 10
#define MAX_HOSTNAME_LEN 256
#define MAX_CLIENT_ID_LEN 256
#define MAX_NODE_ID_LEN 64
#define MAX_INTERFACE_LEN 16
#define HASH_TABLE_SIZE 1024
#define SEEN_RING_SIZE 256

#define DEFAULT_SYNC_PORT 5378
#define DEFAULT_SYNC_INTERVAL 30
#define DEFAULT_PEER_TIMEOUT 120

/* Maximum encrypted message size: struct sync_message + crypto overhead (28 bytes) */
#define MAX_ENCRYPTED_MESSAGE_SIZE (MAX_UDP_PACKET_SIZE)

/* Wire-format address family constants (platform-independent).
 * AF_INET/AF_INET6 values differ across platforms (Linux 2/10, macOS 2/30),
 * so the sync protocol uses its own constants for portability. */
#define SYNC_AF_INET4 1
#define SYNC_AF_INET6 2

#define SYNC_MAGIC 0x4C53594E  /* "LSYN" - Lease SYNc protocol identifier */
#define MAX_UDP_PACKET_SIZE 1400

/* Sync message actions */
#define SYNC_ACTION_ADD 1
#define SYNC_ACTION_UPDATE 2
#define SYNC_ACTION_DELETE 3
#define SYNC_ACTION_SYNC_REQUEST 4
#define SYNC_ACTION_SYNC_RESPONSE 5
#define SYNC_ACTION_HEARTBEAT 6

/* Retry Queue Constants */
#define RETRY_QUEUE_SIZE 64
#define RETRY_MAX_ATTEMPTS 3
#define RETRY_INTERVAL_SECONDS 5

#define RETRY_ACTION_ADD 1
#define RETRY_ACTION_DELETE 2

/* Startup Reconciliation Constants */
#define STARTUP_SYNC_WAIT_SECONDS 5

/* Startup Phase Tracking */
enum startup_phase
{
  STARTUP_PHASE_INIT = 0,           /* Initial state, daemon starting */
  STARTUP_PHASE_SYNC_REQUESTED = 1, /* Full sync requested from peers */
  STARTUP_PHASE_RECONCILING = 2,    /* Comparing dnsmasq state vs lease_db */
  STARTUP_PHASE_COMPLETE = 3        /* Reconciliation complete */
};

struct lease_entry
{
  /* IP address (IPv4 or IPv6) */
  int af_family;                          /* AF_INET or AF_INET6 */
  union
  {
    struct in_addr addr4;
    struct in6_addr addr6;
  } addr;
  char ip_str[INET6_ADDRSTRLEN];         /* String representation */

  /* MAC address */
  char mac[18];                           /* Format: aa:bb:cc:dd:ee:ff */

  /* Hostname */
  char hostname[MAX_HOSTNAME_LEN];

  /* Client identification */
  unsigned char client_id[MAX_CLIENT_ID_LEN];
  int client_id_len;

  /* Lease timing */
  time_t expires;                         /* Unix timestamp */
  uint32_t lease_length;                  /* Duration in seconds */

  /* IPv6 specific */
  uint32_t iaid;                          /* Identity Association ID */
  bool is_temporary;                      /* Temporary address flag */

  /* Network information */
  char interface[MAX_INTERFACE_LEN];

  /* Metadata */
  char source_node[MAX_NODE_ID_LEN];     /* Node that created this lease */
  uint64_t timestamp_ms;                  /* For conflict resolution */
  uint16_t sequence;                      /* Message sequence number */

  /* Hash table linkage */
  struct lease_entry *next;               /* Collision chain */
};

struct retry_entry
{
  uint8_t action;                     /* RETRY_ACTION_ADD or RETRY_ACTION_DELETE */
  struct lease_entry lease;           /* Copy of lease data */
  int retry_count;
  time_t first_queued;
  time_t last_attempt;
  struct retry_entry *next;
};

struct retry_queue
{
  struct retry_entry *head;
  struct retry_entry *tail;
  int count;
};

/* Sync Message Structure (Network Protocol) */
struct __attribute__((packed)) sync_message
{
  /* Header */
  uint32_t magic;                         /* SYNC_MAGIC */
  uint8_t version;                        /* Protocol version */
  uint8_t action;                         /* SYNC_ACTION_* */
  uint16_t sequence;                      /* Sequence number */

  /* Originator */
  char node_id[MAX_NODE_ID_LEN];
  uint64_t timestamp_ms;

  /* Lease data */
  uint8_t af_family;                      /* SYNC_AF_INET4 or SYNC_AF_INET6 */
  union
  {
    struct in_addr addr4;
    struct in6_addr addr6;
  } addr;

  char mac[18];
  char hostname[MAX_HOSTNAME_LEN];

  uint8_t client_id[MAX_CLIENT_ID_LEN];
  uint16_t client_id_len;

  uint32_t expires;                       /* Unix timestamp */
  uint32_t lease_length;

  /* IPv6 fields */
  uint32_t iaid;
  uint8_t is_temporary;

  char interface[MAX_INTERFACE_LEN];

  /* Causality field for DELETE (expected timestamp for conflict detection)
   * For DELETE messages: the timestamp of the lease being deleted.
   * Receiver compares local timestamp against this to detect if local
   * lease was renewed after the original lease was created.
   */
  uint64_t expected_ts;
};

struct peer_info
{
  char address[INET6_ADDRSTRLEN];
  struct sockaddr_storage addr;
  socklen_t addr_len;

  /* Per-peer source address for outgoing packets */
  char source_address[INET6_ADDRSTRLEN];
  struct sockaddr_storage source_addr;
  socklen_t source_addr_len;
  bool has_source_address;

  time_t last_seen;
  uint64_t last_heartbeat;
  bool connected;

  /* Statistics */
  uint64_t messages_sent;
  uint64_t messages_received;
  uint64_t errors;
};

struct config
{
  char node_id[MAX_NODE_ID_LEN];

  /* Peer configuration */
  struct peer_info peers[MAX_PEERS];
  int peer_count;

  /* Network */
  uint16_t sync_port;
  int sync_socket;
  char bind_address[INET6_ADDRSTRLEN];  /* Explicit bind address (empty = all interfaces) */

  /* Timing */
  int sync_interval;                      /* Periodic full sync interval (seconds) */
  int peer_timeout;                       /* Peer considered down after (seconds) */

  /* Paths */
  char node_id_file[256];

  /* Flags */
  bool daemon_mode;
  int log_level;

  /* Security */
  enum
  {
    SECURITY_MODE_ENCRYPTED = 0,  /* Use PSK encryption (future) */
    SECURITY_MODE_PLAIN = 1       /* Plaintext (requires acknowledgment) */
  } security_mode;
  bool plain_mode_acknowledged;  /* Explicit opt-in to unencrypted mode */
  char psk_key[256];              /* Pre-shared key for encryption (future) */
};

struct daemon_state
{
  struct config config;

  /* Encryption context (NULL if plain mode) */
  struct security_context *security_ctx;

  /* Lease database */
  struct lease_entry *lease_table[HASH_TABLE_SIZE];
  int lease_count;
  int local_lease_count;
  int peer_lease_count;

  /* Message deduplication */
  struct
  {
    uint16_t sequence;
    char node_id[MAX_NODE_ID_LEN];
    time_t timestamp;
  } seen_messages[SEEN_RING_SIZE];
  int seen_index;

  /* ubus */
  struct ubus_context *ubus_ctx;

  /* Sequence counter */
  uint16_t message_sequence;

  /* Running state */
  time_t start_time;

  /* Startup reconciliation */
  enum startup_phase startup_phase;
  time_t startup_sync_request_time;
  bool reconciliation_in_progress;  /* True during startup reconciliation - skip broadcasts */

  /* Statistics */
  uint64_t total_leases_added;
  uint64_t total_leases_deleted;
  uint64_t total_leases_updated;
  uint64_t total_sync_messages_sent;
  uint64_t total_sync_messages_received;
  uint64_t total_conflicts_resolved;

  /* Retry queue statistics */
  uint64_t total_injection_retries;
  uint64_t total_injection_drops;

  /* Reconciliation statistics */
  uint64_t total_leases_reconciled;

  /* Retry queue */
  struct retry_queue retry_queue;
};

/* Global daemon state */
extern struct daemon_state *g_state;

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

/* lease-db.c */
int lease_db_init(void);
void lease_db_cleanup(void);
struct lease_entry *lease_db_find(const char *ip);
int lease_db_add(struct lease_entry *entry);
int lease_db_update(struct lease_entry *entry);
int lease_db_delete(const char *ip);
int lease_db_count(void);
void lease_db_foreach(void (*callback)(struct lease_entry *entry, void *user_data), void *user_data);

/* peer-sync.c */
int peer_sync_init(struct daemon_state *state);
void peer_sync_cleanup(void);
int peer_sync_send_lease(struct lease_entry *entry, uint8_t action);
int peer_sync_broadcast_lease(struct lease_entry *entry, uint8_t action);
int peer_sync_broadcast_delete(struct lease_entry *entry, uint64_t expected_ts);
void peer_sync_handle_message(struct daemon_state *state);
int peer_sync_request_full_sync(void);
int peer_sync_send_heartbeat(void);
void peer_sync_update_status(void);

/* ubus-handler.c */
int ubus_handler_init(struct daemon_state *state);
void ubus_handler_cleanup(void);
int ubus_handler_inject_lease(struct lease_entry *entry);
int ubus_handler_delete_lease(const char *ip);

/* Callback type for iterating dnsmasq leases */
typedef void (*lease_enum_callback_t)(const char *ip, const char *mac,
                                      const char *hostname, time_t expires,
                                      void *user_data);
int ubus_handler_get_leases(lease_enum_callback_t callback, void *user_data);

/* Retry queue functions (retry-queue.c) */
int retry_queue_init(struct retry_queue *queue);
void retry_queue_cleanup(struct retry_queue *queue);
int retry_queue_add(struct retry_queue *queue, struct lease_entry *entry, uint8_t action);
void retry_queue_process(void);
int retry_queue_count(void);

/* config.c */
int config_load(struct config *config, const char *config_file);
int config_save(struct config *config, const char *config_file);
void config_set_defaults(struct config *config);
int config_load_node_id(struct config *config);
int config_generate_node_id(struct config *config);
int config_ensure_directories(struct config *config);

/* util.c */
uint64_t get_timestamp_ms(void);
uint32_t hash_string(const char *str);
int parse_ip_address(const char *str, int *af_family, void *addr);
const char *format_mac(const unsigned char *mac, int len, char *buf, size_t bufsz);
int parse_mac(const char *str, unsigned char *mac, size_t max_len, int *len);
bool is_seen_message(uint16_t sequence, const char *node_id);
void mark_message_seen(uint16_t sequence, const char *node_id);
char *trim_whitespace(char *str);
int parse_int(const char *str, int *value);

#endif /* LEASE_SYNC_COMMON_H */
