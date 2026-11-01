// SPDX-License-Identifier: GPL-3.0-or-later
#include "reader_config.h"
#include "reader.h"
#include "wallet_crypto.h"
#include <string.h>
#if __has_include("reader_private.h")
#include "reader_private.h"
#else
#include "reader_config.example.h"
#endif
reader_settings_t reader_settings;
static wallet_config_t wallet_cfg;
void reader_config_init(void) { reader_settings = reader_initial_settings; }
bool reader_configured(void) { return reader_settings.scbk_configured; }
const uint8_t *reader_scbk(void) { return reader_settings.scbk; }
static int random_bytes(void *u, uint8_t *out, size_t n) {
  (void)u;
  if (n > UINT32_MAX)
    return -1;
  return reader_rng_block(out, (unsigned)n);
}
static int apple_ecdh(void *u, const uint8_t peer[65], uint8_t shared[32]) {
  (void)u;
  if (reader_settings.apple_slot >= 0)
    return reader_atecc_wallet_ecdh((unsigned)reader_settings.apple_slot, peer,
                                    shared);
  return wallet_ecdh(reader_settings.apple_private, peer, shared);
}
static int google_sign(void *u, const uint8_t digest[32], uint8_t *out,
                       size_t *n) {
  (void)u;
  if (reader_settings.google_slot >= 0) {
    uint8_t raw[64];
    int rc = reader_atecc_wallet_sign((unsigned)reader_settings.google_slot,
                                      digest, raw);
    if (!rc)
      rc = wallet_signature_der(raw, out, n);
    wallet_wipe(raw, sizeof(raw));
    return rc;
  }
  return wallet_sign(reader_settings.google_private, digest, out, n);
}
wallet_hooks_t reader_wallet_hooks(void) {
  return (wallet_hooks_t){NULL, random_bytes, apple_ecdh, google_sign};
}
const wallet_config_t *reader_wallet_config(void) { return &wallet_cfg; }
bool reader_wallet_begin(void) {
  wallet_config_t c = {0};
  uint8_t pub[65], hash[32];
  bool ok = true;
  if (reader_settings.apple_enabled) {
    size_t n = strnlen(reader_settings.apple_pass_type,
                       sizeof(reader_settings.apple_pass_type));
    if (n < 6 || n >= sizeof(reader_settings.apple_pass_type) ||
        memcmp(reader_settings.apple_pass_type, "pass.", 5))
      ok = false;
    if (reader_settings.apple_slot < -1 || reader_settings.apple_slot > 15)
      ok = false;
    if (ok && wallet_sha256((uint8_t *)reader_settings.apple_pass_type, n,
                            c.pass_hash))
      ok = false;
    if (ok) {
      if (reader_settings.apple_slot >= 0)
        ok = wallet_expand(reader_settings.apple_public, 65, pub) == 0;
      else
        ok = wallet_public(reader_settings.apple_private, pub) == 0;
    }
    if (ok && wallet_sha256(pub + 1, 32, hash))
      ok = false;
    if (ok) {
      memcpy(c.apple_key_id, hash, 4);
      c.apple = true;
    }
  }
  if (ok && reader_settings.google_enabled) {
    if (!reader_settings.google_key_version_set ||
        reader_settings.google_slot < -1 || reader_settings.google_slot > 15)
      ok = false;
    if (ok && reader_settings.google_slot < 0 &&
        wallet_public(reader_settings.google_private, pub))
      ok = false;
    if (ok) {
      c.google = true;
      c.collector_id = reader_settings.google_collector;
      c.key_version = reader_settings.google_key_version;
      c.key_version_configured = true;
    }
  }
  wallet_wipe(pub, sizeof(pub));
  wallet_wipe(hash, sizeof(hash));
  wallet_cfg = ok ? c : (wallet_config_t){0};
  return ok;
}
