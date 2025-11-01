// I2C_comm.h

#ifndef I2C_COMM_H
#define I2C_COMM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "hardware/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

// I2C 初期化・終了
bool i2c_init_config(i2c_inst_t *i2c_port, uint32_t i2c_speed, uint sda_pin, uint scl_pin);
void i2c_deinit_config(i2c_inst_t *i2c_port);
void i2c_reset(i2c_inst_t *i2c_port, uint32_t i2c_speed, uint sda_pin, uint scl_pin);

// I2C 読み書き
int i2c_read(i2c_inst_t *i2c_port, uint8_t device_addr, uint8_t reg_addr, uint8_t *data, size_t length);
int i2c_write(i2c_inst_t *i2c_port, uint8_t device_addr, uint8_t reg_addr, uint8_t *data, size_t length);

// I2C 読み書き（タイムアウト付き）
int i2c_read_with_timeout(i2c_inst_t *i2c_port, uint8_t device_addr, uint8_t reg_addr, uint8_t *data, size_t length, uint32_t timeout_ms);
int i2c_write_with_timeout(i2c_inst_t *i2c_port, uint8_t device_addr, uint8_t reg_addr, uint8_t *data, size_t length, uint32_t timeout_ms);

// デバイス存在チェックおよびスキャン
bool check_i2c_device(i2c_inst_t *i2c_port, uint8_t addr);
int scan_i2c_devices(i2c_inst_t *i2c_port);

// ★ 追加：張り付き解除＆固まらないスキャナ
void i2c_bus_clear(uint sda_pin, uint scl_pin);
int  scan_i2c_quick(i2c_inst_t *i2c_port);

#ifdef __cplusplus
}
#endif

#endif // I2C_COMM_H
