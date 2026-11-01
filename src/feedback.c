// SPDX-License-Identifier: GPL-3.0-or-later
#include "board.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "osdp/osdp_buz_state.h"
#include "osdp/osdp_commands.h"
#include "osdp/osdp_led_state.h"
#include "reader.h"
#include "reader_config.h"
#include <stdatomic.h>
#include <string.h>
static osdp_led_t led;
static osdp_buz_t buzzer;
static uint32_t beep_at;
static bool beep;
static atomic_bool pending_beep;
void reader_feedback_begin(void) {
  gpio_set_level(PIN_LED, 1);
  gpio_set_direction(PIN_LED, GPIO_MODE_INPUT_OUTPUT);
  gpio_set_level(PIN_BUZZER, 0);
  gpio_set_direction(PIN_BUZZER, reader_settings.buzzer_enabled
                                     ? GPIO_MODE_INPUT_OUTPUT
                                     : GPIO_MODE_INPUT);
}
void reader_feedback_report(void) {
  int led_pin = gpio_get_level(PIN_LED);
  int buzzer_pin = gpio_get_level(PIN_BUZZER);
  ESP_LOGI("feedback", "LED_GPIO8=%d pin=%d buzzer_GPIO3=%d buzzer_pin=%d "
                       "buzzer_enabled=%u", !led_pin, led_pin,
           reader_settings.buzzer_enabled && buzzer_pin, buzzer_pin,
           reader_settings.buzzer_enabled);
}
void reader_feedback_read(void) { atomic_store(&pending_beep, true); }
static bool color(uint8_t c) {
  return c == OSDP_LED_BLACK || c == OSDP_LED_GREEN;
}
int reader_feedback_command(uint8_t code, const uint8_t *data, size_t n,
                            bool authorized) {
  if (!authorized)
    return OSDP_ERR_INVALID_ARG;
  uint32_t now = reader_millis();
  if (code == OSDP_CMD_LED) {
    if (!data || !n || n % OSDP_LED_RECORD_BYTES)
      return OSDP_ERR_BAD_LENGTH;
    osdp_led_t next = led;
    for (size_t i = 0; i < n; i += OSDP_LED_RECORD_BYTES) {
      osdp_led_record_t r = {0};
      size_t count = 0;
      if (osdp_led_decode(data + i, OSDP_LED_RECORD_BYTES, &r, 1, &count) !=
          OSDP_OK)
        return OSDP_ERR_BAD_LENGTH;
      if (r.reader_no || r.led_no || r.temp_control_code > 2 ||
          r.perm_control_code > 1)
        return OSDP_ERR_INVALID_ARG;
      if (r.temp_control_code == OSDP_LED_TEMP_SET &&
          (!color(r.temp_on_color) || !color(r.temp_off_color)))
        return OSDP_ERR_INVALID_ARG;
      if (r.perm_control_code == OSDP_LED_PERM_SET &&
          (!color(r.perm_on_color) || !color(r.perm_off_color)))
        return OSDP_ERR_INVALID_ARG;
      osdp_led_apply(&next, &r, now);
    }
    led = next;
    return OSDP_OK;
  }
  if (code == OSDP_CMD_BUZ && reader_settings.buzzer_enabled) {
    osdp_buz_cmd_t b = {0};
    if (osdp_buz_decode(data, n, &b) != OSDP_OK)
      return OSDP_ERR_BAD_LENGTH;
    if (b.reader_no || (b.tone_code != OSDP_BUZ_TONE_OFF &&
                        b.tone_code != OSDP_BUZ_TONE_DEFAULT))
      return OSDP_ERR_INVALID_ARG;
    osdp_buz_apply(&buzzer, &b, now);
    beep = false;
    atomic_store(&pending_beep, false);
    return OSDP_OK;
  }
  return OSDP_ERR_INVALID_ARG;
}
void reader_feedback_tick(bool authorized) {
  uint32_t now = reader_millis();
  if (!authorized) {
    memset(&led, 0, sizeof(led));
    memset(&buzzer, 0, sizeof(buzzer));
    beep = false;
    atomic_store(&pending_beep, false);
  }
  if (atomic_exchange(&pending_beep, false) && authorized) {
    beep = true;
    beep_at = now;
  }
  if (beep && now - beep_at >= 100)
    beep = false;
  if (led.temp_active && now - led.temp_started_ms >= led.temp_duration_ms)
    led.temp_active = false;
  uint32_t cycle =
      ((uint32_t)buzzer.on_time_100ms + buzzer.off_time_100ms) * 100;
  if (buzzer.active && buzzer.count &&
      now - buzzer.started_ms >= cycle * buzzer.count)
    buzzer.active = false;
  gpio_set_level(PIN_LED, osdp_led_color(&led, now) == OSDP_LED_GREEN ? 0 : 1);
  if (reader_settings.buzzer_enabled)
    gpio_set_level(PIN_BUZZER, beep || osdp_buz_sounding(&buzzer, now));
}
