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
 * retry-queue.h - Injection retry queue public API
 *
 * This module provides a bounded FIFO queue for retrying failed lease injections.
 * See retry-queue.c for implementation details.
 */

#ifndef LEASE_SYNC_RETRY_QUEUE_H
#define LEASE_SYNC_RETRY_QUEUE_H

#include "common.h"

/*
 * Initialize an empty retry queue
 *
 * @param queue  Pointer to struct retry_queue to initialize
 * @return 0 on success, -1 on error (NULL queue)
 */
int retry_queue_init(struct retry_queue *queue);

/*
 * Free all entries in the retry queue
 *
 * @param queue  Pointer to struct retry_queue to cleanup
 */
void retry_queue_cleanup(struct retry_queue *queue);

/*
 * Add an entry to the retry queue
 *
 * Behavior:
 * - If entry with same IP exists and has newer timestamp: update in place
 * - If entry with same IP exists and has older timestamp: skip (don't downgrade)
 * - If queue is full: remove oldest, log warning, increment drops
 *
 * @param queue   Pointer to struct retry_queue
 * @param entry   Pointer to struct lease_entry to queue
 * @param action  RETRY_ACTION_ADD or RETRY_ACTION_DELETE
 * @return 0 on success, -1 on error
 */
int retry_queue_add(struct retry_queue *queue, struct lease_entry *entry, uint8_t action);

/*
 * Process pending retry queue entries
 *
 * For each entry where enough time has passed since last attempt:
 * - Attempts injection (ADD or DELETE based on action)
 * - Removes successful entries
 * - Removes entries that have exceeded max retries
 *
 * Should be called periodically from main loop.
 */
void retry_queue_process(void);

/*
 * Return current retry queue size
 *
 * @return Number of entries in the queue, 0 if g_state is NULL
 */
int retry_queue_count(void);

#endif /* LEASE_SYNC_RETRY_QUEUE_H */
