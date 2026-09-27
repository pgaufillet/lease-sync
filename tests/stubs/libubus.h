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
 * Minimal libubus declarations for host unit tests. Tests provide the
 * definitions (typically a fake dnsmasq behind ubus_invoke).
 */

#ifndef TEST_STUB_LIBUBUS_H
#define TEST_STUB_LIBUBUS_H

#include <stdint.h>
#include "libubox/blobmsg.h"

enum ubus_msg_status
{
    UBUS_STATUS_OK,
    UBUS_STATUS_INVALID_COMMAND,
    UBUS_STATUS_INVALID_ARGUMENT,
    UBUS_STATUS_METHOD_NOT_FOUND,
    UBUS_STATUS_NOT_FOUND,
    UBUS_STATUS_NO_DATA,
    UBUS_STATUS_PERMISSION_DENIED,
    UBUS_STATUS_TIMEOUT,
    UBUS_STATUS_NOT_SUPPORTED,
    UBUS_STATUS_UNKNOWN_ERROR,
    UBUS_STATUS_CONNECTION_FAILED,
};

struct ubus_context;
struct ubus_event_handler;
struct ubus_request;

typedef void (*ubus_data_handler_t)(struct ubus_request *req, int type,
                                    struct blob_attr *msg);
typedef void (*ubus_event_handler_t)(struct ubus_context *ctx,
                                     struct ubus_event_handler *ev,
                                     const char *type, struct blob_attr *msg);

struct ubus_event_handler
{
    ubus_event_handler_t cb;
};

struct ubus_request
{
    void *priv;
};

struct ubus_context *ubus_connect(const char *path);
void ubus_free(struct ubus_context *ctx);
void ubus_add_uloop(struct ubus_context *ctx);
const char *ubus_strerror(int error);
int ubus_register_event_handler(struct ubus_context *ctx,
                                struct ubus_event_handler *ev,
                                const char *pattern);
int ubus_lookup_id(struct ubus_context *ctx, const char *path, uint32_t *id);
int ubus_invoke(struct ubus_context *ctx, uint32_t obj, const char *method,
                struct blob_attr *msg, ubus_data_handler_t cb, void *priv,
                int timeout);

#endif /* TEST_STUB_LIBUBUS_H */
