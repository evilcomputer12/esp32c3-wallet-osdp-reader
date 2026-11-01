// SPDX-License-Identifier: GPL-3.0-or-later
// C stdin/stdout harness compatible with the original independent phone
// emulator.
#include "wallet_crypto.h"
#include "wallet_reader.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wolfssl/wolfcrypt/random.h>
static uint8_t apple_key[32], google_key[32];
static int random_bytes(void *u, uint8_t *p, size_t n) {
  (void)u;
  WC_RNG rng;
  if (wc_InitRng(&rng))
    return -1;
  int rc = wc_RNG_GenerateBlock(&rng, p, n);
  wc_FreeRng(&rng);
  return rc;
}
static int sign_digest(void *u, const uint8_t *p, uint8_t *s, size_t *n) {
  (void)u;
  return wallet_sign(google_key, p, s, n);
}
static int ecdh(void *u, const uint8_t *p, uint8_t *z) {
  (void)u;
  return wallet_ecdh(apple_key, p, z);
}
static int digit(char c) {
  return c >= '0' && c <= '9'   ? c - '0'
         : c >= 'a' && c <= 'f' ? c - 'a' + 10
         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                : -1;
}
static bool unhex(const char *s, uint8_t *out, size_t cap, size_t *used) {
  size_t n = strcspn(s, "\r\n");
  *used = 0;
  if (n % 2 || n > cap * 2)
    return false;
  for (size_t i = 0; i < n; i += 2) {
    int a = digit(s[i]), b = digit(s[i + 1]);
    if (a < 0 || b < 0)
      return false;
    out[(*used)++] = (uint8_t)(a * 16 + b);
  }
  return true;
}
static void hex(const uint8_t *p, size_t n) {
  while (n--)
    printf("%02x", *p++);
}
int main(void) {
  char line[2 * WALLET_MAX_RESPONSE + 8];
  wallet_config_t c = {0};
  size_t n;
  if (!fgets(line, sizeof(line), stdin))
    return 2;
  line[strcspn(line, "\r\n")] = 0;
  if (wallet_sha256((uint8_t *)line, strlen(line), c.pass_hash))
    return 2;
  if (!fgets(line, sizeof(line), stdin) || !unhex(line, apple_key, 32, &n) ||
      n != 32)
    return 2;
  if (!fgets(line, sizeof(line), stdin) || !unhex(line, google_key, 32, &n) ||
      n != 32)
    return 2;
  wallet_wipe(line, sizeof(line));
  if (!fgets(line, sizeof(line), stdin) ||
      sscanf(line, "%" SCNu32 " %" SCNu32, &c.collector_id, &c.key_version) !=
          2)
    return 2;
  c.apple = c.google = c.key_version_configured = true;
  uint8_t pub[65], hash[32];
  if (wallet_public(apple_key, pub) || wallet_sha256(pub + 1, 32, hash))
    return 2;
  memcpy(c.apple_key_id, hash, 4);
  wallet_hooks_t h = {NULL, random_bytes, ecdh, sign_digest};
  wallet_reader_t r = {0};
  if (!wallet_begin(&r, &c, &h))
    return 2;
  uint8_t in[WALLET_MAX_RESPONSE], cmd[WALLET_MAX_COMMAND];
  while (r.state != WS_DONE && r.state != WS_FAILED) {
    if (!wallet_next(&r, cmd, sizeof(cmd), &n))
      break;
    printf("APDU ");
    hex(cmd, n);
    puts("");
    fflush(stdout);
    if (!fgets(line, sizeof(line), stdin) || !unhex(line, in, sizeof(in), &n))
      return 2;
    if (!wallet_feed(&r, in, n))
      break;
  }
  int rc = 1;
  if (r.state == WS_DONE) {
    printf("OK %u ", (unsigned)r.result.provider);
    hex(r.result.credential, 32);
    printf(" ");
    hex(r.result.value, r.result.length);
    puts("");
    rc = 0;
  } else
    printf("FAIL %u %u\n", (unsigned)r.fault, r.status);
  wallet_reset(&r);
  wallet_wipe(apple_key, 32);
  wallet_wipe(google_key, 32);
  return rc;
}
