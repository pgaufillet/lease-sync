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
 * retry-queue.c - Injection retry queue for handling temporary dnsmasq unavailability
 *
 * This module provides a bounded FIFO queue for retrying failed lease injections.
 * When dnsmasq is temporarily unavailable (e.g., during restart), lease operations
 * are queued and retried periodically until success or max attempts reached.
 *
 * Features:
 *   - Bounded queue (RETRY_QUEUE_SIZE entries) prevents unbounded memory growth
 *   - Queue overflow drops oldest entry with warning
 *   - Duplicate handling: newer timestamp updates in place, older skipped
 *   - Both ADD and DELETE operations supported
 *   - Configurable retry interval and max attempts
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "retry-queue.h"

extern struct daemon_state *g_state;

/* External injection functions from ubus-handler.c */
extern int ubus_handler_inject_lease(struct lease_entry *entry);
extern int ubus_handler_delete_lease(const char *ip);

int retry_queue_init(struct retry_queue *queue)
{
  if (!queue)
    return -1;

  queue->head = NULL;
  queue->tail = NULL;
  queue->count = 0;

  return 0;
}

void retry_queue_cleanup(struct retry_queue *queue)
{
  struct retry_entry *entry;

  if (!queue)
    return;

  entry = queue->head;
  while (entry)
    {
      struct retry_entry *next = entry->next;
      free(entry);
      entry = next;
    }

  queue->head = NULL;
  queue->tail = NULL;
  queue->count = 0;
}

int retry_queue_add(struct retry_queue *queue, struct lease_entry *entry, uint8_t action)
{
  time_t now;
  struct retry_entry *existing;
  struct retry_entry *new_entry;

  if (!queue || !entry || !g_state)
    return -1;

  now = time(NULL);

  /* Search for existing entry with same IP */
  existing = queue->head;
  while (existing)
    {
      if (strcmp(existing->lease.ip_str, entry->ip_str) == 0)
        {
          /* Found existing entry for this IP */
          if (entry->timestamp_ms > existing->lease.timestamp_ms)
            {
              /* Newer timestamp: update in place */
              memcpy(&existing->lease, entry, sizeof(struct lease_entry));
              existing->lease.next = NULL;  /* Clear collision chain pointer */
              existing->action = action;
              existing->last_attempt = now;
              log_debug("Updated retry queue entry for %s (newer timestamp)", entry->ip_str);
            }
          else
            {
              /* Older or same timestamp: skip */
              log_debug("Skipping retry queue update for %s (same or older timestamp)", entry->ip_str);
            }
          return 0;
        }
      existing = existing->next;
    }

  /* Check if queue is full */
  if (queue->count >= RETRY_QUEUE_SIZE)
    {
      /* Remove oldest (head) entry */
      struct retry_entry *oldest = queue->head;
      queue->head = oldest->next;
      if (queue->head == NULL)
        {
          queue->tail = NULL;
        }
      queue->count--;
      log_warning("Retry queue full, dropping oldest entry: %s", oldest->lease.ip_str);
      free(oldest);
      g_state->total_injection_drops++;
    }

  /* Allocate new entry */
  new_entry = calloc(1, sizeof(struct retry_entry));
  if (!new_entry)
    {
      log_error("Failed to allocate retry queue entry");
      return -1;
    }

  /* Copy lease data */
  memcpy(&new_entry->lease, entry, sizeof(struct lease_entry));
  new_entry->lease.next = NULL;  /* Clear collision chain pointer */
  new_entry->action = action;
  new_entry->retry_count = 0;
  new_entry->first_queued = now;
  new_entry->last_attempt = now;
  new_entry->next = NULL;

  /* Add to tail of queue */
  if (queue->tail)
    {
      queue->tail->next = new_entry;
    }
  else
    {
      queue->head = new_entry;
    }
  queue->tail = new_entry;
  queue->count++;

  log_info("Queued %s for retry: %s (queue size: %d)",
           action == RETRY_ACTION_ADD ? "injection" : "deletion",
           entry->ip_str, queue->count);

  return 0;
}

void retry_queue_process(void)
{
  struct retry_queue *queue;
  time_t now;
  struct retry_entry *prev = NULL;
  struct retry_entry *entry;

  if (!g_state)
    return;

  queue = &g_state->retry_queue;
  if (queue->count == 0)
    return;

  now = time(NULL);
  entry = queue->head;

  while (entry)
    {
      struct retry_entry *next = entry->next;
      int result;
      bool remove_entry;

      /* Skip if not enough time has passed since last attempt */
      if ((now - entry->last_attempt) < RETRY_INTERVAL_SECONDS)
        {
          prev = entry;
          entry = next;
          continue;
        }

      /* Increment retry count and update last attempt time */
      entry->retry_count++;
      entry->last_attempt = now;
      g_state->total_injection_retries++;

      /* Attempt injection */
      if (entry->action == RETRY_ACTION_ADD)
        {
          result = ubus_handler_inject_lease(&entry->lease);
        }
      else
        {
          result = ubus_handler_delete_lease(entry->lease.ip_str);
        }

      remove_entry = false;
      if (result == 0)
        {
          /* Success: remove from queue */
          log_info("Retry successful for %s after %d attempt(s)",
                   entry->lease.ip_str, entry->retry_count);
          remove_entry = true;
        }
      else if (entry->retry_count >= RETRY_MAX_ATTEMPTS)
        {
          /* Max retries exceeded: remove and log warning */
          log_warning("Retry failed for %s after %d attempts, giving up",
                      entry->lease.ip_str, entry->retry_count);
          g_state->total_injection_drops++;
          remove_entry = true;
        }
      else
        {
          /* Failed but retries remaining */
          log_debug("Retry %d/%d failed for %s",
                    entry->retry_count, RETRY_MAX_ATTEMPTS, entry->lease.ip_str);
        }

      if (remove_entry)
        {
          /* Remove entry from queue */
          if (prev)
            {
              prev->next = next;
            }
          else
            {
              queue->head = next;
            }
          if (entry == queue->tail)
            {
              queue->tail = prev;
            }
          queue->count--;
          free(entry);
        }
      else
        {
          prev = entry;
        }

      entry = next;
    }
}

int retry_queue_count(void)
{
  if (!g_state)
    return 0;

  return g_state->retry_queue.count;
}
