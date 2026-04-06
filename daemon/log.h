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
#ifndef LEASE_SYNC_LOG_H
#define LEASE_SYNC_LOG_H

#include <stdbool.h>

/*
 * Log levels - matches syslog priorities
 * Lower values = higher priority (errors always logged)
 */
#define LOG_LEVEL_ERROR   0
#define LOG_LEVEL_WARNING 1
#define LOG_LEVEL_INFO    2
#define LOG_LEVEL_DEBUG   3

/*
 * Log modes - determines output destinations
 */
enum log_mode
{
  LOG_MODE_DAEMON,      /* syslog only (normal daemon operation) */
  LOG_MODE_FOREGROUND   /* syslog + stderr (development/debugging) */
};

/*
 * Initialize logging subsystem
 *
 * @param ident   Program identifier for syslog (e.g., "lease-sync")
 * @param mode    Output mode (daemon or foreground)
 * @param level   Maximum log level to output (0-3)
 */
void log_init(const char *ident, enum log_mode mode, int level);

/*
 * Cleanup logging subsystem
 * Closes syslog connection
 */
void log_cleanup(void);

/*
 * Set log level (can be called after init)
 *
 * @param level   Maximum log level to output (0-3)
 */
void log_set_level(int level);

/*
 * Get current log level
 */
int log_get_level(void);

/*
 * Internal logging function - use macros below instead
 *
 * @param level   Log level for this message
 * @param fmt     printf-style format string
 * @param ...     Format arguments
 */
void log_write(int level, const char *fmt, ...)
__attribute__((format(printf, 2, 3)));

/*
 * Logging macros - use these for all logging
 *
 * Messages are plain text (no level prefix) as per OpenWrt conventions.
 * The syslog facility.level (e.g., daemon.info) is added automatically.
 *
 * Examples:
 *   log_error("Failed to connect: %s", strerror(errno));
 *   log_warning("Peer %s not responding", peer_addr);
 *   log_info("Daemon started, listening on port %d", port);
 *   log_debug("Received message type %d", msg_type);
 */
#define log_error(fmt, ...) \
    log_write(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)

#define log_warning(fmt, ...) \
    log_write(LOG_LEVEL_WARNING, fmt, ##__VA_ARGS__)

#define log_info(fmt, ...) \
    log_write(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)

#define log_debug(fmt, ...) \
    log_write(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)

#endif /* LEASE_SYNC_LOG_H */
