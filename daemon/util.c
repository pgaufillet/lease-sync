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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <ctype.h>

#include "common.h"

extern struct daemon_state *g_state;

uint64_t get_timestamp_ms(void)
{
  struct timeval tv;

  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

uint32_t hash_string(const char *str)
{
  uint32_t hash = 5381;
  int c;

  while ((c = *str++))
    hash = ((hash << 5) + hash) + c;

  return hash;
}

int parse_ip_address(const char *str, int *af_family, void *addr)
{
  if (!str || !af_family || !addr)
    return -1;

  /* Try IPv4 first */
  if (inet_pton(AF_INET, str, addr) == 1)
    {
      *af_family = AF_INET;
      return 0;
    }

  /* Try IPv6 */
  if (inet_pton(AF_INET6, str, addr) == 1)
    {
      *af_family = AF_INET6;
      return 0;
    }

  return -1;  /* Invalid */
}

const char *format_mac(const unsigned char *mac, int len, char *buf)
{
  int i, pos;

  if (!mac || !buf || len <= 0)
    return NULL;

  if (len == 6)
    {
      /* Standard Ethernet MAC */
      snprintf(buf, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
  else
    {
      /* Generic hex dump */
      pos = 0;
      for (i = 0; i < len && pos < 50; i++)
        {
          pos += snprintf(buf + pos, 50 - pos, "%02x", mac[i]);
          if (i < len - 1 && pos < 49)
            buf[pos++] = ':';
        }
      buf[pos] = '\0';
    }

  return buf;
}

int parse_mac(const char *str, unsigned char *mac, int *len)
{
  int count = 0;
  const char *p;
  unsigned int byte;

  if (!str || !mac || !len)
    return -1;

  /* Support formats: aa:bb:cc:dd:ee:ff or aabbccddeeff */
  p = str;

  while (*p && count < 32)
    {
      if (sscanf(p, "%2x", &byte) != 1)
        break;

      mac[count++] = (unsigned char)byte;

      /* Skip hex digits */
      p += 2;

      /* Skip separator if present */
      if (*p == ':' || *p == '-')
        p++;
    }

  *len = count;
  return (count > 0) ? 0 : -1;
}

bool is_seen_message(uint16_t sequence, const char *node_id)
{
  time_t now;
  int i;

  if (!g_state || !node_id)
    return false;

  now = time(NULL);

  for (i = 0; i < SEEN_RING_SIZE; i++)
    {
      if (g_state->seen_messages[i].sequence == sequence &&
          strcmp(g_state->seen_messages[i].node_id, node_id) == 0 &&
          (now - g_state->seen_messages[i].timestamp) < 60)
        {
          return true;  /* Already seen recently */
        }
    }

  return false;
}

void mark_message_seen(uint16_t sequence, const char *node_id)
{
  if (!g_state || !node_id)
    return;

  g_state->seen_messages[g_state->seen_index].sequence = sequence;
  strncpy(g_state->seen_messages[g_state->seen_index].node_id, node_id, MAX_NODE_ID_LEN - 1);
  g_state->seen_messages[g_state->seen_index].node_id[MAX_NODE_ID_LEN - 1] = '\0';
  g_state->seen_messages[g_state->seen_index].timestamp = time(NULL);

  g_state->seen_index = (g_state->seen_index + 1) % SEEN_RING_SIZE;
}

char *safe_strncpy(char *dest, const char *src, size_t n)
{
  if (!dest || !src || n == 0)
    return dest;

  strncpy(dest, src, n - 1);
  dest[n - 1] = '\0';
  return dest;
}

char *trim_whitespace(char *str)
{
  char *end;

  /* Trim leading space */
  while (isspace((unsigned char)*str)) str++;

  if (*str == 0)  /* All spaces? */
    return str;

  /* Trim trailing space */
  end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end)) end--;

  /* Write new null terminator */
  end[1] = '\0';

  return str;
}

int parse_int(const char *str, int *value)
{
  char *endptr;
  long val;

  if (!str || !value)
    return -1;

  errno = 0;
  val = strtol(str, &endptr, 10);

  if (errno != 0 || endptr == str || *endptr != '\0')
    return -1;

  *value = (int)val;
  return 0;
}

const char *format_timestamp(time_t t, char *buf, size_t size)
{
  struct tm *tm_info;

  tm_info = localtime(&t);
  strftime(buf, size, "%Y-%m-%d %H:%M:%S", tm_info);
  return buf;
}

double time_diff_seconds(struct timeval *start, struct timeval *end)
{
  return (end->tv_sec - start->tv_sec) +
         (end->tv_usec - start->tv_usec) / 1000000.0;
}

const char *format_bytes(uint64_t bytes, char *buf, size_t size)
{
  const char *units[] = {"B", "KB", "MB", "GB"};
  int unit = 0;
  double value = (double)bytes;

  while (value >= 1024.0 && unit < 3)
    {
      value /= 1024.0;
      unit++;
    }

  snprintf(buf, size, "%.2f %s", value, units[unit]);
  return buf;
}
