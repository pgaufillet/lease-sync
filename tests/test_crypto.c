/* test_crypto.c
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
 * lease-sync - DHCP Lease Synchronization Daemon
 * test_crypto.c - Unit tests for AES-256-GCM encryption module
 *
 * Build: gcc -o test_crypto test_crypto.c ../daemon/crypto.c -lssl -lcrypto
 * Run: ./test_crypto
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* Include the crypto header */
#include "../daemon/crypto.h"

#include "test_framework.h"

/* Valid test key (64 hex chars = 32 bytes = 256 bits) */
static const char *VALID_KEY = "8b2b64411001592abf237c204845e610e5a743a1472822b3d2b86069ec3658d3";
static const char *VALID_KEY_2 = "0000000000000000000000000000000000000000000000000000000000000001";

/* ============================================
 * Test: Key Validation
 * ============================================ */
void test_key_validation(void)
{
    printf("\n=== Test: Key Validation ===\n");

    /* Test valid key */
    if (security_validate_key(VALID_KEY)) {
        TEST_PASS("Valid 64-char hex key accepted");
    } else {
        TEST_FAIL("Valid 64-char hex key accepted", "rejected valid key");
    }

    /* Test NULL key */
    if (!security_validate_key(NULL)) {
        TEST_PASS("NULL key rejected");
    } else {
        TEST_FAIL("NULL key rejected", "accepted NULL");
    }

    /* Test empty key */
    if (!security_validate_key("")) {
        TEST_PASS("Empty key rejected");
    } else {
        TEST_FAIL("Empty key rejected", "accepted empty string");
    }

    /* Test short key (63 chars) */
    if (!security_validate_key("8b2b64411001592abf237c204845e610e5a743a1472822b3d2b86069ec3658d")) {
        TEST_PASS("Short key (63 chars) rejected");
    } else {
        TEST_FAIL("Short key (63 chars) rejected", "accepted short key");
    }

    /* Test long key (65 chars) */
    if (!security_validate_key("8b2b64411001592abf237c204845e610e5a743a1472822b3d2b86069ec3658d3a")) {
        TEST_PASS("Long key (65 chars) rejected");
    } else {
        TEST_FAIL("Long key (65 chars) rejected", "accepted long key");
    }

    /* Test non-hex characters */
    if (!security_validate_key("8b2b64411001592abf237c204845e610e5a743a1472822b3d2b86069ec3658gx")) {
        TEST_PASS("Non-hex characters rejected");
    } else {
        TEST_FAIL("Non-hex characters rejected", "accepted non-hex");
    }

    /* Test uppercase hex (should be accepted) */
    if (security_validate_key("8B2B64411001592ABF237C204845E610E5A743A1472822B3D2B86069EC3658D3")) {
        TEST_PASS("Uppercase hex key accepted");
    } else {
        TEST_FAIL("Uppercase hex key accepted", "rejected uppercase");
    }
}

/* ============================================
 * Test: Security Context Creation
 * ============================================ */
void test_context_creation(void)
{
    printf("\n=== Test: Security Context Creation ===\n");

    /* Test valid context creation */
    struct security_context *ctx = security_context_new(VALID_KEY);
    if (ctx != NULL) {
        TEST_PASS("Context created with valid key");
        security_context_free(ctx);
    } else {
        TEST_FAIL("Context created with valid key", "returned NULL");
    }

    /* Test NULL key */
    ctx = security_context_new(NULL);
    if (ctx == NULL) {
        TEST_PASS("Context creation with NULL key returns NULL");
    } else {
        TEST_FAIL("Context creation with NULL key returns NULL", "returned non-NULL");
        security_context_free(ctx);
    }

    /* Test invalid key (wrong length) */
    ctx = security_context_new("invalid");
    if (ctx == NULL) {
        TEST_PASS("Context creation with invalid key returns NULL");
    } else {
        TEST_FAIL("Context creation with invalid key returns NULL", "returned non-NULL");
        security_context_free(ctx);
    }

    /* Test context free with NULL (should not crash) */
    security_context_free(NULL);
    TEST_PASS("security_context_free(NULL) does not crash");
}

/* ============================================
 * Test: Encrypt/Decrypt Round-Trip
 * ============================================ */
void test_encrypt_decrypt_roundtrip(void)
{
    printf("\n=== Test: Encrypt/Decrypt Round-Trip ===\n");

    struct security_context *ctx = security_context_new(VALID_KEY);
    if (!ctx) {
        TEST_FAIL("Encrypt/decrypt round-trip", "failed to create context");
        return;
    }

    /* Test data - simulates a sync message */
    const char *plaintext = "Hello, lease-sync encryption!";
    size_t plaintext_len = strlen(plaintext) + 1;  /* Include null terminator */

    /* Allocate buffers */
    uint8_t ciphertext[plaintext_len + CRYPTO_OVERHEAD + 16];  /* Extra space for safety */
    uint8_t decrypted[plaintext_len + 16];
    size_t ciphertext_len = 0;
    size_t decrypted_len = 0;

    /* Encrypt */
    int ret = security_encrypt(ctx, (const uint8_t *)plaintext, plaintext_len,
                               ciphertext, &ciphertext_len);
    if (ret == CRYPTO_OK) {
        TEST_PASS("Encryption succeeds");
    } else {
        TEST_FAIL("Encryption succeeds", "encryption failed");
        security_context_free(ctx);
        return;
    }

    /* Verify ciphertext length */
    if (ciphertext_len == plaintext_len + CRYPTO_OVERHEAD) {
        TEST_PASS("Ciphertext has expected length (plaintext + overhead)");
    } else {
        char msg[100];
        snprintf(msg, sizeof(msg), "expected %zu, got %zu",
                 plaintext_len + CRYPTO_OVERHEAD, ciphertext_len);
        TEST_FAIL("Ciphertext has expected length", msg);
    }

    /* Decrypt */
    ret = security_decrypt(ctx, ciphertext, ciphertext_len,
                           decrypted, &decrypted_len);
    if (ret == CRYPTO_OK) {
        TEST_PASS("Decryption succeeds");
    } else {
        TEST_FAIL("Decryption succeeds", "decryption failed");
        security_context_free(ctx);
        return;
    }

    /* Verify decrypted length */
    if (decrypted_len == plaintext_len) {
        TEST_PASS("Decrypted length matches plaintext length");
    } else {
        char msg[100];
        snprintf(msg, sizeof(msg), "expected %zu, got %zu", plaintext_len, decrypted_len);
        TEST_FAIL("Decrypted length matches plaintext length", msg);
    }

    /* Verify content */
    if (memcmp(decrypted, plaintext, plaintext_len) == 0) {
        TEST_PASS("Decrypted content matches original plaintext");
    } else {
        TEST_FAIL("Decrypted content matches original plaintext", "content mismatch");
    }

    security_context_free(ctx);
}

/* ============================================
 * Test: Tamper Detection
 * ============================================ */
void test_tamper_detection(void)
{
    printf("\n=== Test: Tamper Detection ===\n");

    struct security_context *ctx = security_context_new(VALID_KEY);
    if (!ctx) {
        TEST_FAIL("Tamper detection setup", "failed to create context");
        return;
    }

    const char *plaintext = "This message must not be tampered with!";
    size_t plaintext_len = strlen(plaintext) + 1;

    uint8_t ciphertext[plaintext_len + CRYPTO_OVERHEAD + 16];
    uint8_t decrypted[plaintext_len + 16];
    size_t ciphertext_len = 0;
    size_t decrypted_len = 0;

    /* Encrypt */
    int ret = security_encrypt(ctx, (const uint8_t *)plaintext, plaintext_len,
                               ciphertext, &ciphertext_len);
    if (ret != CRYPTO_OK) {
        TEST_FAIL("Tamper detection setup", "encryption failed");
        security_context_free(ctx);
        return;
    }

    /* Test 1: Tamper with ciphertext body (middle of encrypted data) */
    uint8_t tampered[ciphertext_len];
    memcpy(tampered, ciphertext, ciphertext_len);
    /* Flip a byte in the middle of the ciphertext (after nonce, before tag) */
    size_t tamper_pos = CRYPTO_NONCE_SIZE + (ciphertext_len - CRYPTO_OVERHEAD) / 2;
    tampered[tamper_pos] ^= 0xFF;

    ret = security_decrypt(ctx, tampered, ciphertext_len, decrypted, &decrypted_len);
    if (ret == CRYPTO_ERROR_AUTH) {
        TEST_PASS("Tampered ciphertext body detected (auth failed)");
    } else if (ret != CRYPTO_OK) {
        TEST_PASS("Tampered ciphertext body detected (other error)");
    } else {
        TEST_FAIL("Tampered ciphertext body detected", "decryption succeeded on tampered data");
    }

    /* Test 2: Tamper with authentication tag */
    memcpy(tampered, ciphertext, ciphertext_len);
    /* Flip a byte in the auth tag (last 16 bytes) */
    tampered[ciphertext_len - 1] ^= 0xFF;

    ret = security_decrypt(ctx, tampered, ciphertext_len, decrypted, &decrypted_len);
    if (ret == CRYPTO_ERROR_AUTH) {
        TEST_PASS("Tampered authentication tag detected");
    } else if (ret != CRYPTO_OK) {
        TEST_PASS("Tampered authentication tag detected (other error)");
    } else {
        TEST_FAIL("Tampered authentication tag detected", "decryption succeeded on tampered tag");
    }

    /* Test 3: Tamper with nonce */
    memcpy(tampered, ciphertext, ciphertext_len);
    /* Flip a byte in the nonce (first 12 bytes) */
    tampered[5] ^= 0xFF;

    ret = security_decrypt(ctx, tampered, ciphertext_len, decrypted, &decrypted_len);
    if (ret == CRYPTO_ERROR_AUTH) {
        TEST_PASS("Tampered nonce detected (auth failed)");
    } else if (ret != CRYPTO_OK) {
        TEST_PASS("Tampered nonce detected (other error)");
    } else {
        TEST_FAIL("Tampered nonce detected", "decryption succeeded on tampered nonce");
    }

    security_context_free(ctx);
}

/* ============================================
 * Test: Wrong Key Detection
 * ============================================ */
void test_wrong_key_detection(void)
{
    printf("\n=== Test: Wrong Key Detection ===\n");

    struct security_context *ctx1 = security_context_new(VALID_KEY);
    struct security_context *ctx2 = security_context_new(VALID_KEY_2);

    if (!ctx1 || !ctx2) {
        TEST_FAIL("Wrong key detection setup", "failed to create contexts");
        security_context_free(ctx1);
        security_context_free(ctx2);
        return;
    }

    const char *plaintext = "Encrypted with key 1, decrypted with key 2";
    size_t plaintext_len = strlen(plaintext) + 1;

    uint8_t ciphertext[plaintext_len + CRYPTO_OVERHEAD + 16];
    uint8_t decrypted[plaintext_len + 16];
    size_t ciphertext_len = 0;
    size_t decrypted_len = 0;

    /* Encrypt with key 1 */
    int ret = security_encrypt(ctx1, (const uint8_t *)plaintext, plaintext_len,
                               ciphertext, &ciphertext_len);
    if (ret != CRYPTO_OK) {
        TEST_FAIL("Wrong key detection setup", "encryption failed");
        security_context_free(ctx1);
        security_context_free(ctx2);
        return;
    }

    /* Try to decrypt with key 2 */
    ret = security_decrypt(ctx2, ciphertext, ciphertext_len, decrypted, &decrypted_len);
    if (ret == CRYPTO_ERROR_AUTH) {
        TEST_PASS("Wrong key detected (authentication failed)");
    } else if (ret != CRYPTO_OK) {
        TEST_PASS("Wrong key detected (other error)");
    } else {
        TEST_FAIL("Wrong key detected", "decryption succeeded with wrong key");
    }

    /* Verify correct key still works */
    ret = security_decrypt(ctx1, ciphertext, ciphertext_len, decrypted, &decrypted_len);
    if (ret == CRYPTO_OK && memcmp(decrypted, plaintext, plaintext_len) == 0) {
        TEST_PASS("Correct key still works after wrong key attempt");
    } else {
        TEST_FAIL("Correct key still works after wrong key attempt", "decryption failed");
    }

    security_context_free(ctx1);
    security_context_free(ctx2);
}

/* ============================================
 * Test: Key Generation
 * ============================================ */
void test_key_generation(void)
{
    printf("\n=== Test: Key Generation ===\n");

    char *key1 = security_generate_key();
    if (key1 != NULL) {
        TEST_PASS("Key generation returns non-NULL");
    } else {
        TEST_FAIL("Key generation returns non-NULL", "returned NULL");
        return;
    }

    /* Verify key length */
    if (strlen(key1) == CRYPTO_KEY_HEX_SIZE) {
        TEST_PASS("Generated key has correct length (64 chars)");
    } else {
        char msg[100];
        snprintf(msg, sizeof(msg), "expected 64, got %zu", strlen(key1));
        TEST_FAIL("Generated key has correct length", msg);
    }

    /* Verify key is valid hex */
    if (security_validate_key(key1)) {
        TEST_PASS("Generated key passes validation");
    } else {
        TEST_FAIL("Generated key passes validation", "validation failed");
    }

    /* Generate second key and verify they're different */
    char *key2 = security_generate_key();
    if (key2 != NULL && strcmp(key1, key2) != 0) {
        TEST_PASS("Two generated keys are different");
    } else if (key2 == NULL) {
        TEST_FAIL("Two generated keys are different", "second key generation failed");
    } else {
        TEST_FAIL("Two generated keys are different", "keys are identical (RNG issue?)");
    }

    /* Verify generated key can be used for encryption */
    struct security_context *ctx = security_context_new(key1);
    if (ctx != NULL) {
        const char *test_data = "Test encryption with generated key";
        uint8_t ciphertext[100];
        uint8_t decrypted[100];
        size_t ct_len, pt_len;

        int ret = security_encrypt(ctx, (const uint8_t *)test_data, strlen(test_data) + 1,
                                   ciphertext, &ct_len);
        if (ret == CRYPTO_OK) {
            ret = security_decrypt(ctx, ciphertext, ct_len, decrypted, &pt_len);
            if (ret == CRYPTO_OK && strcmp((char *)decrypted, test_data) == 0) {
                TEST_PASS("Generated key works for encryption/decryption");
            } else {
                TEST_FAIL("Generated key works for encryption/decryption", "decrypt failed");
            }
        } else {
            TEST_FAIL("Generated key works for encryption/decryption", "encrypt failed");
        }
        security_context_free(ctx);
    } else {
        TEST_FAIL("Generated key works for encryption/decryption", "context creation failed");
    }

    free(key1);
    free(key2);
}

/* ============================================
 * Test: Hex Encoding/Decoding
 * ============================================ */
void test_hex_encoding(void)
{
    printf("\n=== Test: Hex Encoding/Decoding ===\n");

    /* Test encoding */
    uint8_t data[] = {0xde, 0xad, 0xbe, 0xef};
    char *encoded = hex_encode(data, sizeof(data));
    if (encoded != NULL && strcmp(encoded, "deadbeef") == 0) {
        TEST_PASS("Hex encoding works correctly");
    } else if (encoded == NULL) {
        TEST_FAIL("Hex encoding works correctly", "returned NULL");
    } else {
        char msg[100];
        snprintf(msg, sizeof(msg), "expected 'deadbeef', got '%s'", encoded);
        TEST_FAIL("Hex encoding works correctly", msg);
    }
    free(encoded);

    /* Test decoding */
    size_t decoded_len = 0;
    uint8_t *decoded = hex_decode("deadbeef", &decoded_len);
    if (decoded != NULL && decoded_len == 4 &&
        decoded[0] == 0xde && decoded[1] == 0xad &&
        decoded[2] == 0xbe && decoded[3] == 0xef) {
        TEST_PASS("Hex decoding works correctly");
    } else if (decoded == NULL) {
        TEST_FAIL("Hex decoding works correctly", "returned NULL");
    } else {
        TEST_FAIL("Hex decoding works correctly", "decoded data mismatch");
    }
    free(decoded);

    /* Test decoding invalid hex */
    decoded = hex_decode("invalid", &decoded_len);
    if (decoded == NULL) {
        TEST_PASS("Invalid hex decoding returns NULL");
    } else {
        TEST_FAIL("Invalid hex decoding returns NULL", "returned non-NULL");
        free(decoded);
    }

    /* Test decoding odd-length hex */
    decoded = hex_decode("abc", &decoded_len);
    if (decoded == NULL) {
        TEST_PASS("Odd-length hex decoding returns NULL");
    } else {
        TEST_FAIL("Odd-length hex decoding returns NULL", "returned non-NULL");
        free(decoded);
    }

    /* Test encoding NULL */
    encoded = hex_encode(NULL, 0);
    if (encoded == NULL) {
        TEST_PASS("Encoding NULL data returns NULL");
    } else {
        TEST_FAIL("Encoding NULL data returns NULL", "returned non-NULL");
        free(encoded);
    }
}

/* ============================================
 * Test: Edge Cases
 * ============================================ */
void test_edge_cases(void)
{
    printf("\n=== Test: Edge Cases ===\n");

    struct security_context *ctx = security_context_new(VALID_KEY);
    if (!ctx) {
        TEST_FAIL("Edge cases setup", "failed to create context");
        return;
    }

    /* Test empty message encryption */
    uint8_t ciphertext[CRYPTO_OVERHEAD + 16];
    uint8_t decrypted[16];
    size_t ct_len, pt_len;

    int ret = security_encrypt(ctx, (const uint8_t *)"", 0, ciphertext, &ct_len);
    if (ret == CRYPTO_OK && ct_len == CRYPTO_OVERHEAD) {
        TEST_PASS("Empty message encryption works");
    } else if (ret == CRYPTO_OK) {
        char msg[100];
        snprintf(msg, sizeof(msg), "unexpected ciphertext length: %zu", ct_len);
        TEST_FAIL("Empty message encryption works", msg);
    } else {
        TEST_FAIL("Empty message encryption works", "encryption failed");
    }

    /* Test decrypt of too-short ciphertext */
    uint8_t short_ct[CRYPTO_OVERHEAD - 1];
    memset(short_ct, 0, sizeof(short_ct));
    ret = security_decrypt(ctx, short_ct, sizeof(short_ct), decrypted, &pt_len);
    if (ret != CRYPTO_OK) {
        TEST_PASS("Too-short ciphertext rejected");
    } else {
        TEST_FAIL("Too-short ciphertext rejected", "decryption succeeded");
    }

    /* Test large message (sync message size ~512 bytes) */
    uint8_t large_plain[512];
    uint8_t large_cipher[512 + CRYPTO_OVERHEAD + 16];
    uint8_t large_decrypt[512 + 16];

    for (int i = 0; i < 512; i++) {
        large_plain[i] = (uint8_t)(i & 0xFF);
    }

    ret = security_encrypt(ctx, large_plain, sizeof(large_plain), large_cipher, &ct_len);
    if (ret == CRYPTO_OK) {
        ret = security_decrypt(ctx, large_cipher, ct_len, large_decrypt, &pt_len);
        if (ret == CRYPTO_OK && pt_len == sizeof(large_plain) &&
            memcmp(large_decrypt, large_plain, sizeof(large_plain)) == 0) {
            TEST_PASS("Large message (512 bytes) encrypt/decrypt works");
        } else {
            TEST_FAIL("Large message (512 bytes) encrypt/decrypt works", "decrypt failed");
        }
    } else {
        TEST_FAIL("Large message (512 bytes) encrypt/decrypt works", "encrypt failed");
    }

    security_context_free(ctx);
}

/* ============================================
 * Test: Nonce Uniqueness
 * ============================================ */
void test_nonce_uniqueness(void)
{
    printf("\n=== Test: Nonce Uniqueness ===\n");

    struct security_context *ctx = security_context_new(VALID_KEY);
    if (!ctx) {
        TEST_FAIL("Nonce uniqueness setup", "failed to create context");
        return;
    }

    const char *plaintext = "Same message, different nonces";
    size_t plaintext_len = strlen(plaintext) + 1;

    uint8_t cipher1[plaintext_len + CRYPTO_OVERHEAD + 16];
    uint8_t cipher2[plaintext_len + CRYPTO_OVERHEAD + 16];
    size_t ct_len1, ct_len2;

    /* Encrypt same message twice */
    int ret1 = security_encrypt(ctx, (const uint8_t *)plaintext, plaintext_len, cipher1, &ct_len1);
    int ret2 = security_encrypt(ctx, (const uint8_t *)plaintext, plaintext_len, cipher2, &ct_len2);

    if (ret1 == CRYPTO_OK && ret2 == CRYPTO_OK) {
        /* Check that the nonces (first 12 bytes) are different */
        if (memcmp(cipher1, cipher2, CRYPTO_NONCE_SIZE) != 0) {
            TEST_PASS("Nonces are different for same plaintext");
        } else {
            TEST_FAIL("Nonces are different for same plaintext", "nonces are identical!");
        }

        /* Ciphertexts should be different due to different nonces */
        if (memcmp(cipher1, cipher2, ct_len1) != 0) {
            TEST_PASS("Ciphertexts are different for same plaintext");
        } else {
            TEST_FAIL("Ciphertexts are different for same plaintext", "ciphertexts are identical!");
        }
    } else {
        TEST_FAIL("Nonce uniqueness test", "encryption failed");
    }

    security_context_free(ctx);
}

/* ============================================
 * Main
 * ============================================ */
int main(void)
{
    printf("=========================================\n");
    printf("lease-sync Crypto Module Unit Tests\n");
    printf("=========================================\n");

    /* Run all tests */
    test_key_validation();
    test_context_creation();
    test_encrypt_decrypt_roundtrip();
    test_tamper_detection();
    test_wrong_key_detection();
    test_key_generation();
    test_hex_encoding();
    test_edge_cases();
    test_nonce_uniqueness();

    return print_test_summary();
}
