// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
  bool scbk_configured, apple_enabled, google_enabled, google_key_version_set,
      buzzer_enabled;
  uint8_t scbk[16];
  char apple_pass_type[256];
  int apple_slot,
      google_slot; // -1 = software key; 0..15 = existing ATECC slot.
  uint8_t apple_private[32], google_private[32], apple_public[65];
  uint32_t google_collector, google_key_version;
} reader_settings_t;
extern reader_settings_t reader_settings;
void reader_config_init(void);
