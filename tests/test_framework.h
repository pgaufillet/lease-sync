/*
 * Copyright (C) 2025-2026 Pierre Gaufillet <pierre.gaufillet@bergamote.eu>
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
 * test_framework.h - Shared test macros and declarations for lease-sync unit tests
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>

/* ANSI colors for output */
#define GREEN "\033[0;32m"
#define RED   "\033[0;31m"
#define NC    "\033[0m"

/* Test counters (defined in test_framework.c) */
extern int tests_passed;
extern int tests_failed;

/* Basic test result macros */
#define TEST_PASS(name) do { \
    printf(GREEN "PASS" NC ": %s\n", name); \
    tests_passed++; \
} while(0)

#define TEST_FAIL(name, reason) do { \
    printf(RED "FAIL" NC ": %s - %s\n", name, reason); \
    tests_failed++; \
} while(0)

/* Typed assertion macros */
#define ASSERT_EQ_INT(name, actual, expected) do { \
    int _actual = (int)(actual); \
    int _expected = (int)(expected); \
    if (_actual == _expected) { \
        TEST_PASS(name); \
    } else { \
        char _msg[128]; \
        snprintf(_msg, sizeof(_msg), "expected %d, got %d", \
                 _expected, _actual); \
        TEST_FAIL(name, _msg); \
    } \
} while(0)

#define ASSERT_EQ_STR(name, actual, expected) do { \
    const char *_actual = (actual); \
    const char *_expected = (expected); \
    if (_actual != NULL && _expected != NULL && \
        strcmp(_actual, _expected) == 0) { \
        TEST_PASS(name); \
    } else { \
        char _msg[256]; \
        snprintf(_msg, sizeof(_msg), "expected '%s', got '%s'", \
                 _expected ? _expected : "NULL", \
                 _actual ? _actual : "NULL"); \
        TEST_FAIL(name, _msg); \
    } \
} while(0)

#define ASSERT_TRUE(name, condition) do { \
    if (condition) { \
        TEST_PASS(name); \
    } else { \
        TEST_FAIL(name, "condition is false"); \
    } \
} while(0)

#define ASSERT_NULL(name, ptr) do { \
    if ((ptr) == NULL) { \
        TEST_PASS(name); \
    } else { \
        TEST_FAIL(name, "expected NULL, got non-NULL"); \
    } \
} while(0)

#define ASSERT_NOT_NULL(name, ptr) do { \
    if ((ptr) != NULL) { \
        TEST_PASS(name); \
    } else { \
        TEST_FAIL(name, "expected non-NULL, got NULL"); \
    } \
} while(0)

/* Print test summary and return exit code (0=all passed, 1=failures) */
int print_test_summary(void);

#endif /* TEST_FRAMEWORK_H */
