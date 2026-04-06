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
 * crypto.h - AES-256-GCM encryption interface
 *
 * Provides authenticated encryption for UDP sync messages.
 * Wire format: [12-byte nonce][ciphertext][16-byte auth tag]
 */

#ifndef LEASE_SYNC_CRYPTO_H
#define LEASE_SYNC_CRYPTO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* AES-256-GCM parameters */
#define CRYPTO_KEY_SIZE      32   /* 256 bits */
#define CRYPTO_KEY_HEX_SIZE  64   /* 64 hex chars for 32 bytes */
#define CRYPTO_NONCE_SIZE    12   /* 96 bits (recommended for GCM) */
#define CRYPTO_TAG_SIZE      16   /* 128 bits authentication tag */
#define CRYPTO_OVERHEAD      (CRYPTO_NONCE_SIZE + CRYPTO_TAG_SIZE)  /* 28 bytes */

/* Error codes */
#define CRYPTO_OK            0
#define CRYPTO_ERROR        -1
#define CRYPTO_ERROR_MEMORY -2
#define CRYPTO_ERROR_KEY    -3
#define CRYPTO_ERROR_ENCRYPT -4
#define CRYPTO_ERROR_DECRYPT -5
#define CRYPTO_ERROR_AUTH   -6   /* Authentication failed */

/* Opaque security context */
struct security_context;

/*
 * Create a new security context from a hex-encoded key.
 *
 * @param key_hex  64-character hex string (representing 32 bytes for AES-256)
 * @return         Security context, or NULL on error
 */
struct security_context *security_context_new(const char *key_hex);

/*
 * Free a security context, securely erasing the key material.
 *
 * @param ctx  Security context to free
 */
void security_context_free(struct security_context *ctx);

/*
 * Encrypt a message using AES-256-GCM.
 *
 * The output format is: [12-byte nonce][ciphertext][16-byte auth tag]
 *
 * @param ctx            Security context
 * @param plaintext      Data to encrypt
 * @param plaintext_len  Length of plaintext
 * @param ciphertext     Output buffer (must be at least plaintext_len + CRYPTO_OVERHEAD)
 * @param ciphertext_len Output: length of ciphertext written
 * @return               CRYPTO_OK on success, error code otherwise
 */
int security_encrypt(struct security_context *ctx,
                     const uint8_t *plaintext, size_t plaintext_len,
                     uint8_t *ciphertext, size_t *ciphertext_len);

/*
 * Decrypt a message using AES-256-GCM.
 *
 * Verifies the authentication tag and fails if tampered.
 *
 * @param ctx            Security context
 * @param ciphertext     Encrypted data (format: nonce + ciphertext + tag)
 * @param ciphertext_len Length of ciphertext
 * @param plaintext      Output buffer (must be at least ciphertext_len - CRYPTO_OVERHEAD)
 * @param plaintext_len  Output: length of plaintext written
 * @return               CRYPTO_OK on success, CRYPTO_ERROR_AUTH if tampered
 */
int security_decrypt(struct security_context *ctx,
                     const uint8_t *ciphertext, size_t ciphertext_len,
                     uint8_t *plaintext, size_t *plaintext_len);

/*
 * Generate a random 256-bit encryption key.
 *
 * @return  64-character hex string (caller must free), or NULL on error
 */
char *security_generate_key(void);

/*
 * Validate that a key string is a valid 64-character hex string.
 *
 * @param key_hex  Key string to validate
 * @return         true if valid, false otherwise
 */
bool security_validate_key(const char *key_hex);

/*
 * Decode a hex string to bytes.
 *
 * @param hex      Hex string to decode
 * @param out_len  Output: length of decoded bytes
 * @return         Decoded bytes (caller must free), or NULL on error
 */
uint8_t *hex_decode(const char *hex, size_t *out_len);

/*
 * Encode bytes to a hex string.
 *
 * @param data  Bytes to encode
 * @param len   Length of data
 * @return      Hex string (caller must free), or NULL on error
 */
char *hex_encode(const uint8_t *data, size_t len);

#endif /* LEASE_SYNC_CRYPTO_H */
