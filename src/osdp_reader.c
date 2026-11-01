// SPDX-License-Identifier: GPL-3.0-or-later
#include "board.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "osdp/osdp_commands.h"
#include "osdp/osdp_pd.h"
#include "osdp/osdp_replies.h"
#include "osdp_sc_wolfcrypt.h"
#include "reader.h"
#include "reader_config.h"
#include <stdatomic.h>
#include <string.h>
static osdp_pd_t pd;
static osdp_sc_wolfcrypt_t sc;
static uint8_t events[128], reply_buffer[64];
static atomic_bool secure;
static atomic_uint generation;
static uint32_t serial_number, queued_at;
static QueueHandle_t incoming;
typedef struct {
  uint8_t data[32];
  uint16_t bits;
  size_t n;
  uint32_t at;
  unsigned generation;
} Event;
static int receive(void *u, uint8_t *out, size_t cap) {
  (void)u;
  int n = uart_read_bytes(OSDP_UART, out, cap, 0);
  return n > 0 ? n : 0;
}
static int transmit(void *u, const uint8_t *data, size_t n) {
  (void)u;
  if (uart_write_bytes(OSDP_UART, data, n) != (int)n)
    return 0;
  uint32_t ms = (uint32_t)((n * 10000 + OSDP_BAUD - 1) / OSDP_BAUD) + 20;
  return uart_wait_tx_done(OSDP_UART, pdMS_TO_TICKS(ms)) == ESP_OK ? (int)n : 0;
}
static uint32_t now(void *u) {
  (void)u;
  return reader_millis();
}
static osdp_status_t command(void *u, uint8_t code, const uint8_t *data,
                             size_t n, osdp_pd_reply_t *reply) {
  (void)u;
  reply->payload = reply_buffer;
  reply->payload_len = 0;
  switch (code) {
  case OSDP_CMD_POLL:
    if (n)
      return OSDP_ERR_BAD_LENGTH;
    reply->code = OSDP_REPLY_ACK;
    return OSDP_OK;
  case OSDP_CMD_ID: {
    if (n != 1)
      return OSDP_ERR_BAD_LENGTH;
    const osdp_pdid_t id = {{0, 0, 0}, 1, 1, serial_number, 0, 1, 0};
    reply->code = OSDP_REPLY_PDID;
    return osdp_pdid_build(&id, reply_buffer, sizeof(reply_buffer),
                           &reply->payload_len);
  }
  case OSDP_CMD_LSTAT:
    if (n)
      return OSDP_ERR_BAD_LENGTH;
    reply_buffer[0] = reply_buffer[1] = 0;
    reply->code = OSDP_REPLY_LSTATR;
    reply->payload_len = 2;
    return OSDP_OK;
  case OSDP_CMD_RSTAT:
    if (n)
      return OSDP_ERR_BAD_LENGTH;
    reply_buffer[0] = reader_atecc_ready() && reader_nfc_ready()
                          ? 0
                          : OSDP_RSTATR_NOT_CONNECTED;
    reply->code = OSDP_REPLY_RSTATR;
    reply->payload_len = 1;
    return OSDP_OK;
  case OSDP_CMD_LED:
  case OSDP_CMD_BUZ:
    reply->code = OSDP_REPLY_ACK;
    return reader_feedback_command(
        code, data, n, osdp_pd_is_online(&pd) && osdp_pd_sc_established(&pd));
  case OSDP_CMD_OUT:
    return OSDP_ERR_INVALID_ARG;
  default:
    return OSDP_ERR_NOT_SUPPORTED;
  }
}
bool reader_pd_begin(void) {
  reader_feedback_begin();
  gpio_set_level(PIN_RS485_DE, 0);
  gpio_set_direction(PIN_RS485_DE, GPIO_MODE_OUTPUT);
  if (!reader_configured())
    return false;
  uart_config_t c = {.baud_rate = OSDP_BAUD,
                     .data_bits = UART_DATA_8_BITS,
                     .parity = UART_PARITY_DISABLE,
                     .stop_bits = UART_STOP_BITS_1,
                     .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                     .source_clk = UART_SCLK_DEFAULT};
  if (uart_driver_install(OSDP_UART, 1024, 0, 0, NULL, 0) != ESP_OK ||
      uart_param_config(OSDP_UART, &c) != ESP_OK ||
      uart_set_pin(OSDP_UART, PIN_RS485_TX, PIN_RS485_RX, PIN_RS485_DE,
                   UART_PIN_NO_CHANGE) != ESP_OK ||
      uart_set_mode(OSDP_UART, UART_MODE_RS485_HALF_DUPLEX) != ESP_OK)
    return false;
  gpio_pullup_en(PIN_RS485_RX);
  incoming = xQueueCreate(4, sizeof(Event));
  if (!incoming)
    return false;
  osdp_pd_init(&pd, 0);
  osdp_pd_transport_t transport = {receive, transmit, now, NULL};
  osdp_pd_set_transport(&pd, &transport);
  osdp_pd_set_command_handler(&pd, command, NULL);
  osdp_pd_set_event_queue(&pd, events, sizeof(events));
  osdp_sc_crypto_t crypto = {0};
  if (osdp_sc_wolfcrypt_aes128(&sc, &crypto) != OSDP_OK ||
      osdp_sc_wolfcrypt_rng(&sc, &crypto) != OSDP_OK)
    return false;
  osdp_pd_set_sc_crypto(&pd, &crypto);
  osdp_pd_set_sc_scbk(&pd, reader_scbk());
  uint8_t mac[6], cuid[8] = {0, 0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  memcpy(cuid + 2, mac, 6);
  memcpy(&serial_number, mac + 2, 4);
  osdp_pd_set_sc_cuid(&pd, cuid);
  osdp_pdcap_record_t caps[] = {
      {3, 1, 0}, {4, 2, 1}, {13, 0, 1}, {16, 2, 0}, {5, 2, 0}};
  return osdp_pd_set_pdcap(&pd, caps,
                           sizeof(caps) / sizeof(caps[0]) -
                               (reader_settings.buzzer_enabled ? 0 : 1),
                           NULL) == OSDP_OK;
}
bool reader_pd_secure(void) { return atomic_load(&secure); }
unsigned reader_pd_generation(void) { return atomic_load(&generation); }
bool reader_enqueue(const uint8_t *credential, size_t n, uint16_t bits,
                    unsigned transaction_generation) {
  if (!credential || !n || n > 32 || bits > n * 8 || !reader_pd_secure() ||
      !incoming || transaction_generation != reader_pd_generation())
    return false;
  Event e = {.n = n,
             .bits = bits,
             .at = reader_millis(),
             .generation = transaction_generation};
  memcpy(e.data, credential, n);
  return xQueueSend(incoming, &e, 0) == pdTRUE;
}
void reader_pd_task(void *unused) {
  (void)unused;
  bool previous = false;
  for (;;) {
    osdp_pd_tick(&pd);
    bool active = osdp_pd_is_online(&pd) && osdp_pd_sc_established(&pd);
    if (active != previous) {
      atomic_fetch_add(&generation, 1);
      ESP_LOGI("osdp", "secure=%u", active);
      previous = active;
    }
    atomic_store(&secure, active);
    if (!active) {
      osdp_pd_clear_events(&pd);
      xQueueReset(incoming);
    }
    if (osdp_pd_event_pending(&pd) && reader_millis() - queued_at > 2000)
      osdp_pd_clear_events(&pd);
    Event e;
    if (active && !osdp_pd_event_pending(&pd) &&
        xQueueReceive(incoming, &e, 0) == pdTRUE) {
      if (reader_millis() - e.at <= 2000 &&
          e.generation == atomic_load(&generation)) {
        uint8_t payload[36];
        size_t n = 0;
        osdp_raw_t raw = {0, OSDP_RAW_FORMAT_RAW, e.bits, e.data, e.n};
        if (osdp_raw_build(&raw, payload, sizeof(payload), &n) == OSDP_OK &&
            osdp_pd_enqueue_event(&pd, OSDP_REPLY_RAW, payload, n) == OSDP_OK)
          queued_at = reader_millis();
      }
      memset(&e, 0, sizeof(e));
    }
    reader_feedback_tick(active);
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
