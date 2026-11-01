// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Native USB uses GPIO18/19. Leave strap pins GPIO2/8/9 free of external loads.
#define PIN_ATECC_SDA 0
#define PIN_ATECC_SCL 1
#define PIN_PN532_TX 4
#define PIN_PN532_RX 5
#define PIN_RS485_TX 6
#define PIN_RS485_RX 7
#define PIN_RS485_DE 10
#define PIN_LED 8
#define PIN_BUZZER 3
#define PN532_UART 1
#define OSDP_UART 0
#define OSDP_BAUD 9600
