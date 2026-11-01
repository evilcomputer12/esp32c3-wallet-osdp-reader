// SPDX-License-Identifier: GPL-3.0-or-later
#include "atecc_wallet_policy.h"
#include "board.h"
#include "cryptoauthlib.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "reader.h"
#include "wallet_crypto.h"
#include <stdatomic.h>
#include <string.h>
static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t device;
static uint8_t current_address, config_bytes[128];
static ATCAIfaceCfg cfg;
static SemaphoreHandle_t chip_mutex;
static atomic_bool ready;
static portMUX_TYPE pool_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t pool[512];
static size_t pool_head, pool_used;
static bool bus_open(void) {
  i2c_master_bus_config_t c = {.i2c_port = 0,
                               .sda_io_num = PIN_ATECC_SDA,
                               .scl_io_num = PIN_ATECC_SCL,
                               .clk_source = I2C_CLK_SRC_DEFAULT,
                               .glitch_ignore_cnt = 7,
                               .flags.enable_internal_pullup = true};
  return bus || i2c_new_master_bus(&c, &bus) == ESP_OK;
}
static void bus_close(void) {
  if (device) {
    i2c_master_bus_rm_device(device);
    device = NULL;
  }
  if (bus) {
    i2c_del_master_bus(bus);
    bus = NULL;
  }
}
static bool at_address(uint8_t addr) {
  if (device && current_address == addr)
    return true;
  if (device) {
    i2c_master_bus_rm_device(device);
    device = NULL;
  }
  if (!bus_open())
    return false;
  i2c_device_config_t c = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                           .device_address = addr,
                           .scl_speed_hz = 100000};
  if (i2c_master_bus_add_device(bus, &c, &device) != ESP_OK)
    return false;
  current_address = addr;
  return true;
}
static bool wake_pulse(void) {
  bus_close();
  gpio_set_level(PIN_ATECC_SDA, 1);
  gpio_set_level(PIN_ATECC_SCL, 1);
  gpio_config_t c = {.pin_bit_mask =
                         (1ULL << PIN_ATECC_SDA) | (1ULL << PIN_ATECC_SCL),
                     .mode = GPIO_MODE_INPUT_OUTPUT_OD,
                     .pull_up_en = GPIO_PULLUP_ENABLE};
  if (gpio_config(&c) != ESP_OK)
    return false;
  esp_rom_delay_us(10);
  gpio_set_level(PIN_ATECC_SDA, 0);
  esp_rom_delay_us(80);
  gpio_set_level(PIN_ATECC_SDA, 1);
  esp_rom_delay_us(1500);
  return bus_open();
}
ATCA_STATUS hal_i2c_init(ATCAIface iface, ATCAIfaceCfg *config) {
  (void)iface;
  (void)config;
  return bus_open() ? ATCA_SUCCESS : ATCA_COMM_FAIL;
}
ATCA_STATUS hal_i2c_post_init(ATCAIface iface) {
  (void)iface;
  return ATCA_SUCCESS;
}
ATCA_STATUS hal_i2c_release(void *context) {
  (void)context;
  return ATCA_SUCCESS;
}
ATCA_STATUS hal_i2c_send(ATCAIface iface, uint8_t word, uint8_t *data, int n) {
  if (n < 0 || n > 155 || (n && !data))
    return ATCA_BAD_PARAM;
  uint8_t addr = iface->mIfaceCFG->atcai2c.address;
  if (!addr && !n)
    return wake_pulse() ? ATCA_SUCCESS : ATCA_COMM_FAIL;
  if (!at_address(addr >> 1))
    return ATCA_COMM_FAIL;
  uint8_t packet[156];
  packet[0] = word;
  if (n)
    memcpy(packet + 1, data, n);
  return i2c_master_transmit(device, packet, n + 1, 30) == ESP_OK
             ? ATCA_SUCCESS
             : ATCA_COMM_FAIL;
}
ATCA_STATUS hal_i2c_receive(ATCAIface iface, uint8_t address, uint8_t *data,
                            uint16_t *n) {
  (void)iface;
  if (!data || !n)
    return ATCA_BAD_PARAM;
  if (!at_address(address >> 1) ||
      i2c_master_receive(device, data, *n, 30) != ESP_OK) {
    *n = 0;
    return ATCA_COMM_FAIL;
  }
  return ATCA_SUCCESS;
}
ATCA_STATUS hal_i2c_control(ATCAIface iface, uint8_t op, void *value,
                            size_t n) {
  (void)iface;
  if (op == ATCA_HAL_CHANGE_BAUD && value && n == sizeof(uint32_t) &&
      *(uint32_t *)value == 100000)
    return ATCA_SUCCESS;
  return ATCA_UNIMPLEMENTED;
}
void atca_delay_us(uint32_t us) { esp_rom_delay_us(us); }
void hal_delay_us(uint32_t us) { esp_rom_delay_us(us); }
void atca_delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1)); }
void hal_delay_ms(uint32_t ms) { atca_delay_ms(ms); }
static void refill(void) {
  size_t used;
  taskENTER_CRITICAL(&pool_lock);
  used = pool_used;
  taskEXIT_CRITICAL(&pool_lock);
  if (used > sizeof(pool) - 32)
    return;
  uint8_t block[32];
  if (xSemaphoreTake(chip_mutex, pdMS_TO_TICKS(500)) != pdTRUE)
    return;
  ATCA_STATUS status = atcab_random(block);
  (void)atcab_sleep();
  xSemaphoreGive(chip_mutex);
  if (status == ATCA_SUCCESS) {
    taskENTER_CRITICAL(&pool_lock);
    for (size_t i = 0; i < 32; ++i)
      pool[(pool_head + pool_used + i) % sizeof(pool)] = block[i];
    pool_used += 32;
    taskEXIT_CRITICAL(&pool_lock);
  }
  wallet_wipe(block, sizeof(block));
}
static void entropy_task(void *unused) {
  (void)unused;
  for (;;) {
    refill();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
bool reader_atecc_begin(void) {
  if (atomic_load(&ready))
    return true;
  if (!chip_mutex)
    chip_mutex = xSemaphoreCreateMutex();
  if (!chip_mutex)
    return false;
  if (xSemaphoreTake(chip_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    return false;
  uint8_t found = 0;
  for (unsigned index = 0; index < 113; ++index) {
    uint8_t addr = index == 0 ? 0x60 : (uint8_t)(index + 7);
    uint8_t response[4];
    if (!wake_pulse() || !at_address(addr))
      continue;
    if (i2c_master_receive(device, response, 4, 20) == ESP_OK &&
        hal_check_wake(response, 4) == ATCA_SUCCESS) {
      found = addr;
      break;
    }
  }
  ATCA_STATUS rc = ATCA_NO_DEVICES;
  if (found) {
    cfg = (ATCAIfaceCfg){.iface_type = ATCA_I2C_IFACE,
                         .devtype = ATECC608,
                         .wake_delay = 1500,
                         .rx_retries = 20};
    cfg.atcai2c.address = found << 1;
    cfg.atcai2c.baud = 100000;
    rc = atcab_init(&cfg);
    if (rc == ATCA_SUCCESS)
      rc = atcab_read_config_zone(config_bytes);
    (void)atcab_sleep();
  }
  xSemaphoreGive(chip_mutex);
  ESP_LOGI("atecc", "address=0x%02x status=%d", found, rc);
  if (rc != ATCA_SUCCESS)
    return false;
  for (unsigned i = 0; i < 8; ++i)
    refill();
  if (xTaskCreate(entropy_task, "atecc_entropy", 4096, NULL, 7, NULL) != pdPASS)
    return false;
  atomic_store(&ready, true);
  return true;
}
bool reader_atecc_ready(void) { return atomic_load(&ready); }
int reader_rng_block(unsigned char *out, unsigned int n) {
  if ((!out && n) || !reader_atecc_ready())
    return -1;
  taskENTER_CRITICAL(&pool_lock);
  if (n > pool_used) {
    taskEXIT_CRITICAL(&pool_lock);
    if (out)
      wallet_wipe(out, n);
    return -1;
  }
  for (unsigned i = 0; i < n; ++i) {
    out[i] = pool[pool_head];
    pool[pool_head] = 0;
    pool_head = (pool_head + 1) % sizeof(pool);
  }
  pool_used -= n;
  taskEXIT_CRITICAL(&pool_lock);
  return 0;
}
int reader_atecc_verify(const uint8_t pub[65], const uint8_t hash[32],
                        const uint8_t sig[64]) {
  if (!pub || !hash || !sig || pub[0] != 4 || !reader_atecc_ready() ||
      xSemaphoreTake(chip_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    return -1;
  bool verified = false;
  ATCA_STATUS rc = atcab_verify_extern(hash, sig, pub + 1, &verified);
  (void)atcab_sleep();
  xSemaphoreGive(chip_mutex);
  return rc == ATCA_SUCCESS && verified ? 0 : -1;
}
int reader_atecc_wallet_sign(unsigned slot, const uint8_t digest[32],
                             uint8_t sig[64]) {
  if (!sig)
    return -1;
  memset(sig, 0, 64);
  if (!digest || !reader_atecc_ready() ||
      !reader_atecc_wallet_policy(config_bytes, 128, slot, false) ||
      xSemaphoreTake(chip_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    return -1;
  ATCA_STATUS rc = atcab_sign(slot, digest, sig);
  (void)atcab_sleep();
  xSemaphoreGive(chip_mutex);
  if (rc != ATCA_SUCCESS)
    memset(sig, 0, 64);
  return rc == ATCA_SUCCESS ? 0 : -1;
}
int reader_atecc_wallet_ecdh(unsigned slot, const uint8_t peer[65],
                             uint8_t shared[32]) {
  if (!shared)
    return -1;
  memset(shared, 0, 32);
  if (!peer || peer[0] != 4 || !reader_atecc_ready() ||
      !reader_atecc_wallet_policy(config_bytes, 128, slot, true) ||
      xSemaphoreTake(chip_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    return -1;
  ATCA_STATUS rc =
      atcab_ecdh_base(ECDH_MODE_SOURCE_EEPROM_SLOT | ECDH_MODE_OUTPUT_CLEAR |
                          ECDH_MODE_COPY_OUTPUT_BUFFER,
                      slot, peer + 1, shared, NULL);
  (void)atcab_sleep();
  xSemaphoreGive(chip_mutex);
  uint8_t nonzero = 0;
  for (unsigned i = 0; i < 32; ++i)
    nonzero |= shared[i];
  if (rc != ATCA_SUCCESS || !nonzero) {
    memset(shared, 0, 32);
    return -1;
  }
  return 0;
}
