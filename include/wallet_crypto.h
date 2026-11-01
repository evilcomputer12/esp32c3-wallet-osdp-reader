// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// All functions return zero on success. No plaintext is released on failure.
void wallet_wipe(void *p, size_t n);
int wallet_sha256(const uint8_t *p, size_t n, uint8_t out[32]);
int wallet_public(const uint8_t secret[32], uint8_t out[65]);
int wallet_expand(const uint8_t *encoded, size_t n, uint8_t out[65]);
int wallet_ecdh(const uint8_t secret[32], const uint8_t peer[65],
                uint8_t out[32]);
#ifndef READER_WALLET_APPLE_ONLY
int wallet_sign(const uint8_t secret[32], const uint8_t digest[32],
                uint8_t *der, size_t *n);
int wallet_signature_der(const uint8_t raw[64], uint8_t *der, size_t *n);
#endif
#ifndef READER_WALLET_GOOGLE_ONLY
int wallet_vas_key(const uint8_t shared[32], const uint8_t pass_hash[32],
                   unsigned method, uint8_t out[32]);
int wallet_vas_open(const uint8_t key[32], const uint8_t *blob, size_t n,
                    const uint8_t *aad, size_t aad_n, uint8_t *out, size_t cap);
#endif
#ifndef READER_WALLET_APPLE_ONLY
int wallet_gst_keys(const uint8_t shared[32], const uint8_t peer[33],
                    const uint8_t transcript[101], const uint8_t *sig,
                    size_t sig_n, uint8_t out[48]);
int wallet_gst_open(const uint8_t keys[48], const uint8_t *blob, size_t n,
                    uint8_t *out, size_t cap);
#endif
#ifdef __cplusplus
}
#endif
