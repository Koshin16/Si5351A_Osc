// serial_comm.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/i2c.h"
#include "serial_comm.h"

#define SERIAL_BUF_LEN 64

// ==== 内部変数 ====
static char recv_buf[SERIAL_BUF_LEN];
static char command_buf[SERIAL_BUF_LEN];
static volatile int recv_index = 0;
static volatile bool command_ready = false;

// ==== ロギング状態 ====
static bool logging_enabled = false;
static bool logging_mode2 = false;

// ==== 外部I2C変数 ====
extern i2c_inst_t *i2c;
extern uint8_t sht31_addr;
extern uint8_t mcp9600_addr;

// ==== 内部関数 ====
static void strip_newline(char *cmd);
static int stricmp_embedded(const char *s1, const char *s2);

// ============================================================
//                  基本通信関連
// ============================================================

void serial_comm_init(void) {
    stdio_init_all();
    sleep_ms(1000); // 初回USB接続安定待ち
    serial_printf("USB serial comm.: OK", 1);
}

void serial_comm_echo(void) {
    int ch = getchar_timeout_us(0);
    if (ch == PICO_ERROR_TIMEOUT) return;

    if (ch == '\r' || ch == '\n') {
        if (recv_index > 0) {
            recv_buf[recv_index] = '\0';
            serial_printf("Received: %s", 1, recv_buf);
            recv_index = 0;
        }
    } else if (recv_index < SERIAL_BUF_LEN - 1) {
        recv_buf[recv_index++] = (char)ch;
    }
}

// ============================================================
//                  標準コマンド受信（MCP9600など）
// ============================================================

const char* serial_receive_command(void) {
    static char recv_buf[64];
    static char command_buf[64];
    static int recv_index = 0;

    int ch = getchar_timeout_us(2000);
    if (ch == PICO_ERROR_TIMEOUT) return NULL;

    if (ch == '\r' || ch == '\n') {
        if (recv_index == 0) return NULL;

        recv_buf[recv_index] = '\0';
        strncpy(command_buf, recv_buf, sizeof(command_buf));
        recv_index = 0;

        // CR+LF対策
        while (true) {
            int next = getchar_timeout_us(0);
            if (next != '\r' && next != '\n') break;
        }
        return command_buf;
    }

    if (recv_index < (int)sizeof(recv_buf) - 1)
        recv_buf[recv_index++] = (char)ch;

    return NULL;
}

// ============================================================
//          Si5351A CLIなど高速応答用の受信関数
// ============================================================

const char* serial_receive_command_fast(void) {
    static char buf[64];
    static char cmd[64];
    static int idx = 0;

    int ch = getchar_timeout_us(2000);
    if (ch == PICO_ERROR_TIMEOUT) return NULL;

    if (ch == '\r' || ch == '\n') {
        if (idx == 0) return NULL;
        buf[idx] = '\0';
        strncpy(cmd, buf, sizeof(cmd));
        idx = 0;

        // CR+LF対策
        while (true) {
            int next = getchar_timeout_us(0);
            if (next != '\r' && next != '\n') break;
        }
        return cmd;
    }

    if (idx < (int)sizeof(buf) - 1) buf[idx++] = (char)ch;
    return NULL;
}

// ============================================================
//                      テスト・ヘルプ系
// ============================================================

bool test_command(const char* cmd) {
    if (stricmp_embedded(cmd, "test") == 0 || stricmp_embedded(cmd, "t") == 0) {
        serial_printf("Serial comm.(test): OK", 1);
        return true;
    } else if (stricmp_embedded(cmd, "H") == 0 || stricmp_embedded(cmd, "HELP") == 0) {
        test_help_screen();
        return true;
    }
    return false;
}

void test_help_screen(void) {
    serial_printf("", 1);
    serial_printf("================== HELP MENU (for Test) ====================", 1);
    serial_printf(" TEST / T     : Serial test response", 1);
    serial_printf(" HELP / H     : Show this help menu", 1);
    serial_printf("============================================================", 1);
    serial_printf("", 1);
}

// ============================================================
//                  ロギングフラグ制御
// ============================================================

bool serial_comm_logging_enabled(void) { return logging_enabled; }
bool serial_comm_logging_mode2(void)   { return logging_mode2; }

// ============================================================
//                  共通ユーティリティ
// ============================================================

static void strip_newline(char *cmd) {
    char *p;
    if ((p = strchr(cmd, '\r')) != NULL) *p = '\0';
    if ((p = strchr(cmd, '\n')) != NULL) *p = '\0';
}

int stricmp_embedded(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
        s1++; s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

void serial_printf(const char *fmt, int crlf, ...) {
    va_list args;
    va_start(args, crlf);
    vprintf(fmt, args);
    va_end(args);

    switch (crlf) {
        case 1: printf("\r\n"); break;  // CR+LF
        case 2: printf("\r");   break;
        case 3: printf("\n");   break;
        default: break;
    }
}
