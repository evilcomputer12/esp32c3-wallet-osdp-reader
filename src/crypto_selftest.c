// SPDX-License-Identifier: GPL-3.0-or-later
#include "reader.h"
#include "wallet_crypto.h"
#include <string.h>
#include <wolfssl/wolfcrypt/aes.h>

// FIPS 197 Appendix C. Run before starting concurrent crypto users. On the
// ESP32-C3 this also initializes and checks the wolfCrypt hardware AES path.
bool reader_crypto_selftest(void) {
  static const uint8_t plain[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
                                    0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
                                    0xcc, 0xdd, 0xee, 0xff};
  static const uint8_t cipher[2][16] = {
      {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30, 0xd8, 0xcd, 0xb7, 0x80,
       0x70, 0xb4, 0xc5, 0x5a},
      {0x8e, 0xa2, 0xb7, 0xca, 0x51, 0x67, 0x45, 0xbf, 0xea, 0xfc, 0x49, 0x90,
       0x4b, 0x49, 0x60, 0x89}};
  static const uint8_t sha[32] = {
      0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
      0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
      0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
  uint8_t key[32], block[32];
  for (unsigned i = 0; i < sizeof(key); ++i)
    key[i] = (uint8_t)i;
  bool ok = true;
  for (unsigned i = 0; i < 2 && ok; ++i) {
    Aes aes;
    if (wc_AesInit(&aes, NULL, INVALID_DEVID))
      return false;
    unsigned n = i ? 32 : 16;
    ok = wc_AesSetKey(&aes, key, n, NULL, AES_ENCRYPTION) == 0 &&
         wc_AesEcbEncrypt(&aes, block, plain, 16) == 0 &&
         memcmp(block, cipher[i], 16) == 0 &&
         wc_AesSetKey(&aes, key, n, NULL, AES_DECRYPTION) == 0 &&
         wc_AesEcbDecrypt(&aes, block, cipher[i], 16) == 0 &&
         memcmp(block, plain, 16) == 0;
    wc_AesFree(&aes);
  }
  ok = ok && wallet_sha256((const uint8_t *)"abc", 3, block) == 0 &&
       memcmp(block, sha, 32) == 0;
  wallet_wipe(key, sizeof(key));
  wallet_wipe(block, sizeof(block));
  return ok;
}
