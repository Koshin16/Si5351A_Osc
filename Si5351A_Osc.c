/**
 * @file    main.c
 * @brief   RP2040 USB Serial CLI（AE-Si5351A 専用, I2C=0x60 固定）
 * @date    2025-11-02
 * @version 2.0
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"

#include "I2C_comm.h"     // i2c_bus_clear / i2c_init_config / i2c_read / i2c_write_with_timeout
#include "led_blink.h"    // start_led_blinking()
#include "si5351_cli.h"   // si5351_cli_init(), si5351_cli_handle()

// ===== I2C 配線設定 =====
// XIAO RP2040: SDA=7(D5), SCL=6(D4)
#define I2C_PORT    i2c1
#define SDA_PIN     7
#define SCL_PIN     6
#define I2C_SPEED   100000   // Si5351Aは最大400kHz対応。初期は100kHzで安全に。

// ===== CLI 入力バッファ =====
#define CMD_BUF_LEN 64

// ===== バナー =====
static void banner(void) {
    printf("\r\n**************************************************************\r\n");
    printf(" RP2040 USB Serial CLI  (for AE-Si5351A @I2C=0x60)\r\n");
    printf("**************************************************************\r\n");
    printf(" 改行コード: CR+LF   /   ローカルエコー: OFF推奨\r\n");
    printf(" Type 'help' or 'h' then [Enter]\r\n\r\n");
}

// ===== シンプルなping =====
static int ping_si5351(void) {
    uint8_t dummy = 0;
    int rc = i2c_write_timeout_us(I2C_PORT, 0x60, &dummy, 0, false, 5000);
    if (rc >= 0) printf("[PING] 0x60 ACK\r\n");
    else         printf("[PING] 0x60 NACK/Timeout (rc=%d)\r\n", rc);
    return rc;
}

// ===== strict scan =====
static void scan_strict(void) {
    printf("[SCAN] strict 7-bit scan (write reg=0 + read 1B)...\r\n");
    int found = 0;
    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        uint8_t v = 0x00;
        int rc1 = i2c_write_timeout_us(I2C_PORT, addr, &v, 1, true, 2000);
        int rc2 = i2c_read_timeout_us(I2C_PORT, addr, &v, 1, false, 2000);
        if (rc1 >= 0 && rc2 >= 0) {
            printf("  - found 0x%02X (val=0x%02X)\r\n", addr, v);
            found++;
        }
        sleep_us(500);
    }
    if (!found) printf("[SCAN] none\r\n");
}

// ===== main =====
int main(void) {
    stdio_init_all();
    setvbuf(stdin,  NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    while (!stdio_usb_connected()) sleep_ms(10);
    banner();

    // LED点滅開始
    start_led_blinking(250);

    printf("[BOOT] I2C bus clear...\r\n");
    i2c_bus_clear(SDA_PIN, SCL_PIN);
    sleep_ms(2);

    printf("[BOOT] I2C init...\r\n");
    if (!i2c_init_config(I2C_PORT, I2C_SPEED, SDA_PIN, SCL_PIN)) {
        printf("[ERROR] I2C init failed.\r\n");
        while (1) tight_loop_contents();
    }
    printf("[INFO] I2C initialized: SDA=%d, SCL=%d, speed=%d Hz\r\n", SDA_PIN, SCL_PIN, I2C_SPEED);

    // Si5351A通信チェック
    printf("[BOOT] Pinging Si5351A @0x60...\r\n");
    if (ping_si5351() < 0) {
        printf("[ERR]  Si5351A not responding.\r\n");
        while (1) tight_loop_contents();
    }

    // スキャン
    printf("[BOOT] I2C strict scan...\r\n");
    scan_strict();

    // Si5351A準備
    printf("[BOOT] Si5351A (addr=0x60)\r\n");
    si5351_cli_init(I2C_PORT, 0x60);

    // PLL初期化 & 出力設定
    printf("[BOOT] init PLLA...\r\n");
    si5351_cli_handle("init");
    sleep_ms(100);

    printf("[BOOT] set CLK0=100 MHz...\r\n");
    si5351_cli_handle("clk0=100");

    printf("[BOOT] disable CLK1/2...\r\n");
    si5351_cli_handle("clk1=0");
    si5351_cli_handle("clk2=0");

    printf("[BOOT] CLK0=100 MHz output enabled (CLK1/2 OFF)\r\n");

    // 簡易CLIループ
    char cmd[CMD_BUF_LEN];
    int idx = 0;
    printf("\r\n> ");
    while (true) {
        int ch = getchar_timeout_us(2000);
        if (ch == PICO_ERROR_TIMEOUT) continue;
        if (ch == '\r' || ch == '\n') {
            if (idx == 0) continue;
            cmd[idx] = '\0';
            idx = 0;

            if (!strcasecmp(cmd, "scan")) scan_strict();
            else if (!strcasecmp(cmd, "ping")) ping_si5351();
            else si5351_cli_handle(cmd);

            printf("\r\n> ");
        } else if (ch >= 0x20 && ch <= 0x7E && idx < CMD_BUF_LEN - 1) {
            cmd[idx++] = (char)ch;
        }
    }
}
