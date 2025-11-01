/**
 * @file    si5351_cli.h
 * @brief   Si5351A CLI (USBシリアル制御) ヘッダ
 * @date    2025-11-01
 * @version 1.7.0
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SI5351_CLI_H
#define SI5351_CLI_H

#include <stdint.h>
#include <stdbool.h>
#include <stdint.h>
#include "hardware/i2c.h"  // for i2c_inst_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Si5351A CLI の初期化
 * @param i2c_port 使用する I2C ポート (例: i2c0)
 * @param i2c_addr Si5351A の 7bit アドレス（通常 0x60）
 *
 * 例:
 *   si5351_cli_init(i2c0, 0x60);
 */
void si5351_cli_init(i2c_inst_t *i2c_port, uint8_t i2c_addr);

/**
 * @brief 1行のコマンドを処理（USBシリアルから受け取った文字列を渡す）
 * @param cmd ヌル終端文字列（例: "clk 1 20", "freq=100", "help" など）
 *
 * 例:
 *   const char* cmd = serial_receive_command();
 *   if (cmd) si5351_cli_handle(cmd);
 */
void si5351_cli_handle(const char *cmd);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SI5351_CLI_H
