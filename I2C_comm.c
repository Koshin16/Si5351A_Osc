// I2C_comm.c

#include "i2c_comm.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include <stdio.h>
#include "pico/stdlib.h"

// ================= 基本初期化 =================
bool i2c_init_config(i2c_inst_t *i2c_port, uint32_t i2c_speed, uint sda_pin, uint scl_pin) {
    int ret = i2c_init(i2c_port, i2c_speed);
    if (ret < 0) {
        printf("[ERROR] i2c_init() failed, return = %d\r\n", ret);
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

void i2c_reset(i2c_inst_t *i2c_port, uint32_t i2c_speed, uint sda_pin, uint scl_pin) {
    i2c_deinit_config(i2c_port);
    sleep_ms(100);
    i2c_init_config(i2c_port, i2c_speed, sda_pin, scl_pin);
}

// ================= 読み書き =================
int i2c_read(i2c_inst_t *i2c_port, uint8_t device_addr, uint8_t reg_addr, uint8_t *data, size_t length) {
    if (i2c_write_blocking(i2c_port, device_addr, &reg_addr, 1, true) < 0) return -1;
    if (i2c_read_blocking (i2c_port, device_addr, data, length, false) < 0) return -2;
    return 0;
}

int i2c_write(i2c_inst_t *i2c_port, uint8_t device_addr, uint8_t reg_addr, uint8_t *data, size_t length) {
    uint8_t buf[length + 1];
    buf[0] = reg_addr;
    for (size_t i = 0; i < length; i++) buf[i + 1] = data[i];
    if (i2c_write_blocking(i2c_port, device_addr, buf, length + 1, false) < 0) return -1;
    return 0;
}

int i2c_read_with_timeout(i2c_inst_t *i2c_port, uint8_t device_addr, uint8_t reg_addr, uint8_t *data, size_t length, uint32_t timeout_ms) {
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (to_ms_since_boot(get_absolute_time()) - start < timeout_ms) {
        if (i2c_write_blocking(i2c_port, device_addr, &reg_addr, 1, true) >= 0) {
            if (i2c_read_blocking(i2c_port, device_addr, data, length, false) >= 0) return 0;
        }
        tight_loop_contents();
    }
    printf("I2C Timeout Error (read)\r\n");
    return -1;
}

int i2c_write_with_timeout(i2c_inst_t *i2c_port, uint8_t device_addr, uint8_t reg_addr, uint8_t *data, size_t length, uint32_t timeout_ms) {
    uint8_t buf[length + 1];
    buf[0] = reg_addr;
    for (size_t i = 0; i < length; i++) buf[i + 1] = data[i];

    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (to_ms_since_boot(get_absolute_time()) - start < timeout_ms) {
        if (i2c_write_blocking(i2c_port, device_addr, buf, length + 1, false) >= 0) return 0;
        tight_loop_contents();
    }
    printf("I2C Timeout Error (write)\r\n");
    return -1;
}

// ================= スキャン（従来版） =================
bool check_i2c_device(i2c_inst_t *i2c_port, uint8_t addr) {
    uint8_t dummy = 0;
    // 軽いゼロ長書き込みで存在チェック（タイムアウト1ms）
    int r = i2c_write_timeout_us(i2c_port, addr, &dummy, 0, false, 1000);
    return (r >= 0);
}

int scan_i2c_devices(i2c_inst_t *i2c_port) {
    int found = 0;
    printf("Scanning I2C devices...\r\n");
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (check_i2c_device(i2c_port, addr)) {
            printf("Found device at address 0x%02X\r\n", addr);
            found++;
        }
    }
    if (found == 0) printf("No I2C devices found.\r\n");
    return found ? found : -1;
}

// ================= 追加：I2Cバスクリア =================
void i2c_bus_clear(uint sda_pin, uint scl_pin) {
    // GPIOモードにして手動でSCLをトグル。SDAは入力プルアップ。
    gpio_init(scl_pin);
    gpio_init(sda_pin);
    gpio_set_dir(scl_pin, GPIO_OUT);
    gpio_set_dir(sda_pin, GPIO_IN);
    gpio_pull_up(scl_pin);
    gpio_pull_up(sda_pin);
    sleep_ms(1);

    // 9パルスでクロック回し → STOP条件生成
    for (int i = 0; i < 9; i++) {
        gpio_put(scl_pin, 0); sleep_us(5);
        gpio_put(scl_pin, 1); sleep_us(5);
        if (gpio_get(sda_pin)) break; // SDAが解放されたら終了
    }
    // STOP相当（SCL=HでSDA=Hに解放）
    sleep_us(5);
}

// ================= 追加：固まらない簡易スキャン =================
int scan_i2c_quick(i2c_inst_t *i2c_port) {
    int found = 0;
    uint8_t dummy = 0;
    //printf("Scanning I2C (quick)...\r\n");   // ← 行ごとに出力したくなければコメントアウト

    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        int r = i2c_write_timeout_us(i2c_port, addr, &dummy, 0, false, 1000);
        if (r >= 0) {
            //printf("  Found: 0x%02X\r\n", addr);  // ← 各アドレス出力を削除
            found = addr;                           // 最後に見つかったアドレスを保持
            break;                                  // 最初に見つかったら抜ける
        }
    }

    if (found) printf("Found I2C device at 0x%02X\r\n", found);
    else       printf("No I2C devices found.\r\n");

    return found ? 1 : 0;
}


