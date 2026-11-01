// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "wallet_reader.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
uint32_t reader_millis(void);
bool reader_crypto_selftest(void);
bool reader_nfc_ready(void);
bool reader_atecc_begin(void);
bool reader_atecc_ready(void);
int reader_rng_block(unsigned char *, unsigned int);
int reader_atecc_verify(const uint8_t[65], const uint8_t[32],
                        const uint8_t[64]);
int reader_atecc_wallet_sign(unsigned, const uint8_t[32], uint8_t[64]);
int reader_atecc_wallet_ecdh(unsigned, const uint8_t[65], uint8_t[32]);
bool reader_pd_begin(void);
bool reader_pd_secure(void);
void reader_pd_report(void);
unsigned reader_pd_generation(void);
bool reader_enqueue(const uint8_t *, size_t, uint16_t, unsigned);
void reader_pd_task(void *);
void reader_nfc_task(void *);
void reader_console_task(void *);
void reader_feedback_begin(void);
void reader_feedback_read(void);
void reader_feedback_tick(bool);
int reader_feedback_command(uint8_t, const uint8_t *, size_t, bool);
bool reader_wallet_begin(void);
const wallet_config_t *reader_wallet_config(void);
wallet_hooks_t reader_wallet_hooks(void);
bool reader_configured(void);
const uint8_t *reader_scbk(void);
void reader_report(void);
