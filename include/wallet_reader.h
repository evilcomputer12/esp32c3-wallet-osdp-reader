// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define WALLET_MAX_RESPONSE 1024
#define WALLET_MAX_VALUE 256
#define WALLET_MAX_COMMAND 251
typedef enum {
  WALLET_NONE,
  WALLET_STATE,
  WALLET_CONFIGURATION,
  WALLET_FORMAT,
  WALLET_CAPACITY,
  WALLET_STATUS,
  WALLET_UNSUPPORTED,
  WALLET_CRYPTO,
  WALLET_AUTHENTICATION,
  WALLET_NO_PASS,
  WALLET_USER_ACTION,
  WALLET_SESSION,
  WALLET_TRANSPORT
} wallet_error_t;
typedef enum {
  PROVIDER_NONE,
  PROVIDER_APPLE,
  PROVIDER_GOOGLE
} wallet_provider_t;
typedef struct {
  bool apple, google;
  uint8_t pass_hash[32], apple_key_id[4];
  uint32_t collector_id, key_version;
  bool key_version_configured;
} wallet_config_t;
typedef struct {
  void *user;
  int (*random)(void *, uint8_t *, size_t);
  int (*apple_ecdh)(void *, const uint8_t[65], uint8_t[32]);
  int (*google_sign)(void *, const uint8_t[32], uint8_t *, size_t *);
} wallet_hooks_t;
typedef struct {
  wallet_provider_t provider;
  uint8_t value[WALLET_MAX_VALUE];
  size_t length;
  uint8_t encoding;
  uint32_t apple_timestamp;
  uint8_t credential[32];
} wallet_result_t;
typedef enum {
  WS_IDLE,
  WS_OSE,
  WS_GOOGLE_SELECT,
  WS_NEGOTIATE,
  WS_GET_GOOGLE,
  WS_MORE_GOOGLE,
  WS_GET_APPLE,
  WS_DONE,
  WS_FAILED
} wallet_state_t;
typedef struct {
  wallet_state_t state;
  const wallet_config_t *config;
  wallet_hooks_t hooks;
  bool waiting;
  wallet_error_t fault;
  uint16_t status;
  uint8_t session[8], transcript[101], ephemeral[32], signature[72], keys[48];
  size_t signature_n, collected;
  uint8_t sequence, fragments;
  uint8_t data[WALLET_MAX_RESPONSE], plain[WALLET_MAX_RESPONSE];
  wallet_result_t result;
} wallet_reader_t;
void wallet_reset(wallet_reader_t *r);
bool wallet_begin(wallet_reader_t *r, const wallet_config_t *c,
                  const wallet_hooks_t *h);
bool wallet_next(wallet_reader_t *r, uint8_t *out, size_t capacity,
                 size_t *used);
bool wallet_feed(wallet_reader_t *r, const uint8_t *response, size_t length);
void wallet_transport_error(wallet_reader_t *r);
bool wallet_apple_open(const wallet_config_t *, const wallet_hooks_t *,
                       const uint8_t *, size_t, wallet_result_t *);
bool wallet_tlv_find(const uint8_t *, size_t, uint32_t, const uint8_t **,
                     size_t *);
bool wallet_ndef_find(const uint8_t *, size_t, const char *, const uint8_t **,
                      size_t *);
