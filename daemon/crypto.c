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
 * crypto.c - AES-256-GCM encryption implementation
 *
 * Uses OpenSSL EVP interface for authenticated encryption.
 */

#include "crypto.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>

/* Security context structure */
struct security_context
{
  uint8_t key[CRYPTO_KEY_SIZE];
  EVP_CIPHER_CTX *ctx;
};

char *hex_encode(const uint8_t *data, size_t len)
{
  size_t i;
  char *hex;

  if (!data)
    return NULL;

  hex = malloc(len * 2 + 1);
  if (!hex)
    return NULL;

  for (i = 0; i < len; i++)
    {
      sprintf(hex + i * 2, "%02x", data[i]);
    }
  hex[len * 2] = '\0';

  return hex;
}

uint8_t *hex_decode(const char *hex, size_t *out_len)
{
  size_t hex_len, byte_len, i;
  uint8_t *bytes;

  if (!hex || !out_len)
    return NULL;

  hex_len = strlen(hex);
  if (hex_len % 2 != 0)
    return NULL;

  byte_len = hex_len / 2;
  bytes = malloc(byte_len);
  if (!bytes)
    return NULL;

  for (i = 0; i < byte_len; i++)
    {
      char byte_str[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
      if (!isxdigit((unsigned char)byte_str[0]) ||
          !isxdigit((unsigned char)byte_str[1]))
        {
          free(bytes);
          return NULL;
        }
      bytes[i] = (uint8_t)strtol(byte_str, NULL, 16);
    }

  *out_len = byte_len;
  return bytes;
}

bool security_validate_key(const char *key_hex)
{
  size_t len, i;

  if (!key_hex)
    return false;

  len = strlen(key_hex);
  if (len != CRYPTO_KEY_HEX_SIZE)
    return false;

  for (i = 0; i < len; i++)
    {
      if (!isxdigit((unsigned char)key_hex[i]))
        return false;
    }

  return true;
}

struct security_context *security_context_new(const char *key_hex)
{
  size_t key_len = 0;
  uint8_t *key_bytes;
  struct security_context *ctx;

  if (!key_hex)
    return NULL;

  /* Decode hex key */
  key_bytes = hex_decode(key_hex, &key_len);
  if (!key_bytes || key_len != CRYPTO_KEY_SIZE)
    {
      free(key_bytes);
      return NULL;
    }

  /* Allocate context */
  ctx = malloc(sizeof(struct security_context));
  if (!ctx)
    {
      explicit_bzero(key_bytes, key_len);
      free(key_bytes);
      return NULL;
    }

  /* Copy key and clear temporary */
  memcpy(ctx->key, key_bytes, CRYPTO_KEY_SIZE);
  explicit_bzero(key_bytes, key_len);
  free(key_bytes);

  /* Create cipher context */
  ctx->ctx = EVP_CIPHER_CTX_new();
  if (!ctx->ctx)
    {
      explicit_bzero(ctx->key, CRYPTO_KEY_SIZE);
      free(ctx);
      return NULL;
    }

  return ctx;
}

void security_context_free(struct security_context *ctx)
{
  if (!ctx)
    return;

  if (ctx->ctx)
    {
      EVP_CIPHER_CTX_free(ctx->ctx);
    }

  /* Securely erase key material */
  explicit_bzero(ctx->key, CRYPTO_KEY_SIZE);
  free(ctx);
}

int security_encrypt(struct security_context *ctx,
                     const uint8_t *plaintext, size_t plaintext_len,
                     uint8_t *ciphertext, size_t *ciphertext_len)
{
  uint8_t nonce[CRYPTO_NONCE_SIZE];
  int len = 0;
  int total_len;
  uint8_t tag[CRYPTO_TAG_SIZE];

  if (!ctx || !plaintext || !ciphertext || !ciphertext_len)
    return CRYPTO_ERROR;

  /* Generate random nonce */
  if (RAND_bytes(nonce, CRYPTO_NONCE_SIZE) != 1)
    {
      return CRYPTO_ERROR_ENCRYPT;
    }

  /* Copy nonce to output */
  memcpy(ciphertext, nonce, CRYPTO_NONCE_SIZE);

  /* Initialize encryption */
  if (EVP_EncryptInit_ex(ctx->ctx, EVP_aes_256_gcm(), NULL, ctx->key, nonce) != 1)
    {
      return CRYPTO_ERROR_ENCRYPT;
    }

  /* Encrypt data */
  if (EVP_EncryptUpdate(ctx->ctx, ciphertext + CRYPTO_NONCE_SIZE, &len,
                        plaintext, plaintext_len) != 1)
    {
      return CRYPTO_ERROR_ENCRYPT;
    }
  total_len = len;

  /* Finalize encryption */
  if (EVP_EncryptFinal_ex(ctx->ctx, ciphertext + CRYPTO_NONCE_SIZE + len, &len) != 1)
    {
      return CRYPTO_ERROR_ENCRYPT;
    }
  total_len += len;

  /* Get authentication tag */
  if (EVP_CIPHER_CTX_ctrl(ctx->ctx, EVP_CTRL_GCM_GET_TAG, CRYPTO_TAG_SIZE, tag) != 1)
    {
      return CRYPTO_ERROR_ENCRYPT;
    }

  /* Append tag to output */
  memcpy(ciphertext + CRYPTO_NONCE_SIZE + total_len, tag, CRYPTO_TAG_SIZE);

  *ciphertext_len = CRYPTO_NONCE_SIZE + total_len + CRYPTO_TAG_SIZE;

  return CRYPTO_OK;
}

int security_decrypt(struct security_context *ctx,
                     const uint8_t *ciphertext, size_t ciphertext_len,
                     uint8_t *plaintext, size_t *plaintext_len)
{
  const uint8_t *nonce;
  const uint8_t *encrypted_data;
  size_t encrypted_len;
  const uint8_t *tag;
  int len = 0;
  int total_len;

  if (!ctx || !ciphertext || !plaintext || !plaintext_len)
    return CRYPTO_ERROR;

  /* Check minimum size */
  if (ciphertext_len < CRYPTO_OVERHEAD)
    return CRYPTO_ERROR;

  /* Extract components */
  nonce = ciphertext;
  encrypted_data = ciphertext + CRYPTO_NONCE_SIZE;
  encrypted_len = ciphertext_len - CRYPTO_NONCE_SIZE - CRYPTO_TAG_SIZE;
  tag = ciphertext + ciphertext_len - CRYPTO_TAG_SIZE;

  /* Initialize decryption */
  if (EVP_DecryptInit_ex(ctx->ctx, EVP_aes_256_gcm(), NULL, ctx->key, nonce) != 1)
    {
      return CRYPTO_ERROR_DECRYPT;
    }

  /* Decrypt data */
  if (EVP_DecryptUpdate(ctx->ctx, plaintext, &len, encrypted_data, encrypted_len) != 1)
    {
      return CRYPTO_ERROR_DECRYPT;
    }
  total_len = len;

  /* Set expected tag for verification */
  if (EVP_CIPHER_CTX_ctrl(ctx->ctx, EVP_CTRL_GCM_SET_TAG, CRYPTO_TAG_SIZE, (void *)tag) != 1)
    {
      return CRYPTO_ERROR_DECRYPT;
    }

  /* Finalize decryption and verify tag */
  if (EVP_DecryptFinal_ex(ctx->ctx, plaintext + len, &len) != 1)
    {
      /* Authentication failed - data was tampered */
      return CRYPTO_ERROR_AUTH;
    }
  total_len += len;

  *plaintext_len = total_len;

  return CRYPTO_OK;
}

char *security_generate_key(void)
{
  uint8_t key[CRYPTO_KEY_SIZE];
  char *hex_key;

  if (RAND_bytes(key, CRYPTO_KEY_SIZE) != 1)
    {
      return NULL;
    }

  hex_key = hex_encode(key, CRYPTO_KEY_SIZE);

  /* Securely erase temporary key */
  explicit_bzero(key, CRYPTO_KEY_SIZE);

  return hex_key;
}
