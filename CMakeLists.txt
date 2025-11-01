cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

project(Si5351A_Osc C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# これを一度だけ追加
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# === Pico SDK 初期化 ===
pico_sdk_init()

# === ソースファイル ===
add_executable(Si5351A_Osc
    Si5351A_Osc.c
    si5351_cli.c
    serial_comm.c
    i2c_comm.c
    led_blink.c
)

# === ヘッダ検索パス ===
target_include_directories(Si5351A_Osc PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

# === リンクライブラリ ===
target_link_libraries(Si5351A_Osc
    pico_stdlib
    hardware_i2c
    hardware_gpio
    hardware_timer
)

# ✅ USBシリアルを有効化
pico_enable_stdio_usb(Si5351A_Osc 1)
pico_enable_stdio_uart(Si5351A_Osc 0)

# === 出力バイナリ生成 ===
pico_add_extra_outputs(Si5351A_Osc)
