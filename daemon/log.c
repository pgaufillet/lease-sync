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
 * log.c - Centralized logging module for lease-sync daemon
 *
 * Follows OpenWrt conventions:
 * - Plain messages without [LEVEL] prefixes (syslog adds facility.level)
 * - Uses LOG_PERROR for foreground mode (automatic stderr duplication)
 * - Single log level for filtering
 */

#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>

/* Global logging state */
static int g_log_level = LOG_LEVEL_INFO;
static enum log_mode g_log_mode = LOG_MODE_DAEMON;
static bool g_initialized = false;

void log_init(const char *ident, enum log_mode mode, int level)
{
  int flags = LOG_PID;

  g_log_mode = mode;
  g_log_level = level;

  if (mode == LOG_MODE_FOREGROUND)
    {
      flags |= LOG_PERROR;  /* Also output to stderr */
    }
  openlog(ident, flags, LOG_DAEMON);

  g_initialized = true;
}

void log_cleanup(void)
{
  closelog();
  g_initialized = false;
}

void log_set_level(int level)
{
  if (level >= LOG_LEVEL_ERROR && level <= LOG_LEVEL_DEBUG)
    {
      g_log_level = level;
    }
}

int log_get_level(void)
{
  return g_log_level;
}

/*
 * Map our log levels to syslog priorities
 */
static int level_to_syslog(int level)
{
  switch (level)
    {
    case LOG_LEVEL_ERROR:   return LOG_ERR;
    case LOG_LEVEL_WARNING: return LOG_WARNING;
    case LOG_LEVEL_INFO:    return LOG_INFO;
    case LOG_LEVEL_DEBUG:   return LOG_DEBUG;
    default:                return LOG_INFO;
    }
}

void log_write(int level, const char *fmt, ...)
{
  va_list args;

  /* Always log errors, otherwise check level */
  if (level != LOG_LEVEL_ERROR && level > g_log_level)
    {
      return;
    }

  va_start(args, fmt);

  if (!g_initialized)
    {
      /* Not initialized: stderr only with newline */
      vfprintf(stderr, fmt, args);
      fprintf(stderr, "\n");
    }
  else
    {
      /* Use syslog (LOG_PERROR handles stderr in foreground mode) */
      vsyslog(level_to_syslog(level), fmt, args);
    }

  va_end(args);
}
