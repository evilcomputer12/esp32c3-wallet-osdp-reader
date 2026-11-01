// SPDX-License-Identifier: GPL-3.0-or-later
#include "pn532.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {
  uint8_t rx[4096], payload[263];
  size_t used, pos, n;
  unsigned writes, mode, step;
  uint32_t clock;
  bool extended;
} Mock;
static void add(Mock *m, const uint8_t *p, size_t n) {
  assert(m->used + n <= sizeof(m->rx));
  memcpy(m->rx + m->used, p, n);
  m->used += n;
}
static bool write_mock(void *u, const uint8_t *p, size_t n) {
  Mock *m = u;
  ++m->writes;
  m->used = m->pos = 0;
  if (n == 3) {
    assert(p[0] == 0x55);
    return true;
  }
  if (n == 6)
    return true; // Recovery ACK.
  assert(n >= 9 && p[0] == 0 && p[1] == 0 && p[2] == 255 && p[5] == 0xd4);
  assert((uint8_t)(p[3] + p[4]) == 0 && p[3] + 7 == n);
  uint8_t sum = 0;
  for (size_t i = 5; i < n - 1; ++i)
    sum += p[i];
  assert(sum == 0 && p[n - 1] == 0);
  if (m->mode == 6) {
    static const uint8_t order[] = {0x14, 2, 0x32, 0x52, 0x32, 0x32};
    assert(m->step < sizeof(order) && p[6] == order[m->step]);
    m->n = 0;
    if (m->step == 1) {
      const uint8_t firmware[] = {0x32, 1, 6, 7};
      memcpy(m->payload, firmware, 4);
      m->n = 4;
    }
    if (m->step == 3) {
      m->payload[0] = 0;
      m->n = 1;
    }
    ++m->step;
  } else if (m->mode == 7) {
    assert(p[6] == 0x40 && m->step < 2);
    assert(n == (m->step == 0 ? 11 : 10)); // APDU then MI continuation.
    m->n = 3;
    m->payload[0] = m->step == 0 ? 0x40 : 0;
    m->payload[1] = m->step == 0 ? 0x12 : 0x90;
    m->payload[2] = m->step == 0 ? 0x34 : 0;
    ++m->step;
  }
  uint8_t ack[] = {0, 0, 255, 0, 255, 0};
  if (m->mode == 1)
    ack[3] = 1;
  add(m, ack, 6);
  size_t size = m->n + 2;
  uint8_t h[] = {0, 0, 255, 0, 0};
  if (m->extended) {
    h[3] = h[4] = 255;
    add(m, h, 5);
    uint8_t len[] = {(uint8_t)(size >> 8), (uint8_t)size,
                     (uint8_t)(0 - (size >> 8) - (size & 255))};
    add(m, len, 3);
  } else {
    h[3] = (uint8_t)size;
    h[4] = (uint8_t)(0 - size);
    if (m->mode == 2)
      h[4] ^= 1;
    add(m, h, 5);
  }
  uint8_t type[] = {0xd5, (uint8_t)(p[6] + 1)};
  if (m->mode == 3)
    type[1] ^= 1;
  add(m, type, 2);
  add(m, m->payload, m->n);
  sum = type[0] + type[1];
  for (size_t i = 0; i < m->n; ++i)
    sum += m->payload[i];
  uint8_t tail[] = {(uint8_t)(0 - sum), 0};
  if (m->mode == 4)
    tail[0] ^= 1;
  add(m, tail, 2);
  if (m->mode == 5)
    --m->used;
  return true;
}
static int read_mock(void *u, uint8_t *out, size_t n, uint32_t timeout) {
  Mock *m = u;
  (void)timeout;
  ++m->clock;
  if (n > 3)
    n = 3;
  if (n > m->used - m->pos)
    n = m->used - m->pos;
  memcpy(out, m->rx + m->pos, n);
  m->pos += n;
  return (int)n;
}
static void drain_mock(void *u) { (void)u; }
static uint32_t now_mock(void *u) { return ((Mock *)u)->clock; }
static void delay_mock(void *u, uint32_t ms) { ((Mock *)u)->clock += ms; }
int main(void) {
  Mock m = {.payload = {0x32, 1, 6, 7}, .n = 4};
  pn532_t p = {
      .io = {&m, read_mock, write_mock, drain_mock, now_mock, delay_mock}};
  const uint8_t cmd[] = {2};
  uint8_t out[263];
  assert(pn532_exchange(&p, cmd, 1, out, sizeof(out), 1000) == 4 &&
         !memcmp(out, m.payload, 4));
  for (unsigned mode = 1; mode <= 5; ++mode) {
    m.mode = mode;
    assert(pn532_exchange(&p, cmd, 1, out, sizeof(out), 1000) < 0 && p.resync);
  }
  m.mode = 0;
  m.extended = true;
  m.n = 263;
  memset(m.payload, 0xa5, m.n);
  assert(pn532_exchange(&p, cmd, 1, out, sizeof(out), 1000) == 263 &&
         !memcmp(out, m.payload, 263));
  assert(pn532_exchange(&p, cmd, 1, out, 262, 1000) == -4);
  p.target = 1;
  m.n = 5;
  m.extended = false;
  m.payload[0] = 0;
  m.payload[1] = 1;
  m.payload[2] = 2;
  m.payload[3] = 0x90;
  m.payload[4] = 0;
  size_t n = sizeof(out);
  assert(pn532_apdu(&p, cmd, 1, out, &n) && n == 4 && out[2] == 0x90);
  m.payload[0] = 1;
  n = sizeof(out);
  assert(!pn532_apdu(&p, cmd, 1, out, &n) && n == 0);
  m.payload[0] = 0x40;
  m.n = 1;
  n = sizeof(out);
  assert(!pn532_apdu(&p, cmd, 1, out, &n) && n == 0);
  m.payload[0] = 0x40;
  m.n = 2;
  n = sizeof(out);
  assert(!pn532_apdu(&p, cmd, 1, out, &n) &&
         n == 0); // Infinite MI rejected at 16 chunks.
  assert(!pn532_apdu(&p, cmd, 252, out, &n));
  m.mode = 7;
  m.step = 0;
  n = sizeof(out);
  assert(pn532_apdu(&p, cmd, 1, out, &n) && n == 4 && m.step == 2);
  const uint8_t combined[] = {0x12, 0x34, 0x90, 0};
  assert(!memcmp(out, combined, 4));
  m.mode = 6;
  m.step = 0;
  pn532_io_t io = p.io;
  assert(pn532_begin(&p, &io) && m.step == 6 && p.firmware == 0x32010607);
  puts(
      "PN532 frame, checksum, partial read, extended length, capacity, status "
      "and MI bounds, successful chaining and cold-start command order passed");
  return 0;
}
