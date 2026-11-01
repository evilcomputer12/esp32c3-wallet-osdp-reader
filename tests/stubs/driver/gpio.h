// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
typedef enum { GPIO_MODE_INPUT, GPIO_MODE_INPUT_OUTPUT } gpio_mode_t;
int gpio_set_level(int pin, uint32_t level);
int gpio_set_direction(int pin, gpio_mode_t mode);
int gpio_get_level(int pin);
