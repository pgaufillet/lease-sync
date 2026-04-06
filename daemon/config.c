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
 * config.c - Configuration file parsing and management
 *
 * Configuration file format (simple key=value):
 *   node_id=node1
 *   peer=192.168.1.10
 *   peer=192.168.1.11
 *   sync_port=5378
 *   sync_interval=30
 *   persist_interval=60
 *   peer_timeout=120
 *   persist_file=/var/lib/lease-sync/leases.db
 *   debug=1
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#include "common.h"
#include "crypto.h"

void config_set_defaults(struct config *config)
{
  if (!config)
    return;

  memset(config, 0, sizeof(struct config));

  /* Default node ID (will be loaded or generated later) */
  strncpy(config->node_id, "node-unknown", sizeof(config->node_id) - 1);
  config->node_id[sizeof(config->node_id) - 1] = '\0';

  /* Network defaults */
  config->sync_port = DEFAULT_SYNC_PORT;
  config->sync_socket = -1;
  config->bind_address[0] = '\0';  /* Empty = bind to all interfaces */

  /* Timing defaults */
  config->sync_interval = DEFAULT_SYNC_INTERVAL;
  config->persist_interval = DEFAULT_PERSIST_INTERVAL;
  config->peer_timeout = DEFAULT_PEER_TIMEOUT;

  /* Paths - use compile-time STATEDIR (defaults to /var/lib/lease-sync) */
#ifndef STATEDIR
#define STATEDIR "/var/lib/lease-sync"
#endif
  snprintf(config->persist_file, sizeof(config->persist_file), "%s/leases.db", STATEDIR);
  snprintf(config->node_id_file, sizeof(config->node_id_file), "%s/node_id", STATEDIR);

  /* Flags */
  config->daemon_mode = false;
  config->log_level = LOG_LEVEL_INFO;

  /* Security - Secure by default (like owsync) */
  config->security_mode = SECURITY_MODE_ENCRYPTED;
  config->plain_mode_acknowledged = false;
  config->psk_key[0] = '\0';

  /* No peers by default */
  config->peer_count = 0;
}

int config_generate_node_id(struct config *config)
{
  char hostname[64] = "unknown";
  uint32_t rand_suffix;

  if (!config)
    return -1;

  /* Generate based on hostname + random suffix */
  gethostname(hostname, sizeof(hostname) - 1);

  /* Truncate hostname if too long */
  if (strlen(hostname) > 32)
    hostname[32] = '\0';

  /* Add random suffix */
  rand_suffix = (uint32_t)time(NULL) ^ (uint32_t)getpid();

  snprintf(config->node_id, MAX_NODE_ID_LEN, "%s-%08x", hostname, rand_suffix);

  log_info("Generated node ID: %s", config->node_id);

  return 0;
}

int config_load_node_id(struct config *config)
{
  FILE *f;
  char *p;

  if (!config)
    return -1;

  f = fopen(config->node_id_file, "r");
  if (!f)
    {
      /* File doesn't exist - generate new ID */
      if (config_generate_node_id(config) < 0)
        return -1;

      /* Save it */
      f = fopen(config->node_id_file, "w");
      if (f)
        {
          fprintf(f, "%s\n", config->node_id);
          fclose(f);
          log_info("Saved node ID to %s", config->node_id_file);
        }
      else
        {
          log_warning("Failed to save node ID: %s", strerror(errno));
        }

      return 0;
    }

  /* Read node ID from file */
  if (fgets(config->node_id, MAX_NODE_ID_LEN, f) == NULL)
    {
      fclose(f);
      log_error("Failed to read node ID from %s", config->node_id_file);
      return -1;
    }

  fclose(f);

  /* Trim whitespace */
  p = trim_whitespace(config->node_id);
  if (p != config->node_id)
    memmove(config->node_id, p, strlen(p) + 1);

  log_info("Loaded node ID: %s", config->node_id);

  return 0;
}

static int config_add_peer(struct config *config, const char *value)
{
  struct peer_info *peer;
  char peer_addr[INET6_ADDRSTRLEN];
  char source_addr[INET6_ADDRSTRLEN];
  const char *comma;
  struct sockaddr_in *addr4;
  struct sockaddr_in6 *addr6;

  if (!config || !value)
    return -1;

  if (config->peer_count >= MAX_PEERS)
    {
      log_error("Maximum number of peers reached (%d)", MAX_PEERS);
      return -1;
    }

  peer = &config->peers[config->peer_count];
  memset(peer, 0, sizeof(struct peer_info));

  /* Parse format "peer_address[,source_address]" */
  memset(source_addr, 0, sizeof(source_addr));
  comma = strchr(value, ',');

  if (comma)
    {
      /* Has source address: extract both parts */
      size_t peer_len = comma - value;
      if (peer_len >= sizeof(peer_addr))
        {
          log_error("Peer address too long: %s", value);
          return -1;
        }
      strncpy(peer_addr, value, peer_len);
      peer_addr[peer_len] = '\0';
      strncpy(source_addr, comma + 1, sizeof(source_addr) - 1);
    }
  else
    {
      /* No source address: use entire value as peer address */
      strncpy(peer_addr, value, sizeof(peer_addr) - 1);
    }

  strncpy(peer->address, peer_addr, sizeof(peer->address) - 1);

  /* Parse peer address and populate sockaddr */
  addr4 = (struct sockaddr_in *)&peer->addr;
  addr6 = (struct sockaddr_in6 *)&peer->addr;

  /* Try IPv4 first */
  if (inet_pton(AF_INET, peer_addr, &addr4->sin_addr) == 1)
    {
      addr4->sin_family = AF_INET;
      addr4->sin_port = htons(config->sync_port);
      peer->addr_len = sizeof(struct sockaddr_in);
    }
  /* Try IPv6 */
  else if (inet_pton(AF_INET6, peer_addr, &addr6->sin6_addr) == 1)
    {
      addr6->sin6_family = AF_INET6;
      addr6->sin6_port = htons(config->sync_port);
      peer->addr_len = sizeof(struct sockaddr_in6);
    }
  else
    {
      log_error("Invalid peer address: %s", peer_addr);
      return -1;
    }

  /* Parse source address if provided */
  if (source_addr[0] != '\0')
    {
      struct sockaddr_in *src4 = (struct sockaddr_in *)&peer->source_addr;
      struct sockaddr_in6 *src6 = (struct sockaddr_in6 *)&peer->source_addr;

      strncpy(peer->source_address, source_addr, sizeof(peer->source_address) - 1);

      if (inet_pton(AF_INET, source_addr, &src4->sin_addr) == 1)
        {
          src4->sin_family = AF_INET;
          peer->source_addr_len = sizeof(struct sockaddr_in);
          peer->has_source_address = true;
        }
      else if (inet_pton(AF_INET6, source_addr, &src6->sin6_addr) == 1)
        {
          src6->sin6_family = AF_INET6;
          peer->source_addr_len = sizeof(struct sockaddr_in6);
          peer->has_source_address = true;
        }
      else
        {
          log_error("Invalid source address for peer %s: %s", peer_addr, source_addr);
          return -1;
        }
    }

  peer->connected = false;
  peer->last_seen = 0;

  config->peer_count++;

  if (peer->has_source_address)
    {
      log_info("Added peer: %s (source: %s, total: %d)", peer_addr, source_addr, config->peer_count);
    }
  else
    {
      log_info("Added peer: %s (total: %d)", peer_addr, config->peer_count);
    }

  return 0;
}

int config_load(struct config *config, const char *config_file)
{
  FILE *f;
  char line[512];
  int line_num = 0;

  if (!config || !config_file)
    return -1;

  f = fopen(config_file, "r");
  if (!f)
    {
      log_warning("Config file not found: %s (using defaults)", config_file);
      return 0;  /* Not a fatal error - use defaults */
    }

  while (fgets(line, sizeof(line), f))
    {
      char *p;
      char *eq;
      char *key;
      char *value;

      line_num++;

      /* Trim whitespace */
      p = trim_whitespace(line);

      /* Skip empty lines and comments */
      if (*p == '\0' || *p == '#')
        continue;

      /* Parse key=value */
      eq = strchr(p, '=');
      if (!eq)
        {
          log_warning("Invalid config line %d: %s", line_num, p);
          continue;
        }

      *eq = '\0';
      key = trim_whitespace(p);
      value = trim_whitespace(eq + 1);

      /* Process configuration keys */
      if (strcmp(key, "node_id") == 0)
        {
          strncpy(config->node_id, value, MAX_NODE_ID_LEN - 1);
        }
      else if (strcmp(key, "peer") == 0)
        {
          config_add_peer(config, value);
        }
      else if (strcmp(key, "sync_port") == 0)
        {
          int port;
          if (parse_int(value, &port) == 0 && port > 0 && port < 65536)
            config->sync_port = (uint16_t)port;
          else
            log_warning("Invalid sync_port value: %s", value);
        }
      else if (strcmp(key, "sync_interval") == 0)
        {
          int interval;
          if (parse_int(value, &interval) == 0 && interval > 0)
            config->sync_interval = interval;
        }
      else if (strcmp(key, "persist_interval") == 0)
        {
          int interval;
          if (parse_int(value, &interval) == 0 && interval > 0)
            config->persist_interval = interval;
        }
      else if (strcmp(key, "peer_timeout") == 0)
        {
          int timeout;
          if (parse_int(value, &timeout) == 0 && timeout > 0)
            config->peer_timeout = timeout;
        }
      else if (strcmp(key, "persist_file") == 0)
        {
          strncpy(config->persist_file, value, sizeof(config->persist_file) - 1);
        }
      else if (strcmp(key, "node_id_file") == 0)
        {
          strncpy(config->node_id_file, value, sizeof(config->node_id_file) - 1);
        }
      else if (strcmp(key, "debug") == 0)
        {
          /* Backward compatibility: debug=1 sets log_level to DEBUG */
          if (strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0)
            config->log_level = LOG_LEVEL_DEBUG;
        }
      else if (strcmp(key, "log_level") == 0)
        {
          int level;
          if (parse_int(value, &level) == 0)
            config->log_level = level;
        }
      else if (strcmp(key, "security_mode") == 0)
        {
          if (strcasecmp(value, "plain") == 0)
            config->security_mode = SECURITY_MODE_PLAIN;
          else if (strcasecmp(value, "encrypted") == 0)
            config->security_mode = SECURITY_MODE_ENCRYPTED;
          else
            log_warning("Invalid security_mode: %s (must be 'plain' or 'encrypted')", value);
        }
      else if (strcmp(key, "plain_mode_acknowledged") == 0)
        {
          config->plain_mode_acknowledged =
            (strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0);
        }
      else if (strcmp(key, "psk_key") == 0)
        {
          /* Validate key format: must be 64 hex characters (32 bytes for AES-256) */
          if (security_validate_key(value))
            {
              strncpy(config->psk_key, value, sizeof(config->psk_key) - 1);
            }
          else
            {
              log_error("Invalid psk_key: must be 64 hexadecimal characters");
            }
        }
      else if (strcmp(key, "bind_address") == 0)
        {
          /* Explicit bind address for source address selection */
          strncpy(config->bind_address, value, sizeof(config->bind_address) - 1);
          config->bind_address[sizeof(config->bind_address) - 1] = '\0';
        }
      else
        {
          log_warning("Unknown config key at line %d: %s", line_num, key);
        }
    }

  fclose(f);

  log_info("Loaded configuration from %s", config_file);

  return 0;
}

int config_save(struct config *config, const char *config_file)
{
  FILE *f;
  int i;

  if (!config || !config_file)
    return -1;

  f = fopen(config_file, "w");
  if (!f)
    {
      log_error("Failed to open config file for writing: %s", strerror(errno));
      return -1;
    }

  fprintf(f, "# lease-sync daemon configuration\n");
  fprintf(f, "# Generated automatically - edit with caution\n\n");

  fprintf(f, "node_id=%s\n", config->node_id);
  fprintf(f, "\n");

  for (i = 0; i < config->peer_count; i++)
    {
      fprintf(f, "peer=%s\n", config->peers[i].address);
    }
  fprintf(f, "\n");

  fprintf(f, "sync_port=%d\n", config->sync_port);
  fprintf(f, "sync_interval=%d\n", config->sync_interval);
  fprintf(f, "persist_interval=%d\n", config->persist_interval);
  fprintf(f, "peer_timeout=%d\n", config->peer_timeout);
  fprintf(f, "\n");

  fprintf(f, "persist_file=%s\n", config->persist_file);
  fprintf(f, "node_id_file=%s\n", config->node_id_file);
  fprintf(f, "\n");

  fprintf(f, "log_level=%d\n", config->log_level);

  fclose(f);

  log_info("Saved configuration to %s", config_file);

  return 0;
}

int config_ensure_directories(struct config *config)
{
  char dir[256];
  char *last_slash;

  if (!config)
    return -1;

  /* Extract directory from persist_file */
  strncpy(dir, config->persist_file, sizeof(dir) - 1);

  last_slash = strrchr(dir, '/');
  if (last_slash)
    {
      *last_slash = '\0';

      /* Create directory if it doesn't exist */
      if (mkdir(dir, 0755) < 0 && errno != EEXIST)
        {
          log_error("Failed to create directory %s: %s", dir, strerror(errno));
          return -1;
        }
    }

  /* Extract directory from node_id_file */
  strncpy(dir, config->node_id_file, sizeof(dir) - 1);
  last_slash = strrchr(dir, '/');
  if (last_slash)
    {
      *last_slash = '\0';

      if (mkdir(dir, 0755) < 0 && errno != EEXIST)
        {
          log_error("Failed to create directory %s: %s", dir, strerror(errno));
          return -1;
        }
    }

  return 0;
}
