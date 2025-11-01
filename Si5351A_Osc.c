/**
 * @file    main.c
 * @brief   RP2040 USBシリアルCLI（Si5351A/I2C制御統合版）
 * @date    2025-11-02
 * @version 1.1
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"

#include "I2C_comm.h"
#include "led_blink.h"
#include "si5351_cli.h"     // ★ 今後のため有効化
#include "serial_comm.h"

// ===== I2C 設定 =====
#define I2C_PORT    i2c0
#define SDA_PIN     7   // XIAO D5
#define SCL_PIN     6   // XIAO D4
#define I2C_SPEED   100000
#define SI5351_ADDR 0x60

// ===== CLI 設定 =====
#define CMD_BUF_LEN 64

// ===== 初期バナー表示 =====
static void banner(void) {
    printf("\r\n**************************************************************\r\n");
    printf(" RP2040 USB Serial CLI  (for Si5351A / I2C device)\r\n");
    printf("**************************************************************\r\n");
    printf(" 改行コード: CR+LF   /   ローカルエコー: OFF推奨\r\n");
    printf(" Type 'help' or 'h' then [Enter]\r\n\r\n");
}

// ===== Help 表示関数 =====
static void print_help(void) {
    printf("\r\n================ HELP ================\r\n");
    printf(" help / h / H / ? : show this help\r\n");
    printf(" i2cscan         : scan I2C bus\r\n");
    printf(" ledon / ledoff  : control onboard LED\r\n");
    printf(" clk0=10         : Si5351A example (10 MHz)\r\n");
    printf(" clk 1 20        : Si5351A CLK1=20 MHz\r\n");
    printf("======================================\r\n\r\n");
}

// ===== メイン =====
int main(void) {
    // ==== USB初期化 ====
    stdio_init_all();
    setvbuf(stdin,  NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    while (!stdio_usb_connected()) sleep_ms(10);
    banner();

    // ==== LED & I2C 初期化 ====
    start_led_blinking(250);
    i2c_init_config(I2C_PORT, I2C_SPEED, SDA_PIN, SCL_PIN);
    printf("[I2C] initialized SDA=%d SCL=%d\r\n", SDA_PIN, SCL_PIN);

    // ==== Si5351 初期化 ====
    si5351_cli_init(I2C_PORT, SI5351_ADDR);
    printf("[Si5351A] ready (addr=0x%02X)\r\n", SI5351_ADDR);

    // ==== コマンドバッファ ====
    char cmd[CMD_BUF_LEN];
    int idx = 0;

    printf("> ");  // 初回プロンプト

    // ==== メインループ ====
    while (true) {
        int ch = getchar_timeout_us(2000);
        if (ch == PICO_ERROR_TIMEOUT) {
            tight_loop_contents();
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            if (idx == 0) continue;
            cmd[idx] = '\0';
            idx = 0;

            // === コマンド判定 ===
            if (!strcasecmp(cmd, "h") || !strcasecmp(cmd, "help") ||
                !strcasecmp(cmd, "H") || !strcmp(cmd, "?")) {
                print_help();
            }
            else if (!strcasecmp(cmd, "i2cscan")) {
                scan_i2c_quick(I2C_PORT);
            }
            else if (!strcasecmp(cmd, "ledon")) {
                gpio_put(25, 1);
                printf("LED ON\r\n");
            }
            else if (!strcasecmp(cmd, "ledoff")) {
                gpio_put(25, 0);
                printf("LED OFF\r\n");
            }
            else {
                printf("CMD: [%s]\r\n", cmd);
                si5351_cli_handle(cmd);  // ★Si5351コマンド処理
            }

            printf("\r\n> ");  // プロンプト再表示
        }
        else if (ch == 0x08 || ch == 0x7F) {  // バックスペース
            if (idx > 0) idx--;
        }
        else if (ch >= 0x20 && ch <= 0x7E) {  // ASCII範囲のみ受理
            if (idx < CMD_BUF_LEN - 1) cmd[idx++] = (char)ch;
        }
    }

    return 0;
}
