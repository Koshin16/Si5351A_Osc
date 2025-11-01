/**
 * @file    i2c_comm.c
 * @brief   RP2040 I2C通信共通モジュール（Si5351A/MCP9600/DMM共用）
 * @date    2025-11-02
 * @version 2.1
 */

#include "i2c_comm.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

// =========================================================
// 定数・設定
// =========================================================
#define I2C_TOUT_US   2000   // 各I2C操作のタイムアウト（us）
#define I2C_RETRY_MS  20     // write_with_timeout等のリトライ期間

// =========================================================
// 基本初期化
// =========================================================
bool i2c_init_config(i2c_inst_t *i2c_port, uint32_t i2c_speed, uint sda_pin, uint scl_pin) {
    int ret = i2c_init(i2c_port, i2c_speed);
    if (ret < 0) {
        printf("[ERROR] i2c_init() failed (ret=%d)\r\n", ret);
        return false;
    }
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);

    printf("[INFO] I2C initialized: SDA=GPIO%d, SCL=GPIO%d, speed=%u Hz\r\n",
           sda_pin, scl_pin, (unsigned)i2c_speed);
    return true;
}

void i2c_deinit_config(i2c_inst_t *i2c_port) {
    i2c_deinit(i2c_port);
}

void i2c_reset(i2c_inst_t *i2c_port, uint32_t speed, uint sda, uint scl) {
    i2c_deinit_config(i2c_port);
    sleep_ms(50);
    i2c_init_config(i2c_port, speed, sda, scl);
}

// =========================================================
// 基本的な読み書き（タイムアウト付き）
// =========================================================
int i2c_read(i2c_inst_t *port, uint8_t dev, uint8_t reg, uint8_t *data, size_t len) {
    int r = i2c_write_timeout_us(port, dev, &reg, 1, true, I2C_TOUT_US);
    if (r < 0) return -1;
    r = i2c_read_timeout_us(port, dev, data, len, false, I2C_TOUT_US);
    return (r < 0) ? -2 : 0;
}

int i2c_write(i2c_inst_t *port, uint8_t dev, uint8_t reg, uint8_t *data, size_t len) {
    uint8_t buf[9];
    if (len > sizeof(buf)-1) return -9;
    buf[0] = reg;
    memcpy(&buf[1], data, len);
    int r = i2c_write_timeout_us(port, dev, buf, len + 1, false, I2C_TOUT_US);
    return (r < 0) ? -1 : 0;
}

// =========================================================
// リトライ付き読み書き（ブロック回避）
// =========================================================
int i2c_read_with_timeout(i2c_inst_t *port, uint8_t dev, uint8_t reg, uint8_t *data, size_t len, uint32_t timeout_ms) {
    uint64_t limit = to_ms_since_boot(get_absolute_time()) + timeout_ms;
    while (to_ms_since_boot(get_absolute_time()) < limit) {
        if (i2c_read(port, dev, reg, data, len) == 0) return 0;
        sleep_ms(2);
    }
    printf("[I2C] Timeout (read 0x%02X)\r\n", dev);
    return -1;
}

int i2c_write_with_timeout(i2c_inst_t *port, uint8_t dev, uint8_t reg, uint8_t *data, size_t len, uint32_t timeout_ms) {
    uint64_t limit = to_ms_since_boot(get_absolute_time()) + timeout_ms;
    while (to_ms_since_boot(get_absolute_time()) < limit) {
        if (i2c_write(port, dev, reg, data, len) == 0) return 0;
        sleep_ms(2);
    }
    printf("[I2C] Timeout (write 0x%02X)\r\n", dev);
    return -1;
}

// =========================================================
// スキャン機能（安全版）
// =========================================================
bool i2c_ping(i2c_inst_t *port, uint8_t addr) {
    uint8_t dummy = 0;
    int r = i2c_write_timeout_us(port, addr, &dummy, 0, false, I2C_TOUT_US);
    return (r >= 0);
}

int scan_i2c_devices(i2c_inst_t *port) {
    int found = 0;
    printf("Scanning I2C devices...\r\n");
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_ping(port, addr)) {
            printf("  Found device at 0x%02X\r\n", addr);
            found++;
        }
    }
    if (!found) printf("No I2C devices found.\r\n");
    return found;
}

int scan_i2c_quick(i2c_inst_t *port) {
    uint8_t dummy = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        int r = i2c_write_timeout_us(port, addr, &dummy, 0, false, 1000);
        if (r >= 0) {
            printf("Found I2C device at 0x%02X\r\n", addr);
            return addr;
        }
    }
    printf("No I2C devices found.\r\n");
    return 0;
}

// =========================================================
// I2Cバスクリア（SCL手動トグル）
// =========================================================
void i2c_bus_clear(uint sda, uint scl) {
    gpio_init(scl);
    gpio_init(sda);
    gpio_set_dir(scl, GPIO_OUT);
    gpio_set_dir(sda, GPIO_IN);
    gpio_pull_up(scl);
    gpio_pull_up(sda);
    sleep_ms(1);

    for (int i = 0; i < 9; i++) {
        gpio_put(scl, 0); sleep_us(5);
        gpio_put(scl, 1); sleep_us(5);
        if (gpio_get(sda)) break;
    }
    sleep_us(5);
}
