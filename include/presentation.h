// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef struct {
  bool held, refind, reported;
  uint8_t uid[7], uid_n, misses, absent_samples, credential[32];
  uint32_t absent_since, reselections;
} presentation_t;
static inline void presentation_clear_absence(presentation_t *p) {
  p->misses = 0;
  p->absent_samples = 0;
}
static inline bool presentation_timeout(presentation_t *p) {
  if (++p->misses < 3)
    return false;
  p->misses = 0;
  p->absent_samples = 0;
  p->refind = true;
  return true;
}
static inline void presentation_absent(presentation_t *p, uint32_t now) {
  if (!p->held || !p->refind)
    return;
  if (!p->absent_samples)
    p->absent_since = now;
  if (p->absent_samples < 255)
    ++p->absent_samples;
  if (p->absent_samples >= 3 && now - p->absent_since >= 1000) {
    p->held = p->refind = p->reported = false;
    p->uid_n = 0;
    presentation_clear_absence(p);
    memset(p->credential, 0, 32);
  }
}
static inline bool presentation_found(presentation_t *p, const uint8_t *uid,
                                      size_t n) {
  if (!uid || !n || n > 7) {
    presentation_clear_absence(p);
    return false;
  }
  bool same = p->held && p->uid_n == n && !memcmp(p->uid, uid, n);
  if (same)
    ++p->reselections;
  memcpy(p->uid, uid, n);
  p->uid_n = (uint8_t)n;
  p->held = true;
  p->refind = false;
  presentation_clear_absence(p);
  return !same;
}
static inline bool presentation_duplicate(const presentation_t *p,
                                          const uint8_t credential[32]) {
  return p->reported && !memcmp(p->credential, credential, 32);
}
static inline void presentation_report(presentation_t *p,
                                       const uint8_t credential[32]) {
  memcpy(p->credential, credential, 32);
  p->reported = true;
}
