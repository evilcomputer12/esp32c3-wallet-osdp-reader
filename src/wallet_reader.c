// SPDX-License-Identifier: GPL-3.0-or-later
// C port of the independently tested STM32 protocol state machine.
#include "wallet_reader.h"
#include "wallet_crypto.h"
#include <string.h>
static uint32_t be32(const uint8_t *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 |
         p[3];
}
static void put32(uint8_t *p, uint32_t v) {
  for (int i = 3; i >= 0; --i) {
    p[i] = (uint8_t)v;
    v >>= 8;
  }
}
typedef struct {
  const uint8_t *name, *payload;
  size_t name_n, n;
  uint8_t tnf;
} Record;
typedef struct {
  const uint8_t *p;
  size_t n, used;
  bool first, ended, valid;
} Ndef;
static bool ndef_bad(Ndef *it) {
  it->valid = false;
  return false;
}
static bool ndef_next(Ndef *it, Record *r) {
  if (!it->valid || it->ended || it->used == it->n)
    return false;
  if (it->n - it->used < 3)
    return ndef_bad(it);
  const uint8_t h = it->p[it->used++], type_n = it->p[it->used++];
  if (((h & 0x80) != 0) != it->first || (h & 0x20) ||
      ((h & 7) != 1 && (h & 7) != 4))
    return ndef_bad(it);
  it->first = false;
  size_t size = 0;
  if (h & 0x10)
    size = it->p[it->used++];
  else {
    if (it->n - it->used < 4)
      return ndef_bad(it);
    size = be32(it->p + it->used);
    it->used += 4;
  }
  uint8_t id_n = 0;
  if (h & 8) {
    if (it->used == it->n)
      return ndef_bad(it);
    id_n = it->p[it->used++];
  }
  if (size > it->n - it->used ||
      (size_t)type_n + id_n > it->n - it->used - size)
    return ndef_bad(it);
  r->tnf = h & 7;
  r->name = it->p + it->used;
  r->name_n = type_n;
  if (r->tnf == 1) {
    r->name = it->p + it->used + type_n;
    r->name_n = id_n;
  }
  it->used += type_n + id_n;
  r->payload = it->p + it->used;
  r->n = size;
  it->used += size;
  it->ended = (h & 0x40) != 0;
  if ((it->ended && it->used != it->n) || (!it->ended && it->used == it->n))
    return ndef_bad(it);
  return true;
}
static bool ndef_complete(const Ndef *it) {
  return it->valid && it->ended && it->used == it->n;
}
static bool named(const Record *r, const char *name) {
  return r->name_n == strlen(name) && !memcmp(r->name, name, r->name_n);
}
bool wallet_ndef_find(const uint8_t *p, size_t n, const char *name,
                      const uint8_t **value, size_t *length) {
  if (!value || !length)
    return false;
  *value = NULL;
  *length = 0;
  if (!p || !name || !n)
    return false;
  Ndef it = {.p = p, .n = n, .first = true, .valid = true};
  Record r = {0};
  bool found = false;
  while (ndef_next(&it, &r))
    if (named(&r, name)) {
      if (found)
        return false;
      *value = r.payload;
      *length = r.n;
      found = true;
    }
  return found && ndef_complete(&it);
}
typedef struct {
  uint8_t *p;
  size_t cap, n, last;
  bool ok;
} Builder;
static bool builder_rec(Builder *b, const char *name, const uint8_t *v,
                        size_t len) {
  const size_t nn = strlen(name), head = len < 256 ? 3 : 6,
               total = head + nn + len;
  if (!b->ok || !v || nn > 255 || len > WALLET_MAX_RESPONSE ||
      total > b->cap - b->n)
    return b->ok = false;
  const bool first = b->n == 0;
  if (!first)
    b->p[b->last] &= (uint8_t)~0x40;
  b->last = b->n;
  b->p[b->n++] = (uint8_t)(0x44 | (first ? 0x80 : 0) | (len < 256 ? 0x10 : 0));
  b->p[b->n++] = (uint8_t)nn;
  if (len < 256)
    b->p[b->n++] = (uint8_t)len;
  else {
    put32(b->p + b->n, (uint32_t)len);
    b->n += 4;
  }
  memcpy(b->p + b->n, name, nn);
  b->n += nn;
  memcpy(b->p + b->n, v, len);
  b->n += len;
  return true;
}
static bool apdu(uint8_t cla, uint8_t ins, const uint8_t *body, size_t n,
                 uint8_t *out, size_t cap, size_t *used, uint8_t p1,
                 uint8_t p2) {
  *used = 0;
  if (!out || n > 245 || cap < n + 6 || (n && !body))
    return false;
  out[0] = cla;
  out[1] = ins;
  out[2] = p1;
  out[3] = p2;
  if (n) {
    out[4] = (uint8_t)n;
    memcpy(out + 5, body, n);
    out[5 + n] = 0;
    *used = n + 6;
  } else {
    out[4] = 0;
    *used = 5;
  }
  return true;
}
static bool select_aid(const uint8_t *aid, size_t n, uint8_t *out, size_t cap,
                       size_t *used) {
  return apdu(0, 0xa4, aid, n, out, cap, used, 4, 0);
}
static bool session_ok(const uint8_t *p, size_t n, const uint8_t id[8],
                       uint8_t seq) {
  const uint8_t *v = NULL;
  size_t size = 0;
  return wallet_ndef_find(p, n, "ses", &v, &size) && size == 10 &&
         !memcmp(v, id, 8) && v[8] == seq && v[9] == 1;
}
static bool service_value(const uint8_t *p, size_t n, wallet_result_t *out,
                          unsigned depth, unsigned *found) {
  if (depth > 4)
    return false;
  Ndef it = {.p = p, .n = n, .first = true, .valid = true};
  Record r = {0};
  while (ndef_next(&it, &r)) {
    if (named(&r, "n")) {
      if (++*found != 1 || r.n < 2 || r.n - 1 > WALLET_MAX_VALUE ||
          r.payload[0] > 5)
        return false;
      out->encoding = r.payload[0];
      out->length = r.n - 1;
      memcpy(out->value, r.payload + 1, out->length);
    } else if (named(&r, "asv") || named(&r, "ly") || named(&r, "gr") ||
               named(&r, "et") || named(&r, "fl") || named(&r, "gc") ||
               named(&r, "of") || named(&r, "pl") || named(&r, "tr")) {
      if (!service_value(r.payload, r.n, out, depth + 1, found))
        return false;
    }
  }
  return ndef_complete(&it);
}
bool wallet_tlv_find(const uint8_t *p, size_t n, uint32_t wanted,
                     const uint8_t **value, size_t *length) {
  if (!value || !length)
    return false;
  *value = NULL;
  *length = 0;
  if (!p)
    return false;
  bool found = false;
  size_t i = 0;
  while (i < n) {
    uint32_t tag = p[i++];
    if ((tag & 31) == 31) {
      unsigned bytes = 0;
      uint8_t t;
      do {
        if (i == n || ++bytes > 2)
          return false;
        t = p[i++];
        tag = (tag << 8) | t;
      } while (t & 128);
    }
    if (i == n)
      return false;
    size_t len = p[i++];
    if (len & 128) {
      unsigned c = len & 127;
      if (!c || c > 2 || n - i < c)
        return false;
      len = 0;
      if (p[i] == 0)
        return false;
      while (c--)
        len = (len << 8) | p[i++];
      if (len < 128)
        return false;
    }
    if (len > n - i)
      return false;
    if (tag == wanted) {
      if (found)
        return false;
      found = true;
      *value = p + i;
      *length = len;
    }
    i += len;
  }
  return found;
}
bool wallet_apple_open(const wallet_config_t *c, const wallet_hooks_t *h,
                       const uint8_t *blob, size_t n, wallet_result_t *out) {
  if (!out)
    return false;
  if (!c || !h) {
    memset(out, 0, sizeof(*out));
    return false;
  }
  *out = (wallet_result_t){0};
  if (!c->apple || !h->apple_ecdh || !blob || n < 56 ||
      n - 52 > WALLET_MAX_VALUE + 4 || memcmp(blob, c->apple_key_id, 4))
    return false;
  uint8_t encoded[33] = {2}, peer[65] = {0}, shared[32] = {0}, key[32] = {0},
          plain[WALLET_MAX_VALUE + 4] = {0};
  memcpy(encoded + 1, blob + 4,
         32); // Either Y parity gives the same ECDH X coordinate.
  bool ok = false;
  if (wallet_expand(encoded, sizeof(encoded), peer) == 0 &&
      h->apple_ecdh(h->user, peer, shared) == 0) {
    // Both published VAS derivations require a valid GCM tag. No plaintext
    // is accepted merely because one method fails and the other is tried.
    for (unsigned method = 1; method <= 2 && !ok; ++method) {
      const uint8_t *aad = method == 2 ? c->pass_hash : NULL;
      ok = wallet_vas_key(shared, c->pass_hash, method, key) == 0 &&
           wallet_vas_open(key, blob + 36, n - 36, aad, method == 2 ? 32 : 0,
                           plain, sizeof(plain)) == 0;
    }
  }
  if (ok) {
    out->provider = PROVIDER_APPLE;
    out->apple_timestamp = be32(plain);
    out->length = n - 56;
    out->encoding = 4;
    if (!out->length)
      ok = false;
    else
      memcpy(out->value, plain + 4, out->length);
  }
  wallet_wipe(peer, sizeof(peer));
  wallet_wipe(shared, sizeof(shared));
  wallet_wipe(key, sizeof(key));
  wallet_wipe(plain, sizeof(plain));
  if (!ok)
    *out = (wallet_result_t){0};
  return ok;
}
void wallet_reset(wallet_reader_t *r) {
  if (r)
    wallet_wipe(r, sizeof(*r));
}
static bool wallet_fail(wallet_reader_t *r, wallet_error_t e) {
  r->fault = e;
  r->state = WS_FAILED;
  r->waiting = false;
  memset(&r->result, 0, sizeof(r->result));
  wallet_wipe(r->keys, sizeof(r->keys));
  wallet_wipe(r->ephemeral, sizeof(r->ephemeral));
  wallet_wipe(r->data, sizeof(r->data));
  wallet_wipe(r->plain, sizeof(r->plain));
  return false;
}
void wallet_transport_error(wallet_reader_t *r) {
  if (r)
    wallet_fail(r, WALLET_TRANSPORT);
}
bool wallet_begin(wallet_reader_t *r, const wallet_config_t *c,
                  const wallet_hooks_t *h) {
  if (!r)
    return false;
  wallet_reset(r);
  if (!c || !h)
    return wallet_fail(r, WALLET_CONFIGURATION);
  r->config = c;
  r->hooks = *h;
  if ((!c->apple && !c->google) || !h->random || (c->apple && !h->apple_ecdh) ||
      (c->google && (!h->google_sign || !c->key_version_configured)))
    return wallet_fail(r, WALLET_CONFIGURATION);
  r->state = WS_OSE;
  return true;
}
static bool prepare_google(wallet_reader_t *r, const uint8_t nonce[32]) {
  uint8_t pub[65] = {0}, digest[32] = {0};
  put32(r->transcript + 64, r->config->collector_id);
  if (r->hooks.random(r->hooks.user, r->session, 8) ||
      r->hooks.random(r->hooks.user, r->transcript, 32) ||
      r->hooks.random(r->hooks.user, r->ephemeral, 32))
    return wallet_fail(r, WALLET_CRYPTO);
  memcpy(r->transcript + 32, nonce, 32);
  if (wallet_public(r->ephemeral, pub))
    return wallet_fail(r, WALLET_CRYPTO);
  r->transcript[68] = (uint8_t)(2 | (pub[64] & 1));
  memcpy(r->transcript + 69, pub + 1, 32);
  r->signature_n = sizeof(r->signature);
  if (wallet_sha256(r->transcript, sizeof(r->transcript), digest) ||
      r->hooks.google_sign(r->hooks.user, digest, r->signature,
                           &r->signature_n) ||
      r->signature_n < 8 || r->signature_n > 72)
    return wallet_fail(r, WALLET_CRYPTO);
  r->sequence = 0;
  r->state = WS_NEGOTIATE;
  return true;
}
bool wallet_next(wallet_reader_t *r, uint8_t *out, size_t cap, size_t *used) {
  if (!r || !used)
    return false;
  *used = 0;
  if (r->waiting || (r->state == WS_DONE) || (r->state == WS_FAILED) ||
      r->state == WS_IDLE)
    return false;
  bool ok = false;
  uint8_t a[220] = {0}, b[220] = {0}, d[220] = {0};
  static const uint8_t gst[] = {0xa0, 0, 0, 4, 0x76, 0xd0, 0, 1, 0x11};
  static const uint8_t ose[] = {'O', 'S', 'E', '.', 'V',
                                'A', 'S', '.', '0', '1'};
  if (r->state == WS_OSE)
    ok = select_aid(ose, sizeof(ose), out, cap, used);
  else if (r->state == WS_GOOGLE_SELECT)
    ok = select_aid(gst, sizeof(gst), out, cap, used);
  else if (r->state == WS_GET_APPLE) {
    const uint8_t prefix[] = {0x9f, 0x22, 2, 1, 0, 0x9f, 0x25, 32};
    memcpy(a, prefix, sizeof(prefix));
    memcpy(a + 8, r->config->pass_hash, 32);
    const uint8_t nonce_tag[] = {0x9f, 0x28, 4};
    memcpy(a + 40, nonce_tag, sizeof(nonce_tag));
    memcpy(a + 43, r->session, 4);
    const uint8_t suffix[] = {0x9f, 0x26, 4, 0, 0x80, 0, 2};
    memcpy(a + 47, suffix, sizeof(suffix));
    ok = apdu(0x80, 0xca, a, 54, out, cap, used, 1, 1);
  } else if (r->state == WS_MORE_GOOGLE)
    ok = apdu(0x90, 0xc0, NULL, 0, out, cap, used, 0, 0);
  else if (r->state == WS_NEGOTIATE) {
    Builder inner = {.p = a, .cap = sizeof(a), .ok = true};
    d[0] = 4;
    memcpy(d + 1, r->signature, r->signature_n);
    builder_rec(&inner, "sig", d, r->signature_n + 1);
    d[0] = 4;
    put32(d + 1, r->config->collector_id);
    builder_rec(&inner, "cld", d, 5);
    if (!inner.ok)
      return wallet_fail(r, WALLET_CAPACITY);
    memcpy(d, r->transcript, 32);
    d[32] = 1;
    memcpy(d + 33, r->transcript + 68, 33);
    put32(d + 66, r->config->key_version);
    memcpy(d + 70, a, inner.n);
    Builder mid = {.p = b, .cap = sizeof(b), .ok = true};
    memcpy(a, r->session, 8);
    a[8] = r->sequence;
    a[9] = 1;
    builder_rec(&mid, "ses", a, 10);
    builder_rec(&mid, "cpr", d, 70 + inner.n);
    a[0] = 0;
    a[1] = 1;
    memcpy(a + 2, b, mid.n);
    Builder outer = {.p = d, .cap = sizeof(d), .ok = true};
    builder_rec(&outer, "ngr", a, mid.n + 2);
    ok = mid.ok && outer.ok &&
         apdu(0x90, 0x53, d, outer.n, out, cap, used, 0, 0);
  } else if (r->state == WS_GET_GOOGLE) {
    Builder mid = {.p = b, .cap = sizeof(b), .ok = true};
    memcpy(a, r->session, 8);
    a[8] = r->sequence;
    a[9] = 1;
    builder_rec(&mid, "ses", a, 10);
    d[0] = 4;
    put32(d + 1, r->config->collector_id);
    Builder merchant = {.p = a, .cap = sizeof(a), .ok = true};
    builder_rec(&merchant, "cld", d, 5);
    builder_rec(&mid, "mer", a, merchant.n);
    const uint8_t type = 1;
    Builder list = {.p = a, .cap = sizeof(a), .ok = true};
    builder_rec(&list, "str", &type, 1);
    builder_rec(&mid, "slr", a, list.n);
    const uint8_t capabilities[] = {
        1, 0, 0, 0, 1}; // Standalone, pass-only. Zlib NOT advertised.
    builder_rec(&mid, "pcr", capabilities, sizeof(capabilities));
    a[0] = 0;
    a[1] = 1;
    memcpy(a + 2, b, mid.n);
    Builder outer = {.p = d, .cap = sizeof(d), .ok = true};
    builder_rec(&outer, "srq", a, mid.n + 2);
    ok = mid.ok && outer.ok &&
         apdu(0x90, 0x50, d, outer.n, out, cap, used, 0, 0);
  }
  if (!ok)
    return wallet_fail(r, WALLET_CAPACITY);
  r->waiting = true;
  return true;
}
static bool identify(wallet_reader_t *r) {
  uint8_t input[16 + 32 + 1 + WALLET_MAX_VALUE] = {0};
  static const char domain[] = "wallet-osdp-v1";
  size_t n = sizeof(domain) - 1;
  memcpy(input, domain, n);
  input[n++] = (uint8_t)(r->result.provider);
  if (r->result.provider == PROVIDER_APPLE) {
    memcpy(input + n, r->config->pass_hash, 32);
    n += 32;
  } else {
    put32(input + n, r->config->collector_id);
    n += 4;
  }
  input[n++] = r->result.encoding;
  memcpy(input + n, r->result.value, r->result.length);
  n += r->result.length;
  const int rc = wallet_sha256(input, n, r->result.credential);
  wallet_wipe(input, sizeof(input));
  if (rc)
    return wallet_fail(r, WALLET_CRYPTO);
  r->state = WS_DONE;
  wallet_wipe(r->ephemeral, sizeof(r->ephemeral));
  wallet_wipe(r->keys, sizeof(r->keys));
  wallet_wipe(r->data, sizeof(r->data));
  wallet_wipe(r->plain, sizeof(r->plain));
  return true;
}
static bool finish_apple(wallet_reader_t *r, const uint8_t *p, size_t n) {
  const uint8_t *outer, *blob;
  size_t outer_n, blob_n;
  if (!wallet_tlv_find(p, n, 0x70, &outer, &outer_n) ||
      !wallet_tlv_find(outer, outer_n, 0x9f27, &blob, &blob_n))
    return wallet_fail(r, WALLET_FORMAT);
  if (!wallet_apple_open(r->config, &r->hooks, blob, blob_n, &r->result))
    return wallet_fail(r, WALLET_AUTHENTICATION);
  return identify(r);
}
static bool finish_google(wallet_reader_t *r) {
  const uint8_t *outer, *bundle;
  size_t outer_n, bundle_n;
  if (!wallet_ndef_find(r->data, r->collected, "srs", &outer, &outer_n))
    return wallet_fail(r, WALLET_FORMAT);
  if (!session_ok(outer, outer_n, r->session, (uint8_t)(r->sequence + 1)))
    return wallet_fail(r, WALLET_SESSION);
  if (!wallet_ndef_find(outer, outer_n, "reb", &bundle, &bundle_n) ||
      bundle_n < 1)
    return wallet_fail(r, WALLET_FORMAT);
  if (bundle[0] != 1)
    return wallet_fail(r, WALLET_UNSUPPORTED); // Require encryption; reject
                                               // unnegotiated compression.
  if (bundle_n < 45 || wallet_gst_open(r->keys, bundle + 1, bundle_n - 1,
                                       r->plain, sizeof(r->plain)))
    return wallet_fail(r, WALLET_AUTHENTICATION);
  r->result.provider = PROVIDER_GOOGLE;
  unsigned found = 0;
  if (!service_value(r->plain, bundle_n - 45, &r->result, 0, &found) ||
      found != 1)
    return wallet_fail(r, WALLET_FORMAT);
  return identify(r);
}
bool wallet_feed(wallet_reader_t *r, const uint8_t *p, size_t n) {
  if (!r)
    return false;
  if (!r->waiting || !p || n < 2 || n > WALLET_MAX_RESPONSE)
    return wallet_fail(r, WALLET_STATE);
  r->waiting = false;
  r->status = (uint16_t)(p[n - 2]) << 8 | p[n - 1];
  n -= 2;
  if (r->status == 0x9001 || r->status == 0x6a83)
    return wallet_fail(r, WALLET_NO_PASS);
  if (r->status == 0x6287 || r->status == 0x9302)
    return wallet_fail(r, WALLET_USER_ACTION);
  bool more = r->status == 0x9091 || r->status == 0x9100;
  if (r->status != 0x9000 &&
      !(more && (r->state == WS_GET_GOOGLE || r->state == WS_MORE_GOOGLE)))
    return wallet_fail(r, WALLET_STATUS);
  const uint8_t *v = NULL, *inner = NULL;
  size_t size = 0, inner_n = 0;
  if (r->state == WS_OSE) {
    if (!wallet_tlv_find(p, n, 0x6f, &inner, &inner_n) ||
        !wallet_tlv_find(inner, inner_n, 0x50, &v, &size))
      return wallet_fail(r, WALLET_FORMAT);
    if (size == 8 && !memcmp(v, "ApplePay", 8)) {
      if (!r->config->apple)
        return wallet_fail(r, WALLET_CONFIGURATION);
      if (!wallet_tlv_find(inner, inner_n, 0x9f21, &v, &size) || size != 2 ||
          v[0] != 1 || v[1] != 0)
        return wallet_fail(r, WALLET_UNSUPPORTED);
      if (r->hooks.random(r->hooks.user, r->session, 4))
        return wallet_fail(r, WALLET_CRYPTO);
      r->state = WS_GET_APPLE;
    } else if (size == 10 && !memcmp(v, "AndroidPay", 10)) {
      if (!r->config->google)
        return wallet_fail(r, WALLET_CONFIGURATION);
      r->state = WS_GOOGLE_SELECT;
    } else {
      return wallet_fail(r, WALLET_UNSUPPORTED);
    }
    return true;
  }
  if (r->state == WS_GOOGLE_SELECT) {
    if (n < 4 || ((uint16_t)(p[0]) << 8 | p[1]) > 1 ||
        ((uint16_t)(p[2]) << 8 | p[3]) < 1)
      return wallet_fail(r, WALLET_UNSUPPORTED);
    if (!wallet_ndef_find(p + 4, n - 4, "mdn", &v, &size) || size != 33 ||
        v[0] != 4)
      return wallet_fail(r, WALLET_FORMAT);
    return prepare_google(r, v + 1);
  }
  if (r->state == WS_NEGOTIATE) {
    if (!wallet_ndef_find(p, n, "nrs", &inner, &inner_n))
      return wallet_fail(r, WALLET_FORMAT);
    if (!session_ok(inner, inner_n, r->session, (uint8_t)(r->sequence + 1)))
      return wallet_fail(r, WALLET_SESSION);
    if (!wallet_ndef_find(inner, inner_n, "dpk", &v, &size) || size != 33)
      return wallet_fail(r, WALLET_FORMAT);
    uint8_t peer[65] = {0}, shared[32] = {0};
    const bool ok = !wallet_expand(v, size, peer) &&
                    !wallet_ecdh(r->ephemeral, peer, shared) &&
                    !wallet_gst_keys(shared, v, r->transcript, r->signature,
                                     r->signature_n, r->keys);
    wallet_wipe(shared, sizeof(shared));
    wallet_wipe(r->ephemeral, sizeof(r->ephemeral));
    if (!ok)
      return wallet_fail(r, WALLET_CRYPTO);
    r->sequence = 1;
    r->collected = 0;
    r->state = WS_GET_GOOGLE;
    return true;
  }
  if (r->state == WS_GET_APPLE)
    return finish_apple(r, p, n);
  if (r->state == WS_GET_GOOGLE || r->state == WS_MORE_GOOGLE) {
    if (!n || ++r->fragments > 16 || n > sizeof(r->data) - r->collected)
      return wallet_fail(r, WALLET_CAPACITY);
    memcpy(r->data + r->collected, p, n);
    r->collected += n;
    if (more) {
      r->state = WS_MORE_GOOGLE;
      return true;
    }
    return finish_google(r);
  }
  return wallet_fail(r, WALLET_STATE);
}
