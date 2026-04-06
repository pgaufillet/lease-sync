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
 * test_framework.c - Shared test counter definitions and summary function
 */

#include "test_framework.h"

/* Test counter definitions */
int tests_passed = 0;
int tests_failed = 0;

int print_test_summary(void)
{
    int total = tests_passed + tests_failed;

    printf("\n=========================================\n");
    printf("Results: %d passed, %d failed (total: %d)\n",
           tests_passed, tests_failed, total);
    printf("=========================================\n");

    if (tests_failed > 0) {
        printf(RED "SOME TESTS FAILED" NC "\n");
        return 1;
    }

    printf(GREEN "ALL TESTS PASSED" NC "\n");
    return 0;
}
