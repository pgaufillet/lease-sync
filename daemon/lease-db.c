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
 * lease-db.c - In-memory lease database implementation
 *
 * Uses a hash table with chaining for O(1) average case lookups.
 * Single-threaded: all access is from the uloop event loop.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

/* Global state (defined in main) */
extern struct daemon_state *g_state;

static uint32_t hash_ip(const char *ip)
{
  uint32_t hash = 5381;
  int c;

  while ((c = *ip++))
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

  return hash % HASH_TABLE_SIZE;
}

int lease_db_init(void)
{
  if (!g_state)
    {
      log_error("lease_db_init: g_state is NULL");
      return -1;
    }

  /* Initialize hash table */
  memset(g_state->lease_table, 0, sizeof(g_state->lease_table));
  g_state->lease_count = 0;
  g_state->local_lease_count = 0;
  g_state->peer_lease_count = 0;

  log_info("Lease database initialized (hash table size: %d)", HASH_TABLE_SIZE);

  return 0;
}

void lease_db_cleanup(void)
{
  int i;

  if (!g_state)
    return;

  /* Free all lease entries */
  for (i = 0; i < HASH_TABLE_SIZE; i++)
    {
      struct lease_entry *entry = g_state->lease_table[i];
      while (entry)
        {
          struct lease_entry *next = entry->next;
          free(entry);
          entry = next;
        }
      g_state->lease_table[i] = NULL;
    }

  g_state->lease_count = 0;
  g_state->local_lease_count = 0;
  g_state->peer_lease_count = 0;

  log_info("Lease database cleaned up");
}

struct lease_entry *lease_db_find(const char *ip)
{
  uint32_t index;
  struct lease_entry *entry;

  if (!g_state || !ip)
    return NULL;

  index = hash_ip(ip);
  entry = g_state->lease_table[index];

  while (entry)
    {
      if (strcmp(entry->ip_str, ip) == 0)
        return entry;
      entry = entry->next;
    }

  return NULL;
}

int lease_db_add(struct lease_entry *new_entry)
{
  uint32_t index;
  struct lease_entry *entry;
  struct lease_entry *copy;

  if (!g_state || !new_entry)
    {
      log_error("lease_db_add: invalid parameters");
      return -1;
    }

  index = hash_ip(new_entry->ip_str);

  /* Check if entry already exists */
  entry = g_state->lease_table[index];

  while (entry)
    {
      if (strcmp(entry->ip_str, new_entry->ip_str) == 0)
        {
          /* Entry exists - this is an update, not add */
          log_warning("lease_db_add: lease %s already exists, use update instead",
                      new_entry->ip_str);
          return lease_db_update(new_entry);
        }
      entry = entry->next;
    }

  /* Allocate new entry */
  copy = malloc(sizeof(struct lease_entry));
  if (!copy)
    {
      log_error("lease_db_add: failed to allocate memory");
      return -1;
    }

  memcpy(copy, new_entry, sizeof(struct lease_entry));
  copy->next = g_state->lease_table[index];
  g_state->lease_table[index] = copy;

  g_state->lease_count++;

  /* Update statistics */
  if (strcmp(copy->source_node, g_state->config.node_id) == 0)
    g_state->local_lease_count++;
  else
    g_state->peer_lease_count++;

  g_state->total_leases_added++;

  log_debug("Added lease: %s (%s) from node %s",
            copy->ip_str, copy->hostname, copy->source_node);

  return 0;
}

int lease_db_update(struct lease_entry *new_entry)
{
  uint32_t index;
  struct lease_entry *entry;

  if (!g_state || !new_entry)
    {
      log_error("lease_db_update: invalid parameters");
      return -1;
    }

  index = hash_ip(new_entry->ip_str);
  entry = g_state->lease_table[index];

  while (entry)
    {
      if (strcmp(entry->ip_str, new_entry->ip_str) == 0)
        {
          struct lease_entry *next;
          bool was_local;
          bool is_local;

          /* Conflict resolution: newer timestamp wins */
          if (new_entry->timestamp_ms <= entry->timestamp_ms)
            {
              log_debug("Ignoring older lease update for %s (local: %llu, remote: %llu)",
                        new_entry->ip_str,
                        (unsigned long long)entry->timestamp_ms,
                        (unsigned long long)new_entry->timestamp_ms);
              return 0;  /* Not an error, just ignored */
            }

          /* Check if MAC changed (conflict) */
          if (strcmp(entry->mac, new_entry->mac) != 0)
            {
              log_warning("Conflict detected for %s: MAC changed from %s to %s (resolving by timestamp)",
                          entry->ip_str, entry->mac, new_entry->mac);
              g_state->total_conflicts_resolved++;
            }

          /* Update entry in place */
          next = entry->next;  /* Preserve chain */

          /* Update source node statistics if changed */
          was_local = (strcmp(entry->source_node, g_state->config.node_id) == 0);
          is_local = (strcmp(new_entry->source_node, g_state->config.node_id) == 0);

          if (was_local && !is_local)
            {
              g_state->local_lease_count--;
              g_state->peer_lease_count++;
            }
          else if (!was_local && is_local)
            {
              g_state->peer_lease_count--;
              g_state->local_lease_count++;
            }

          memcpy(entry, new_entry, sizeof(struct lease_entry));
          entry->next = next;

          g_state->total_leases_updated++;

          log_debug("Updated lease: %s (%s) from node %s",
                    entry->ip_str, entry->hostname, entry->source_node);

          return 0;
        }
      entry = entry->next;
    }

  /* Entry not found - add it */
  log_debug("lease_db_update: lease %s not found, adding instead", new_entry->ip_str);
  return lease_db_add(new_entry);
}

int lease_db_delete(const char *ip)
{
  uint32_t index;
  struct lease_entry *entry;
  struct lease_entry *prev = NULL;

  if (!g_state || !ip)
    {
      log_error("lease_db_delete: invalid parameters");
      return -1;
    }

  index = hash_ip(ip);
  entry = g_state->lease_table[index];

  while (entry)
    {
      if (strcmp(entry->ip_str, ip) == 0)
        {
          /* Found - remove from chain */
          if (prev)
            prev->next = entry->next;
          else
            g_state->lease_table[index] = entry->next;

          /* Update statistics */
          if (strcmp(entry->source_node, g_state->config.node_id) == 0)
            g_state->local_lease_count--;
          else
            g_state->peer_lease_count--;

          g_state->lease_count--;
          g_state->total_leases_deleted++;

          log_debug("Deleted lease: %s (%s)", entry->ip_str, entry->hostname);

          free(entry);
          return 0;
        }
      prev = entry;
      entry = entry->next;
    }

  log_debug("lease_db_delete: lease %s not found", ip);
  return -1;  /* Not found */
}

int lease_db_count(void)
{
  if (!g_state)
    return 0;

  return g_state->lease_count;
}

void lease_db_foreach(void (*callback)(struct lease_entry *entry, void *user_data), void *user_data)
{
  int i;

  if (!g_state || !callback)
    return;

  for (i = 0; i < HASH_TABLE_SIZE; i++)
    {
      struct lease_entry *entry = g_state->lease_table[i];
      while (entry)
        {
          callback(entry, user_data);
          entry = entry->next;
        }
    }
}

static void print_lease_callback(struct lease_entry *entry, void *user_data)
{
  (void)user_data;
  log_debug("  %s: %s (%s) expires=%ld source=%s ts=%llu",
            entry->ip_str,
            entry->mac,
            entry->hostname,
            entry->expires,
            entry->source_node,
            (unsigned long long)entry->timestamp_ms);
}

void lease_db_dump(void)
{
  log_debug("Lease database dump (%d total):", lease_db_count());
  lease_db_foreach(print_lease_callback, NULL);
}
