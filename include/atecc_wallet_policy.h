// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Inspect the configuration read at startup; this never changes the device.
// Offsets/bit meanings follow CryptoAuthLib's atecc608_config_t/calib_device.h.
// Support only unrestricted private P-256 slots, with no usage-counter policy.
static inline bool reader_atecc_wallet_policy(const uint8_t *config,
                                              size_t length, unsigned slot,
                                              bool ecdh) {
  if (!config || length != 128 || slot >= 16)
    return false;
  if (config[86] != 0 || config[87] != 0)
    return false; // Data/config locked.
  const unsigned slot_config =
      config[20 + slot * 2] | (unsigned)(config[21 + slot * 2]) << 8;
  const unsigned key_config =
      config[96 + slot * 2] | (unsigned)(config[97 + slot * 2]) << 8;
  if ((key_config & 0x1d) != 0x11)
    return false; // Private, KeyType = P-256.
  if (key_config & 0x1080)
    return false; // ReqAuth or PersistentDisable.
  if (!(slot_config & 0x80) || (slot_config & 0x20))
    return false;
  if (!(slot_config & (ecdh ? 0x04 : 0x01)))
    return false;
  // ECDHPROT is bits 8..9 of ChipOptions (little endian, bytes 90..91).
  // Only 0 lets mode 0x0c return clear output. IO-protected output is
  // unsupported.
  return !ecdh || !(config[91] & 0x03);
}
