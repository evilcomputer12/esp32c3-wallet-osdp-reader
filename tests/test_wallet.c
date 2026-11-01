// SPDX-License-Identifier: GPL-3.0-or-later
#include "reader.h"
#include "wallet_crypto.h"
#include "wallet_fixtures.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static wallet_config_t config;
static wallet_reader_t reader;
static unsigned random_calls;
static int random_fixture(void *u, uint8_t *out, size_t n) {
  (void)u;
  ++random_calls;
  memset(out, 0, n);
  if (random_calls == 1) {
    assert(n == 4 || n == 8);
    for (size_t i = 0; i < n; ++i)
      out[i] = (uint8_t)(0xa0 + i);
  } else if (random_calls == 2) {
    assert(n == 32);
    for (size_t i = 0; i < n; ++i)
      out[i] = (uint8_t)(32 + i);
  } else {
    assert(random_calls == 3 && n == 32);
    out[31] = 5; // PUBLIC disposable scalar, only in the host fixture.
  }
  return 0;
}
static int apple_ecdh(void *u, const uint8_t peer[65], uint8_t shared[32]) {
  (void)u;
  uint8_t scalar[32] = {0};
  scalar[31] = 1;
  return wallet_ecdh(scalar, peer, shared);
}
static int google_sign(void *u, const uint8_t digest[32], uint8_t *out,
                       size_t *n) {
  (void)u;
  assert(!memcmp(digest, fixture_google_digest, 32));
  assert(*n >= sizeof(fixture_google_signature));
  memcpy(out, fixture_google_signature, sizeof(fixture_google_signature));
  *n = sizeof(fixture_google_signature);
  return 0;
}
static size_t next(uint8_t out[WALLET_MAX_COMMAND]) {
  size_t n;
  assert(wallet_next(&reader, out, WALLET_MAX_COMMAND, &n));
  return n;
}
static void begin(void) {
  wallet_hooks_t hooks = {NULL, random_fixture, apple_ecdh, google_sign};
  random_calls = 0;
  assert(wallet_begin(&reader, &config, &hooks));
  uint8_t out[WALLET_MAX_COMMAND];
  static const uint8_t ose[] = {0,   0xa4, 4,   0,   10,  'O', 'S', 'E',
                                '.', 'V',  'A', 'S', '.', '0', '1', 0};
  assert(next(out) == sizeof(ose) && !memcmp(out, ose, sizeof(ose)));
}
static void apple(unsigned method, bool corrupt) {
  begin();
  assert(wallet_feed(&reader, fixture_ose_apple, sizeof(fixture_ose_apple)));
  uint8_t out[WALLET_MAX_COMMAND], response[256];
  assert(next(out) == 60 && out[0] == 0x80 && out[1] == 0xca);
  assert(!memcmp(out + 13, fixture_pass_hash, 32));
  const uint8_t *fixture =
      method == 1 ? fixture_apple_response1 : fixture_apple_response2;
  size_t n = method == 1 ? sizeof(fixture_apple_response1)
                         : sizeof(fixture_apple_response2);
  memcpy(response, fixture, n);
  if (corrupt)
    response[n - 3] ^= 1;
  assert(wallet_feed(&reader, response, n) == !corrupt);
  if (corrupt) {
    assert(reader.state == WS_FAILED && reader.result.length == 0);
    return;
  }
  assert(reader.state == WS_DONE && reader.result.provider == PROVIDER_APPLE);
  assert(reader.result.length == sizeof(fixture_apple_value));
  assert(!memcmp(reader.result.value, fixture_apple_value,
                 sizeof(fixture_apple_value)));
  assert(!memcmp(reader.result.credential, fixture_apple_credential, 32));
}
static void google(unsigned mode) {
  begin();
  assert(wallet_feed(&reader, fixture_ose_google, sizeof(fixture_ose_google)));
  uint8_t out[WALLET_MAX_COMMAND], response[WALLET_MAX_RESPONSE];
  assert(next(out) == 15 && out[1] == 0xa4);
  assert(wallet_feed(&reader, fixture_google_select,
                     sizeof(fixture_google_select)));
  assert(next(out) == sizeof(fixture_google_negotiate_command));
  assert(!memcmp(out, fixture_google_negotiate_command,
                 sizeof(fixture_google_negotiate_command)));
  assert(wallet_feed(&reader, fixture_google_negotiate_response,
                     sizeof(fixture_google_negotiate_response)));
  assert(next(out) > 5 && out[1] == 0x50);
  size_t n = sizeof(fixture_google_response);
  memcpy(response, fixture_google_response, n);
  if (mode == 2)
    response[n - 3] ^= 1; // Invalid HMAC, no plaintext/event.
  if (mode == 1) {
    uint8_t fragment[39];
    memcpy(fragment, response, 37);
    fragment[37] = 0x90;
    fragment[38] = 0x91;
    assert(wallet_feed(&reader, fragment, sizeof(fragment)));
    assert(next(out) == 5 && out[1] == 0xc0);
    memmove(response, response + 37, n - 37);
    n -= 37;
  }
  assert(wallet_feed(&reader, response, n) == (mode != 2));
  if (mode == 2) {
    assert(reader.state == WS_FAILED && reader.result.length == 0);
    return;
  }
  assert(reader.state == WS_DONE && reader.result.provider == PROVIDER_GOOGLE);
  assert(reader.result.length == sizeof(fixture_google_value));
  assert(!memcmp(reader.result.value, fixture_google_value,
                 sizeof(fixture_google_value)));
  assert(!memcmp(reader.result.credential, fixture_google_credential, 32));
}
int main(void) {
  assert(reader_crypto_selftest());
  config.apple = config.google = config.key_version_configured = true;
  config.collector_id = 12345678;
  config.key_version = 7;
  memcpy(config.pass_hash, fixture_pass_hash, 32);
  memcpy(config.apple_key_id, fixture_apple_key_id, 4);
  apple(1, false);
  apple(2, false);
  apple(1, true);
  apple(2, true);
  google(0);
  google(1);
  google(2);
  begin();
  const uint8_t malformed[] = {0x6f, 0x80, 0x90, 0};
  assert(!wallet_feed(&reader, malformed, sizeof(malformed)));
  wallet_reset(&reader);
  uint8_t zero[sizeof(reader)] = {0};
  assert(!memcmp(&reader, zero, sizeof(reader)));
  puts("Independent Apple/Google encrypted fixtures, tampering, fragmentation, "
       "wipe and AES/SHA vectors passed");
  return 0;
}
