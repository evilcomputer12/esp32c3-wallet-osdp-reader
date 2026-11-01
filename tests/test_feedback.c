// SPDX-License-Identifier: GPL-3.0-or-later
// Compile the real feedback module with a clock and GPIO boundary stub.
#include "board.h"
#include "driver/gpio.h"
#include "osdp/osdp_buz_state.h"
#include "osdp/osdp_led_state.h"
#include "reader.h"
#include "reader_config.h"
#include <assert.h>
#include <stdio.h>
reader_settings_t reader_settings;
static uint32_t clock_ms;
static int levels[11];
static gpio_mode_t modes[11];
uint32_t reader_millis(void) { return clock_ms; }
int gpio_set_level(int pin, uint32_t level) {
  assert(pin == PIN_LED || pin == PIN_BUZZER);
  levels[pin] = (int)level;
  return 0;
}
int gpio_set_direction(int pin, gpio_mode_t mode) {
  assert(pin == PIN_LED || pin == PIN_BUZZER);
  modes[pin] = mode;
  return 0;
}
int gpio_get_level(int pin) { return levels[pin]; }
static void outputs(bool light, bool sound) {
  assert(levels[PIN_LED] == !light); // Onboard LED is active-low.
  assert(levels[PIN_BUZZER] == sound);
}
static int led_command(bool temporary, uint8_t color, bool authorized) {
  osdp_led_record_t r = {0};
  if (temporary) {
    r.temp_control_code = OSDP_LED_TEMP_SET;
    r.temp_on_color = color;
    r.temp_timer_100ms = 5;
  } else {
    r.temp_control_code = OSDP_LED_TEMP_CANCEL;
    r.perm_control_code = OSDP_LED_PERM_SET;
    r.perm_on_color = color;
  }
  uint8_t data[OSDP_LED_RECORD_BYTES];
  size_t n;
  assert(osdp_led_build(&r, 1, data, sizeof(data), &n) == OSDP_OK);
  return reader_feedback_command(OSDP_CMD_LED, data, n, authorized);
}
static int buz_command(uint8_t tone, bool authorized) {
  const osdp_buz_cmd_t b = {0, tone, 1, 1, 2};
  uint8_t data[OSDP_BUZ_PAYLOAD_BYTES];
  size_t n;
  assert(osdp_buz_build(&b, data, sizeof(data), &n) == OSDP_OK);
  return reader_feedback_command(OSDP_CMD_BUZ, data, n, authorized);
}
int main(void) {
  reader_settings.buzzer_enabled = true;
  reader_feedback_begin();
  outputs(false, false);
  assert(modes[PIN_LED] == GPIO_MODE_INPUT_OUTPUT);
  assert(modes[PIN_BUZZER] == GPIO_MODE_INPUT_OUTPUT);
  reader_feedback_read();
  reader_feedback_tick(false);
  reader_feedback_tick(true);
  outputs(false, false); // No beep from a previous unsecured/session state.
  assert(led_command(false, OSDP_LED_GREEN, false) != OSDP_OK);
  assert(buz_command(OSDP_BUZ_TONE_DEFAULT, false) != OSDP_OK);
  reader_feedback_read();
  reader_feedback_tick(true);
  outputs(false, true); // Reading alone never lights the access indicator.
  clock_ms = 99; reader_feedback_tick(true); outputs(false, true);
  clock_ms = 100; reader_feedback_tick(true); outputs(false, false);

  reader_feedback_read();
  assert(buz_command(OSDP_BUZ_TONE_OFF, true) == OSDP_OK);
  reader_feedback_tick(true); outputs(false, false);
  assert(led_command(false, OSDP_LED_GREEN, true) == OSDP_OK);
  reader_feedback_tick(true); outputs(true, false);
  assert(led_command(false, OSDP_LED_RED, true) != OSDP_OK);
  reader_feedback_tick(true); outputs(true, false); // Invalid command is atomic.
  reader_feedback_read(); reader_feedback_tick(true); outputs(true, true);
  clock_ms += 100; reader_feedback_tick(true); outputs(true, false);
  assert(led_command(false, OSDP_LED_BLACK, true) == OSDP_OK);
  reader_feedback_tick(true); outputs(false, false);

  assert(led_command(true, OSDP_LED_GREEN, true) == OSDP_OK);
  assert(buz_command(OSDP_BUZ_TONE_DEFAULT, true) == OSDP_OK);
  reader_feedback_tick(true); outputs(true, true);
  clock_ms += 100; reader_feedback_tick(true); outputs(true, false);
  clock_ms += 100; reader_feedback_tick(true); outputs(true, true);
  clock_ms += 200; reader_feedback_tick(true); outputs(true, false);
  clock_ms += 100; reader_feedback_tick(true); outputs(false, false);

  clock_ms = UINT32_MAX - 50;
  reader_feedback_read(); reader_feedback_tick(true); outputs(false, true);
  clock_ms += 100; reader_feedback_tick(true); outputs(false, false);
  reader_feedback_read();
  assert(led_command(false, OSDP_LED_GREEN, true) == OSDP_OK);
  reader_feedback_tick(true); outputs(true, true);
  reader_feedback_tick(false); outputs(false, false);
  reader_feedback_tick(true); outputs(false, false);

  reader_settings.buzzer_enabled = false;
  reader_feedback_begin();
  assert(modes[PIN_BUZZER] == GPIO_MODE_INPUT);
  reader_feedback_read(); reader_feedback_tick(true); outputs(false, false);
  assert(buz_command(OSDP_BUZ_TONE_DEFAULT, true) != OSDP_OK);
  assert(led_command(false, OSDP_LED_GREEN, true) == OSDP_OK);
  reader_feedback_tick(true); outputs(true, false);
  reader_feedback_report();
  puts("Feedback timing, controller-only LED, invalid commands, disconnect and wraparound passed");
}
