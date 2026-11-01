// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
  void *user;
  int (*read)(void *, uint8_t *, size_t, uint32_t);
  bool (*write)(void *, const uint8_t *, size_t);
  void (*drain)(void *);
  uint32_t (*now)(void *);
  void (*delay)(void *, uint32_t);
} pn532_io_t;
typedef struct {
  pn532_io_t io;
  uint8_t target;
  bool resync;
  int error, last_status;
  uint32_t firmware;
} pn532_t;
int pn532_exchange(pn532_t *, const uint8_t *, size_t, uint8_t *, size_t,
                   uint32_t);
bool pn532_begin(pn532_t *, const pn532_io_t *);
int pn532_discover(pn532_t *, uint8_t uid[7], size_t *uid_n);
int pn532_presence(pn532_t *);
bool pn532_restart_discovery(pn532_t *);
bool pn532_apdu(pn532_t *, const uint8_t *, size_t, uint8_t *, size_t *);
