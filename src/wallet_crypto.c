// SPDX-License-Identifier: GPL-3.0-or-later
// Replaces the supplied micro-ecc, SHA256 and AES implementations with
// wolfCrypt.
#include "wallet_crypto.h"
#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/hmac.h>
#include <wolfssl/wolfcrypt/kdf.h>
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/sha256.h>

void wallet_wipe(void *p, size_t n) {
  volatile uint8_t *v = (volatile uint8_t *)p;
  while (n--)
    *v++ = 0;
}
int wallet_sha256(const uint8_t *p, size_t n, uint8_t out[32]) {
  if ((!p && n) || !out || n > UINT32_MAX)
    return -1;
  return wc_Sha256Hash(p, (word32)n, out);
}
typedef struct {
  ecc_key v;
  bool ok;
} Key;
typedef struct {
  WC_RNG v;
  bool ok;
} Random;
static void key_init(Key *k) { k->ok = wc_ecc_init(&k->v) == 0; }
static void key_free(Key *k) {
  if (k->ok)
    wc_ecc_free(&k->v);
}
static void rng_init(Random *r) { r->ok = wc_InitRng(&r->v) == 0; }
static void rng_free(Random *r) {
  if (r->ok)
    wc_FreeRng(&r->v);
}
static int load_private(Key *k, const uint8_t *s) {
  if (!k->ok || !s)
    return -1;
  return wc_ecc_import_private_key_ex(s, 32, NULL, 0, &k->v, ECC_SECP256R1);
}
static int export_public(Key *k, uint8_t *out) {
  word32 n = 65;
  return wc_ecc_export_x963(&k->v, out, &n) == 0 && n == 65 ? 0 : -1;
}
int wallet_public(const uint8_t secret[32], uint8_t out[65]) {
  if (!out)
    return -1;
  Key k;
  Random rng;
  key_init(&k);
  rng_init(&rng);
  int rc = load_private(&k, secret);
  if (!rc && !rng.ok)
    rc = -1;
  if (!rc)
    rc = wc_ecc_set_rng(&k.v, &rng.v);
  if (!rc)
    rc = wc_ecc_make_pub(&k.v, NULL);
  if (!rc)
    rc = wc_ecc_check_key(&k.v);
  if (!rc)
    rc = export_public(&k, out);
  if (rc)
    wallet_wipe(out, 65);
  key_free(&k);
  rng_free(&rng);
  return rc;
}
int wallet_expand(const uint8_t *p, size_t n, uint8_t out[65]) {
  if (!out)
    return -1;
  if (!p ||
      !((n == 33 && (p[0] == 2 || p[0] == 3)) || (n == 65 && p[0] == 4))) {
    wallet_wipe(out, 65);
    return -1;
  }
  Key k;
  key_init(&k);
  int rc = k.ok ? wc_ecc_import_x963_ex(p, (word32)n, &k.v, ECC_SECP256R1) : -1;
  if (!rc)
    rc = wc_ecc_check_key(&k.v);
  if (!rc)
    rc = export_public(&k, out);
  if (rc)
    wallet_wipe(out, 65);
  key_free(&k);
  return rc;
}
int wallet_ecdh(const uint8_t secret[32], const uint8_t peer[65],
                uint8_t out[32]) {
  if (!out)
    return -1;
  Key k, p;
  Random rng;
  key_init(&k);
  key_init(&p);
  rng_init(&rng);
  int rc = load_private(&k, secret);
  word32 n = 32;
  if (!rc && (!p.ok || !rng.ok || !peer || peer[0] != 4))
    rc = -1;
  if (!rc)
    rc = wc_ecc_import_x963_ex(peer, 65, &p.v, ECC_SECP256R1);
  if (!rc)
    rc = wc_ecc_check_key(&p.v);
  if (!rc)
    rc = wc_ecc_set_rng(&k.v, &rng.v);
  if (!rc)
    rc = wc_ecc_shared_secret(&k.v, &p.v, out, &n);
  key_free(&k);
  key_free(&p);
  rng_free(&rng);
  if (rc || n != 32) {
    wallet_wipe(out, 32);
    return -1;
  }
  return 0;
}
#ifndef READER_WALLET_APPLE_ONLY
int wallet_sign(const uint8_t secret[32], const uint8_t digest[32],
                uint8_t *der, size_t *n) {
  if (!digest || !der || !n || *n > UINT32_MAX)
    return -1;
  const size_t cap = *n;
  *n = 0;
  Key k;
  Random rng;
  key_init(&k);
  rng_init(&rng);
  int rc = load_private(&k, secret);
  word32 used = (word32)cap;
  if (!rc && !rng.ok)
    rc = -1;
  if (!rc)
    rc = wc_ecc_set_rng(&k.v, &rng.v);
  if (!rc)
    rc = wc_ecc_sign_hash(digest, 32, der, &used, &rng.v, &k.v);
  if (rc)
    wallet_wipe(der, cap);
  else
    *n = used;
  key_free(&k);
  rng_free(&rng);
  return rc;
}
int wallet_signature_der(const uint8_t raw[64], uint8_t *der, size_t *n) {
  if (!raw || !der || !n || *n > UINT32_MAX)
    return -1;
  word32 used = (word32)*n;
  const size_t cap = *n;
  *n = 0;
  const int rc = wc_ecc_rs_raw_to_sig(raw, 32, raw + 32, 32, der, &used);
  if (rc)
    wallet_wipe(der, cap);
  else
    *n = used;
  return rc;
}
#endif
#ifndef READER_WALLET_GOOGLE_ONLY
int wallet_vas_key(const uint8_t shared[32], const uint8_t pass_hash[32],
                   unsigned method, uint8_t out[32]) {
  if (!out)
    return -1;
  wallet_wipe(out, 32);
  if (!shared || !pass_hash || (method != 1 && method != 2))
    return -1;
  // Protocol-defined context; sizeof-1 excludes the C string terminator.
  static const uint8_t context[] = "\x0d"
                                   "id-aes256-GCM"
                                   "ApplePay encrypted VAS data";
  uint8_t info[sizeof(context) - 1 + 32];
  memcpy(info, context, sizeof(context) - 1);
  memcpy(info + sizeof(context) - 1, pass_hash, 32);
  static const uint8_t second_context[] = "ApplePay encrypted VAS data";
  const int rc = wc_X963_KDF(
      WC_HASH_TYPE_SHA256, shared, 32, method == 1 ? info : second_context,
      method == 1 ? sizeof(info) : sizeof(second_context) - 1, out, 32);
  if (rc)
    wallet_wipe(out, 32);
  return rc;
}
int wallet_vas_open(const uint8_t key[32], const uint8_t *blob, size_t n,
                    const uint8_t *aad, size_t aad_n, uint8_t *out,
                    size_t cap) {
  if (!out)
    return -1;
  wallet_wipe(out, cap);
  if (!key || !blob || n < 20 || n - 16 > cap || n > UINT32_MAX ||
      (!aad && aad_n) || aad_n > UINT32_MAX)
    return -1;
  Aes aes;
  if (wc_AesInit(&aes, NULL, INVALID_DEVID))
    return -1;
  // VAS specifies a 16-byte zero IV, not the usual 12-byte GCM nonce.
  const uint8_t nonce[16] = {0};
  int rc = wc_AesGcmSetKey(&aes, key, 32);
  if (!rc)
    rc = wc_AesGcmDecrypt(&aes, out, blob, (word32)n - 16, nonce, sizeof(nonce),
                          blob + n - 16, 16, aad, (word32)aad_n);
  wc_AesFree(&aes);
  if (rc)
    wallet_wipe(out, cap);
  return rc;
}
#endif
#ifndef READER_WALLET_APPLE_ONLY
int wallet_gst_keys(const uint8_t shared[32], const uint8_t peer[33],
                    const uint8_t transcript[101], const uint8_t *sig,
                    size_t sig_n, uint8_t out[48]) {
  if (!shared || !peer || !transcript || !sig || sig_n < 8 || sig_n > 72 ||
      !out)
    return -1;
  uint8_t info[173];
  memcpy(info, transcript, 101);
  memcpy(info + 101, sig, sig_n);
  const int rc = wc_HKDF(WC_SHA256, shared, 32, peer, 33, info,
                         (word32)(101 + sig_n), out, 48);
  wallet_wipe(info, sizeof(info));
  if (rc)
    wallet_wipe(out, 48);
  return rc;
}
int wallet_gst_open(const uint8_t keys[48], const uint8_t *blob, size_t n,
                    uint8_t *out, size_t cap) {
  if (!out)
    return -1;
  wallet_wipe(out, cap);
  if (!keys || !blob || n < 44 || n - 44 > cap || n > UINT32_MAX)
    return -1;
  uint8_t mac[32] = {0}, iv[16] = {0};
  Hmac h;
  int rc = wc_HmacInit(&h, NULL, INVALID_DEVID);
  if (rc)
    return rc;
  rc = wc_HmacSetKey(&h, WC_SHA256, keys + 16, 32);
  if (!rc)
    rc = wc_HmacUpdate(&h, blob, (word32)n - 32);
  if (!rc)
    rc = wc_HmacFinal(&h, mac);
  wc_HmacFree(&h);
  uint8_t different = 0;
  for (size_t i = 0; i < 32; ++i)
    different |= mac[i] ^ blob[n - 32 + i];
  wallet_wipe(mac, sizeof(mac));
  if (rc || different)
    return -1; // Authenticate the IV and ciphertext BEFORE decryption.
  memcpy(iv, blob, 12);
  Aes aes;
  if (wc_AesInit(&aes, NULL, INVALID_DEVID))
    return -1;
  rc = wc_AesSetKey(&aes, keys, 16, iv, AES_ENCRYPTION);
  if (!rc)
    rc = wc_AesCtrEncrypt(&aes, out, blob + 12, (word32)n - 44);
  wc_AesFree(&aes);
  wallet_wipe(iv, sizeof(iv));
  if (rc)
    wallet_wipe(out, cap);
  return rc;
}
#endif
