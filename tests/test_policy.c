// SPDX-License-Identifier: GPL-3.0-or-later
#include "atecc_wallet_policy.h"
#include "presentation.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
  uint8_t cfg[128] = {0};
  cfg[20] = 0x85;
  cfg[96] = 0x11;
  assert(reader_atecc_wallet_policy(cfg, 128, 0, true));
  assert(reader_atecc_wallet_policy(cfg, 128, 0, false));
  cfg[91] = 1;
  assert(!reader_atecc_wallet_policy(cfg, 128, 0, true));
  cfg[91] = 0;
  cfg[86] = 0x55;
  assert(!reader_atecc_wallet_policy(cfg, 128, 0, false));
  cfg[86] = 0;
  cfg[20] |= 0x20;
  assert(!reader_atecc_wallet_policy(cfg, 128, 0, false));
  cfg[20] &= ~0x20;
  cfg[96] |= 0x80;
  assert(!reader_atecc_wallet_policy(cfg, 128, 0, false));
  assert(!reader_atecc_wallet_policy(cfg, 128, 16, false));
  presentation_t p = {0};
  uint8_t uid[] = {1, 2, 3, 4}, credential[32] = {5};
  assert(presentation_found(&p, uid, 4));
  presentation_report(&p, credential);
  assert(presentation_duplicate(&p, credential));
  assert(!presentation_timeout(&p));
  assert(!presentation_timeout(&p));
  assert(presentation_timeout(&p));
  assert(p.held && p.reported && p.refind);
  assert(!presentation_found(&p, uid, 4));
  assert(presentation_duplicate(&p, credential));
  p.refind = true;
  presentation_absent(&p, 100);
  presentation_absent(&p, 500);
  presentation_absent(&p, 900);
  assert(p.held);
  presentation_clear_absence(&p);
  presentation_absent(&p, 1000);
  presentation_absent(&p, 1600);
  assert(p.held);
  presentation_absent(&p, 2100);
  assert(!p.held && !p.reported);
  assert(presentation_found(&p, uid, 4) &&
         !presentation_duplicate(&p, credential));
  puts("ATECC slot policy, held-card suppression and clean removal passed");
  return 0;
}
