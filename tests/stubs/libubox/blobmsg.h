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
 * Minimal blobmsg stand-in for host unit tests.
 *
 * Only the subset used by the daemon is provided. A blob is a plain tree
 * of named attributes instead of libubox's packed binary format, so tests
 * can build ubus messages and replies without libubox.
 */

#ifndef TEST_STUB_BLOBMSG_H
#define TEST_STUB_BLOBMSG_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

enum
{
    BLOBMSG_TYPE_UNSPEC,
    BLOBMSG_TYPE_ARRAY,
    BLOBMSG_TYPE_TABLE,
    BLOBMSG_TYPE_STRING,
    BLOBMSG_TYPE_INT64,
    BLOBMSG_TYPE_INT32,
    BLOBMSG_TYPE_INT16,
    BLOBMSG_TYPE_INT8,
    BLOBMSG_TYPE_BOOL = BLOBMSG_TYPE_INT8,
};

struct blob_attr
{
    const char *name;
    int type;
    const char *str;
    uint32_t u32;
    struct blob_attr *children;     /* TABLE / ARRAY members */
    int n_children;
};

struct blobmsg_policy
{
    const char *name;
    int type;
};

#define STUB_BLOB_BUF_FIELDS 16
#define STUB_BLOB_BUF_STRLEN 800

struct blob_buf
{
    struct blob_attr *head;
    struct blob_attr root;
    struct blob_attr fields[STUB_BLOB_BUF_FIELDS];
    char strs[STUB_BLOB_BUF_FIELDS][STUB_BLOB_BUF_STRLEN];
};

static inline void *blob_data(const struct blob_attr *attr)
{
    return (void *)attr;
}

static inline unsigned int blob_len(const struct blob_attr *attr)
{
    (void)attr;
    return 0;
}

static inline void *blobmsg_data(const struct blob_attr *attr)
{
    return (void *)attr;
}

static inline unsigned int blobmsg_len(const struct blob_attr *attr)
{
    (void)attr;
    return 0;
}

static inline int blobmsg_type(const struct blob_attr *attr)
{
    return attr->type;
}

static inline char *blobmsg_get_string(struct blob_attr *attr)
{
    return (char *)attr->str;
}

static inline uint32_t blobmsg_get_u32(struct blob_attr *attr)
{
    return attr->u32;
}

static inline bool blobmsg_get_bool(struct blob_attr *attr)
{
    return attr->u32 != 0;
}

/* data is the TABLE attribute returned by blob_data()/blobmsg_data() */
static inline int blobmsg_parse(const struct blobmsg_policy *policy, int policy_len,
                                struct blob_attr **tb, void *data, unsigned int len)
{
    const struct blob_attr *table = data;
    int i, j;

    (void)len;
    memset(tb, 0, policy_len * sizeof(*tb));
    if (!table)
        return 0;

    for (i = 0; i < policy_len; i++)
        for (j = 0; j < table->n_children; j++)
            if (table->children[j].name &&
                strcmp(table->children[j].name, policy[i].name) == 0 &&
                table->children[j].type == policy[i].type)
                tb[i] = &table->children[j];

    return 0;
}

#define blobmsg_for_each_attr(pos, attr, rem) \
    for ((rem) = 0; (attr) && (rem) < (attr)->n_children && \
         (((pos) = &(attr)->children[(rem)]), 1); (rem)++)

static inline int blob_buf_init(struct blob_buf *buf, int id)
{
    (void)id;
    memset(buf, 0, sizeof(*buf));
    buf->root.type = BLOBMSG_TYPE_TABLE;
    buf->root.children = buf->fields;
    buf->head = &buf->root;
    return 0;
}

static inline void blob_buf_free(struct blob_buf *buf)
{
    buf->head = NULL;
}

static inline struct blob_attr *stub_blob_buf_next(struct blob_buf *buf,
                                                   const char *name, int type)
{
    struct blob_attr *attr;

    if (buf->root.n_children >= STUB_BLOB_BUF_FIELDS)
        return NULL;
    attr = &buf->fields[buf->root.n_children];
    attr->name = name;
    attr->type = type;
    attr->str = buf->strs[buf->root.n_children];
    buf->root.n_children++;
    return attr;
}

static inline int blobmsg_add_string(struct blob_buf *buf, const char *name,
                                     const char *str)
{
    struct blob_attr *attr = stub_blob_buf_next(buf, name, BLOBMSG_TYPE_STRING);

    if (!attr)
        return -1;
    strncpy((char *)attr->str, str, STUB_BLOB_BUF_STRLEN - 1);
    return 0;
}

static inline int blobmsg_add_u32(struct blob_buf *buf, const char *name, uint32_t val)
{
    struct blob_attr *attr = stub_blob_buf_next(buf, name, BLOBMSG_TYPE_INT32);

    if (!attr)
        return -1;
    attr->u32 = val;
    return 0;
}

static inline int blobmsg_add_u8(struct blob_buf *buf, const char *name, uint8_t val)
{
    struct blob_attr *attr = stub_blob_buf_next(buf, name, BLOBMSG_TYPE_INT8);

    if (!attr)
        return -1;
    attr->u32 = val;
    return 0;
}

#endif /* TEST_STUB_BLOBMSG_H */
