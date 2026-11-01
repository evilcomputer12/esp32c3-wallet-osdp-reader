// SPDX-License-Identifier: GPL-3.0-or-later
#include "board.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "reader.h"
#include "reader_config.h"
uint32_t reader_millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
void reader_console_task(void *unused) {
  (void)unused;
  usb_serial_jtag_driver_config_t c = {.tx_buffer_size = 512,
                                       .rx_buffer_size = 256};
  if (usb_serial_jtag_driver_install(&c) != ESP_OK)
    vTaskDelete(NULL);
  for (;;) {
    uint8_t ch;
    if (usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(250)) == 1) {
      if (ch == 's' || ch == 'w' || ch == 'r')
        reader_report();
      else if (ch == 'h' || ch == '?')
        ESP_LOGI(
            "console",
            "s: status; w: wallet/reader status. No provisioning commands.");
    }
  }
}
void app_main(void) {
  reader_config_init();
  // Keep outputs idle even if crypto/ATECC initialization cannot finish.
  gpio_set_level(PIN_RS485_DE, 0);
  gpio_set_direction(PIN_RS485_DE, GPIO_MODE_OUTPUT);
  reader_feedback_begin();
  uint32_t flash = 0;
  esp_flash_get_size(NULL, &flash);
  ESP_LOGI("reader", "ESP32-C3 C reader: Flash=%lu bytes, free_heap=%lu",
           (unsigned long)flash, (unsigned long)esp_get_free_heap_size());
  configASSERT(xTaskCreate(reader_console_task, "console", 4096, NULL, 2,
                           NULL) == pdPASS);
  if (!reader_crypto_selftest()) {
    ESP_LOGE("reader", "Crypto self-test failed; reader stays inactive");
    return;
  }
  ESP_LOGI("reader", "AES-128/AES-256 and SHA-256 self-test passed");
  while (!reader_atecc_begin()) {
    ESP_LOGW("reader", "ATECC not ready; retrying in 5 s");
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
  if (!reader_wallet_begin())
    ESP_LOGW("reader", "Wallet settings invalid; wallet reads disabled");
  if (reader_pd_begin()) {
    configASSERT(xTaskCreate(reader_pd_task, "osdp", 8192, NULL, 10, NULL) ==
                 pdPASS);
    configASSERT(xTaskCreate(reader_nfc_task, "nfc", 16384, NULL, 5, NULL) ==
                 pdPASS);
  } else
    ESP_LOGE(
        "reader",
        "OSDP startup failed or SCBK is not configured; reader stays inactive");
}
