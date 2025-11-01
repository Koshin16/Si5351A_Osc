#ifndef SERIAL_COMM_H
#define SERIAL_COMM_H

#include <stdbool.h>
#include "hardware/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

extern i2c_inst_t *i2c;
extern uint8_t sht31_addr;
extern uint8_t mcp9600_addr;

// ===== 初期化と基本I/O =====
void serial_comm_init(void);
void serial_comm_echo(void);
const char* serial_receive_command(void);
const char* serial_receive_command_fast(void);  // ★ Si5351用高速版
void serial_printf(const char *fmt, int crlf, ...);

// ===== テスト用 =====
bool test_command(const char* cmd);
void test_help_screen(void);

// ===== ロギング制御 =====
bool serial_comm_logging_enabled(void);
bool serial_comm_logging_mode2(void);

#ifdef __cplusplus
}
#endif

#endif
