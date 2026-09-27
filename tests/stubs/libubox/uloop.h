/*
 * Copyright (C) 2026 Pierre Gaufillet <pierre.gaufillet@bergamote.eu>
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
 * Minimal uloop declarations for host unit tests. Tests provide the
 * definitions and drive callbacks directly instead of running a loop.
 */

#ifndef TEST_STUB_ULOOP_H
#define TEST_STUB_ULOOP_H

#define ULOOP_READ (1 << 0)

struct uloop_fd;
struct uloop_timeout;

typedef void (*uloop_fd_handler)(struct uloop_fd *u, unsigned int events);
typedef void (*uloop_timeout_handler)(struct uloop_timeout *t);

struct uloop_fd
{
    uloop_fd_handler cb;
    int fd;
};

struct uloop_timeout
{
    uloop_timeout_handler cb;
};

int uloop_init(void);
int uloop_run(void);
void uloop_done(void);
int uloop_fd_add(struct uloop_fd *sock, unsigned int flags);
int uloop_timeout_set(struct uloop_timeout *timeout, int msecs);

#endif /* TEST_STUB_ULOOP_H */
