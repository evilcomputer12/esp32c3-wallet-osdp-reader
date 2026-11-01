// SPDX-License-Identifier: GPL-3.0-or-later
// PN532 HSU protocol; cold-start order follows Adafruit's wake + SAMConfig.
// Framing follows NXP PN532 User Manual, normal/extended frame sections.
#include "pn532.h"
#include <string.h>
static const uint8_t ack[] = {0, 0, 255, 0, 255, 0};
static int fail(pn532_t *p, int e) {
  p->error = e;
  p->resync = true;
  return e;
}
static bool receive(pn532_t *p, uint8_t *out, size_t n, uint32_t started,
                    uint32_t timeout) {
  size_t used = 0;
  while (used < n) {
    uint32_t elapsed = p->io.now(p->io.user) - started;
    if (elapsed >= timeout)
      return false;
    int got = p->io.read(p->io.user, out + used, n - used, timeout - elapsed);
    if (got <= 0 || (size_t)got > n - used)
      return false;
    used += (size_t)got;
  }
  return true;
}
int pn532_exchange(pn532_t *p, const uint8_t *cmd, size_t n, uint8_t *out,
                   size_t cap, uint32_t timeout) {
  if (!p || !cmd || !n || n > 253 || !out || !timeout)
    return -3;
  p->error = 0;
  if (p->resync) {
    if (!p->io.write(p->io.user, ack, sizeof(ack)))
      return fail(p, -2);
    p->io.delay(p->io.user, 5);
    p->resync = false;
  }
  p->io.drain(p->io.user);
  uint8_t frame[261] = {0,   0, 255, (uint8_t)(n + 1), (uint8_t)(0 - (n + 1)),
                        0xd4};
  memcpy(frame + 6, cmd, n);
  uint8_t sum = 0xd4;
  for (size_t i = 0; i < n; ++i)
    sum += cmd[i];
  frame[6 + n] = (uint8_t)(0 - sum);
  frame[7 + n] = 0;
  uint32_t started = p->io.now(p->io.user);
  if (!p->io.write(p->io.user, frame, n + 8))
    return fail(p, -2);
  uint8_t got_ack[6], prefix[5], type[2], suffix[2];
  if (!receive(p, got_ack, 6, started, timeout) || memcmp(got_ack, ack, 6))
    return fail(p, -2);
  if (!receive(p, prefix, 5, started, timeout))
    return fail(p, -2);
  if (prefix[0] || prefix[1] || prefix[2] != 255)
    return fail(p, -3);
  unsigned size = prefix[3];
  if (size == 255) {
    uint8_t len[3];
    if (prefix[4] != 255)
      return fail(p, -3);
    if (!receive(p, len, 3, started, timeout))
      return fail(p, -2);
    if ((uint8_t)(len[0] + len[1] + len[2]))
      return fail(p, -3);
    size = (unsigned)len[0] * 256 + len[1];
  } else if ((uint8_t)(size + prefix[4]))
    return fail(p, -3);
  if (size < 2 || size > 265 || size - 2 > cap)
    return fail(p, -4);
  if (!receive(p, type, 2, started, timeout))
    return fail(p, -2);
  if (type[0] != 0xd5 || type[1] != (uint8_t)(cmd[0] + 1))
    return fail(p, -3);
  if (!receive(p, out, size - 2, started, timeout) ||
      !receive(p, suffix, 2, started, timeout))
    return fail(p, -2);
  sum = type[0] + type[1];
  for (unsigned i = 0; i < size - 2; ++i)
    sum += out[i];
  if ((uint8_t)(sum + suffix[0]) || suffix[1])
    return fail(p, -3);
  return (int)size - 2;
}
static bool simple(pn532_t *p, const uint8_t *cmd, size_t n) {
  uint8_t response[1];
  return pn532_exchange(p, cmd, n, response, sizeof(response), 1000) == 0;
}
bool pn532_begin(pn532_t *p, const pn532_io_t *io) {
  if (!p || !io || !io->read || !io->write || !io->drain || !io->now ||
      !io->delay)
    return false;
  memset(p, 0, sizeof(*p));
  p->io = *io;
  const uint8_t wake[] = {0x55, 0, 0}, sam[] = {0x14, 1, 0x14, 1}, fw[] = {2},
                retry[] = {0x32, 5, 0xff, 1, 0};
  p->io.drain(p->io.user);
  if (!p->io.write(p->io.user, wake, sizeof(wake)))
    return false;
  p->io.delay(p->io.user, 2);
  if (!simple(p, sam, sizeof(sam)) && !simple(p, sam, sizeof(sam)))
    return false;
  uint8_t data[4];
  if (pn532_exchange(p, fw, 1, data, sizeof(data), 1000) != 4 ||
      data[0] != 0x32)
    return false;
  p->firmware = (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 |
                (uint32_t)data[2] << 8 | data[3];
  return simple(p, retry, sizeof(retry)) && pn532_restart_discovery(p);
}
bool pn532_restart_discovery(pn532_t *p) {
  const uint8_t release[] = {0x52, 0}, off[] = {0x32, 1, 0},
                on[] = {0x32, 1, 1};
  uint8_t response[1];
  (void)pn532_exchange(p, release, 2, response, 1, 1000);
  p->target = 0;
  if (!simple(p, off, sizeof(off)))
    return false;
  p->io.delay(p->io.user, 20);
  return simple(p, on, sizeof(on));
}
int pn532_discover(pn532_t *p, uint8_t uid[7], size_t *uid_n) {
  if (!uid || !uid_n)
    return -3;
  *uid_n = 0;
  const uint8_t cmd[] = {0x4a, 1, 0};
  uint8_t response[64];
  int n = pn532_exchange(p, cmd, 3, response, sizeof(response), 250);
  if (n < 0)
    return n;
  if (n == 1 && response[0] == 0)
    return 0;
  if (n < 6 || response[0] != 1 || !response[1] || !response[5] ||
      response[5] > 7 || n < 6 + response[5])
    return -3;
  if (!(response[4] & 0x20))
    return -5;
  p->target = response[1];
  *uid_n = response[5];
  memcpy(uid, response + 6, *uid_n);
  return 1;
}
int pn532_presence(pn532_t *p) {
  const uint8_t cmd[] = {0, 6};
  uint8_t response[1];
  int n = pn532_exchange(p, cmd, 2, response, 1, 1000);
  return n == 1 ? response[0] : (n < 0 ? n : -3);
}
bool pn532_apdu(pn532_t *p, const uint8_t *data, size_t length, uint8_t *out,
                size_t *out_n) {
  if (!p || !out_n)
    return false;
  size_t cap = *out_n;
  *out_n = 0;
  p->last_status = -1;
  if (!p->target || !data || !length || length > 251 || !out)
    return false;
  uint8_t cmd[253] = {0x40, p->target}, response[263];
  memcpy(cmd + 2, data, length);
  size_t used = 0, cmd_n = length + 2;
  uint32_t started = p->io.now(p->io.user);
  for (unsigned chunks = 0; chunks < 16; ++chunks) {
    uint32_t elapsed = p->io.now(p->io.user) - started;
    if (elapsed >= 1500)
      return false;
    int n = pn532_exchange(p, cmd, cmd_n, response, sizeof(response),
                           1500 - elapsed);
    if (n < 1) {
      p->last_status = n < 0 ? n : -3;
      return false;
    }
    p->last_status = response[0];
    if ((response[0] & 0xbf) || (size_t)(n - 1) > cap - used ||
        (n == 1 && (response[0] & 0x40)))
      return false;
    memcpy(out + used, response + 1, n - 1);
    used += n - 1;
    if (!(response[0] & 0x40)) {
      *out_n = used;
      return true;
    }
    cmd_n = 2;
  }
  return false;
}
