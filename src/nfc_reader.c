// SPDX-License-Identifier: GPL-3.0-or-later
#include "asymcred/asymcred_pkoc.h"
#include "board.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pn532.h"
#include "presentation.h"
#include "reader.h"
#include "wallet_crypto.h"
#include <stdatomic.h>
#include <string.h>
static pn532_t pn;
static presentation_t presentation;
static wallet_reader_t wallet;
static asymcred_pkoc_t pkoc;
static atomic_uint attempts, reported, failures, polls;
static atomic_bool nfc_ready;
static unsigned transaction_generation;
static bool transaction_live(void) {
  return reader_pd_secure() && reader_pd_generation() == transaction_generation;
}
bool reader_nfc_ready(void) { return atomic_load(&nfc_ready); }
static int uart_read(void *u, uint8_t *out, size_t n, uint32_t ms) {
  (void)u;
  return uart_read_bytes(PN532_UART, out, n, pdMS_TO_TICKS(ms ? ms : 1));
}
static bool uart_write(void *u, const uint8_t *data, size_t n) {
  (void)u;
  return uart_write_bytes(PN532_UART, data, n) == (int)n &&
         uart_wait_tx_done(PN532_UART, pdMS_TO_TICKS(50)) == ESP_OK;
}
static void drain(void *u) {
  (void)u;
  uart_flush_input(PN532_UART);
}
static uint32_t now(void *u) {
  (void)u;
  return reader_millis();
}
static void delay(void *u, uint32_t ms) {
  (void)u;
  vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1));
}
static const pn532_io_t io = {NULL, uart_read, uart_write, drain, now, delay};
static asymcred_status_t hash(void *u, const uint8_t *data, size_t n,
                              uint8_t out[32]) {
  (void)u;
  return wallet_sha256(data, n, out) ? ASYMCRED_ERR_INVALID_STATE : ASYMCRED_OK;
}
static asymcred_status_t verify(void *u, const uint8_t pub[65],
                                const uint8_t digest[32],
                                const uint8_t sig[64]) {
  (void)u;
  return reader_atecc_verify(pub, digest, sig) ? ASYMCRED_ERR_BAD_SIGNATURE
                                               : ASYMCRED_OK;
}
static asymcred_status_t random_bytes(void *u, uint8_t *out, size_t n) {
  (void)u;
  return n > UINT32_MAX || reader_rng_block(out, (unsigned)n)
             ? ASYMCRED_ERR_INVALID_STATE
             : ASYMCRED_OK;
}
static const asymcred_crypto_t crypto = {hash, verify, random_bytes, NULL};
static void deliver(const uint8_t credential[32], unsigned provider) {
  if (presentation_duplicate(&presentation, credential))
    return;
  if (reader_enqueue(credential, 32, 256, transaction_generation)) {
    presentation_report(&presentation, credential);
    atomic_fetch_add(&reported, 1);
    reader_feedback_read();
    ESP_LOGI("nfc", "credential queued provider=%u", provider);
  }
}
static bool run_wallet(void) {
  wallet_hooks_t h = reader_wallet_hooks();
  if (!wallet_begin(&wallet, reader_wallet_config(), &h))
    return false;
  uint8_t command[WALLET_MAX_COMMAND], response[WALLET_MAX_RESPONSE];
  size_t cmd_n;
  for (unsigned steps = 0; steps < 24 && transaction_live(); ++steps) {
    size_t n = sizeof(response);
    if (!wallet_next(&wallet, command, sizeof(command), &cmd_n) ||
        !pn532_apdu(&pn, command, cmd_n, response, &n)) {
      wallet_transport_error(&wallet);
      break;
    }
    bool ok = wallet_feed(&wallet, response, n);
    wallet_wipe(response, sizeof(response));
    if (!ok)
      break;
    if (wallet.state == WS_DONE) {
      deliver(wallet.result.credential, (unsigned)wallet.result.provider);
      wallet_reset(&wallet);
      return true;
    }
  }
  ESP_LOGW("wallet", "failed error=%u sw=%04x", (unsigned)wallet.fault,
           wallet.status);
  wallet_reset(&wallet);
  wallet_wipe(response, sizeof(response));
  return false;
}
static bool run_pkoc(void) {
  static const uint16_t versions[] = {ASYMCRED_PKOC_VERSION_1_1,
                                      ASYMCRED_PKOC_VERSION_1_0};
  asymcred_pkoc_config_t c = {.supported_versions = versions,
                              .supported_version_count = 2,
                              .require_signature = true};
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  memcpy(c.reader_id + 16, mac, 6);
  if (asymcred_pkoc_begin(&pkoc, &c, &crypto) != ASYMCRED_OK)
    return false;
  uint8_t command[96], response[1024];
  size_t cmd_n;
  for (unsigned step = 0; step < 8 && transaction_live(); ++step) {
    size_t n = sizeof(response);
    if (asymcred_pkoc_next_apdu(&pkoc, command, sizeof(command), &cmd_n) !=
            ASYMCRED_OK ||
        !pn532_apdu(&pn, command, cmd_n, response, &n))
      return false;
    if (step == 0 && cmd_n >= 2 && command[1] == 0xa4 && n >= 2 &&
        response[n - 2] == 0x6a && response[n - 1] == 0x82) {
      const wallet_config_t *w = reader_wallet_config();
      return (w->apple || w->google) && run_wallet();
    }
    if (asymcred_pkoc_feed(&pkoc, response, n) != ASYMCRED_OK)
      return false;
    if (!asymcred_pkoc_pending(&pkoc)) {
      asymcred_pkoc_result_t result;
      if (asymcred_pkoc_result(&pkoc, &result) != ASYMCRED_OK ||
          !result.signature_verified)
        return false;
      uint8_t credential[32];
      size_t bytes, bits;
      if (asymcred_pkoc_credential(result.public_key, ASYMCRED_PKOC_CRED_256BIT,
                                   credential, 32, &bytes,
                                   &bits) != ASYMCRED_OK ||
          bytes != 32 || bits != 256)
        return false;
      deliver(credential, 0);
      return true;
    }
  }
  return false;
}
void reader_report(void) {
  ESP_LOGI(
      "status",
      "atecc=%u nfc=%u secure=%u attempts=%u queued=%u failures=%u polls=%u",
      reader_atecc_ready(), atomic_load(&nfc_ready), reader_pd_secure(),
      atomic_load(&attempts), atomic_load(&reported), atomic_load(&failures),
      atomic_load(&polls));
  reader_pd_report();
}
void reader_nfc_task(void *unused) {
  (void)unused;
  uart_config_t c = {.baud_rate = 115200,
                     .data_bits = UART_DATA_8_BITS,
                     .parity = UART_PARITY_DISABLE,
                     .stop_bits = UART_STOP_BITS_1,
                     .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                     .source_clk = UART_SCLK_DEFAULT};
  ESP_ERROR_CHECK(uart_driver_install(PN532_UART, 2048, 0, 0, NULL, 0));
  ESP_ERROR_CHECK(uart_param_config(PN532_UART, &c));
  ESP_ERROR_CHECK(uart_set_pin(PN532_UART, PIN_PN532_TX, PIN_PN532_RX,
                               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  unsigned errors = 0;
  for (;;) {
    if (!atomic_load(&nfc_ready)) {
      bool ok = pn532_begin(&pn, &io);
      atomic_store(&nfc_ready, ok);
      if (!ok) {
        ESP_LOGW("pn532", "not ready, error=%d", pn.error);
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      }
      // Preserve credential suppression across transport recovery.
      if (presentation.held)
        presentation.refind = true;
      errors = 0;
      ESP_LOGI("pn532", "ready firmware=%08lx", (unsigned long)pn.firmware);
    }
    vTaskDelay(pdMS_TO_TICKS(250));
    if (presentation.held && !presentation.refind) {
      int status = pn532_presence(&pn);
      atomic_fetch_add(&polls, 1);
      if (status == 0) {
        presentation_clear_absence(&presentation);
        errors = 0;
      } else if (status == 1) {
        errors = 0;
        if (presentation_timeout(&presentation))
          atomic_store(&nfc_ready, pn532_restart_discovery(&pn));
      } else {
        presentation_clear_absence(&presentation);
        if (++errors >= 3)
          atomic_store(&nfc_ready, false);
      }
      continue;
    }
    if (!reader_pd_secure())
      continue;
    uint8_t uid[7];
    size_t uid_n;
    int status = pn532_discover(&pn, uid, &uid_n);
    atomic_fetch_add(&polls, 1);
    if (status == 0) {
      errors = 0;
      presentation_absent(&presentation, reader_millis());
      continue;
    }
    if (status < 0) {
      presentation_clear_absence(&presentation);
      if (++errors >= 3)
        atomic_store(&nfc_ready, false);
      continue;
    }
    errors = 0;
    bool reselected = presentation.refind;
    if (!presentation_found(&presentation, uid, uid_n) && !reselected)
      continue;
    atomic_fetch_add(&attempts, 1);
    transaction_generation = reader_pd_generation();
    uint32_t started = reader_millis();
    bool ok = run_pkoc();
    wallet_wipe(&pkoc, sizeof(pkoc));
    if (!ok)
      atomic_fetch_add(&failures, 1);
    ESP_LOGI("nfc", "transaction=%s duration_ms=%lu", ok ? "ok" : "failed",
             (unsigned long)(reader_millis() - started));
  }
}
