/**
 * @file    si5351_cli.c
 * @brief   Si5351A CLI (USBシリアル制御, CLK0~2独立制御, 診断コマンド追加版)
 * @date    2025-11-03
 * @version 2.0
 */

#include "si5351_cli.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include "i2c_comm.h"
#include "serial_comm.h"

// ===== 内部状態 =====
static i2c_inst_t *g_i2c = NULL;
static uint8_t g_addr = 0x60;   // AE-Si5351A 固定（7-bit）

// ===== レジスタ定義 =====
#define XTAL_FREQ                 25000000UL
#define PLLA_FREQ                 800000000UL
#define REG_STAT0                 0x00   // SYS_INIT/LOL_A/LOL_B/LOS 等
#define REG_OE                    0x03
#define REG_CLK0_CTRL             0x10
#define REG_CLK1_CTRL             0x11
#define REG_CLK2_CTRL             0x12
#define REG_MS0_BASE              0x2A   // 0x2A..0x31
#define REG_MS1_BASE              0x34
#define REG_MS2_BASE              0x3E
#define REG_CRYSTAL_LOAD          0xB7
#define REG_PLLA_BASE             0x1A   // 0x1A..0x21
#define REG_PLL_RESET             0xB1

static const uint8_t k_clk_ctrl[3] = { REG_CLK0_CTRL, REG_CLK1_CTRL, REG_CLK2_CTRL };
static const uint8_t k_ms_base [3] = { REG_MS0_BASE , REG_MS1_BASE , REG_MS2_BASE  };

// ===== ラッパ =====
static inline int wr8(uint8_t reg, uint8_t v) {
    int rc = i2c_write(g_i2c, g_addr, reg, &v, 1);
    if (rc != 0) serial_printf("[I2C] WR FAIL reg=0x%02X val=0x%02X", 1, reg, v);
    return rc;
}
static inline int rd8(uint8_t reg, uint8_t *v) {
    int rc = i2c_read(g_i2c, g_addr, reg, v, 1);
    if (rc != 0) serial_printf("[I2C] RD FAIL reg=0x%02X", 1, reg);
    return rc;
}

// ===== 内部ユーティリティ =====
static void set_ms_intdiv(uint8_t ms_base, uint16_t div) {
    // 整数分周: a=div, b=0, c=1 → P1=128a-512, P2=0, P3=1
    if (div < 4) div = 4;
    uint32_t a = div, b = 0, c = 1;
    uint32_t P1 = 128*a - 512, P2 = 0, P3 = 1;
    uint8_t d[8] = {
        (P3>>8)&0xFF, (uint8_t)(P3&0xFF),
        (uint8_t)((P1>>16)&0x03), (uint8_t)((P1>>8)&0xFF), (uint8_t)(P1&0xFF),
        (uint8_t)(((P3>>12)&0xF0)|((P2>>16)&0x0F)),
        (uint8_t)((P2>>8)&0xFF), (uint8_t)(P2&0xFF)
    };
    int rc = i2c_write(g_i2c, g_addr, ms_base, d, 8);
    if (rc != 0) serial_printf("[I2C] WR FAIL MS@0x%02X (div=%u)", 1, ms_base, div);
}

static void clk_ctrl_set(uint8_t reg_clk_ctrl, uint8_t val) {
    // 0x4F: Power ON / PLLA / integer / 非反転 / 8mA
    (void)wr8(reg_clk_ctrl, val);
}

static void oe_mask_all(uint8_t mask) {
    // mask=0xFF 全OFF, mask=0xFE CLK0のみON, etc.
    (void)wr8(REG_OE, mask);
}

// ===== 初期化 =====
void si5351_cli_init(i2c_inst_t *port, uint8_t addr) {
    g_i2c = port;
    g_addr = addr & 0x7F;
    serial_printf("[CLI] I2C addr=0x%02X", 1, g_addr);
}

static void si5351_init_basic(void) {
    // 1) まず全出力OFF（OE全閉）
    oe_mask_all(0xFF);

    // 2) 水晶負荷キャパシタ（8 pF）
    wr8(REG_CRYSTAL_LOAD, 0b10000000); //wr8(REG_CRYSTAL_LOAD, 0b11000000);

    // 3) PLLA=800MHz (整数: a=32)
    {
        uint32_t a=32,b=0,c=1;
        uint32_t P1=128*a-512, P2=0, P3=1;
        uint8_t d[8]={(P3>>8)&0xFF,(uint8_t)(P3&0xFF),
                      (uint8_t)((P1>>16)&0x03),(uint8_t)((P1>>8)&0xFF),(uint8_t)(P1&0xFF),
                      (uint8_t)(((P3>>12)&0xF0)|((P2>>16)&0x0F)),
                      (uint8_t)((P2>>8)&0xFF),(uint8_t)(P2&0xFF)};
        int rc = i2c_write(g_i2c, g_addr, REG_PLLA_BASE, d, 8);
        if (rc != 0) serial_printf("[I2C] WR FAIL PLLA", 1);
        wr8(REG_PLL_RESET, 0xA0); // PLLA/Bリセット
    }

    // 4) CLK0=100MHz → 800/8
    set_ms_intdiv(REG_MS0_BASE, 8);
    clk_ctrl_set(REG_CLK0_CTRL, 0x4F); // Power ON / PLLA / integer

    // 5) CLK1/2は停止構成にしておく
    clk_ctrl_set(REG_CLK1_CTRL, 0x8F); // bit7=1(PD)
    clk_ctrl_set(REG_CLK2_CTRL, 0x8F);

    // 6) CLK0のみ出力ON
    oe_mask_all(0xFE); // bit0=0 → CLK0有効

    serial_printf("Si5351A initialized (PLLA=800 MHz, CLK0=100 MHz, CLK1/2 off)", 1);
}

// ===== 周波数設定（整数） =====
static void si5351_set_freq_ch(unsigned ch, unsigned freq_mhz) {
    if (ch > 2) { serial_printf("ERR: ch=%u (use 0..2)", 1, ch); return; }

    if (freq_mhz == 0) {
        // 停止：OEビットを立てる
        uint8_t oe=0xFF; rd8(REG_OE,&oe);
        oe |= (1u<<ch); wr8(REG_OE, oe);
        serial_printf("CLK%u disabled", 1, ch);
        return;
    }

    unsigned long freq_hz = (unsigned long)freq_mhz * 1000000UL;
    if (freq_hz > 150000000UL) { serial_printf("Freq too high (<150 MHz)", 1); return; }

    // 分周器（整数）
    double divf = (double)PLLA_FREQ / (double)freq_hz;
    unsigned div = (unsigned)(divf + 0.5);
    if (div < 4) div = 4;

    set_ms_intdiv(k_ms_base[ch], (uint16_t)div);

    // CLKコントロール: 電源ON/PLLA/整数/非反転/8mA
    clk_ctrl_set(k_clk_ctrl[ch], 0x4F);

    // OEで ch を有効化
    uint8_t oe=0xFF; rd8(REG_OE,&oe);
    oe &= ~(1u<<ch); wr8(REG_OE, oe);

    serial_printf("CLK%u = %u MHz (div=%u)", 1, ch, freq_mhz, div);
}

// ===== ヘルプ =====
static void cmd_help(void){
    serial_printf("",1);
    serial_printf("==================  HELP MENU (Si5351A)  ==================",1);
    serial_printf(" help / h / H / ?           : show this help",1);
    serial_printf(" scan                       : I2C scan (quiet)",1);
    serial_printf(" status                     : show STAT0/OE/CLK0_CTRL",1);
    serial_printf(" peek <hexReg>              : read  1 byte from reg",1);
    serial_printf(" poke <hexReg> <hexVal>     : write 1 byte to reg",1);
    serial_printf(" init                       : re-init (PLLA=800MHz, CLK0=100MHz)",1);
    serial_printf(" force_on                   : OE/CLK0 を強制有効化",1);
    serial_printf(" freq=<MHz>                 : set CLK0 (compat)",1);
    serial_printf(" clk <ch> <MHz>             : set CLKch (MHz=0 disables)",1);
    serial_printf(" clk0=<MHz> / clk1=<MHz> / clk2=<MHz>",1);
    serial_printf(" ch0=<MHz>  / ch1=<MHz>  / ch2=<MHz>",1);
    serial_printf("==========================================================",1);
    serial_printf("",1);
}

// ===== 文字列ヘルパ =====
static void to_lower_inplace(char *s){ for(char*p=s;*p;++p)*p=(char)tolower(*p); }
static void replace_char(char *s, char from, char to){ for(char*p=s;*p;++p) if(*p==from)*p=to; }

// ===== CLIハンドラ =====
void si5351_cli_handle(const char *cmd){
    if(!cmd || !*cmd) return;

    char buf[128];
    strncpy(buf,cmd,sizeof(buf)); buf[sizeof(buf)-1]='\0';
    replace_char(buf,'=',' ');

    char *tok = strtok(buf," \t\r\n");
    if(!tok) return;

    char key[32]; strncpy(key,tok,sizeof(key)); key[sizeof(key)-1]='\0';
    to_lower_inplace(key);

    // ---- help ----
    if(!strcmp(key,"help") || !strcmp(key,"h") || !strcmp(key,"?")){
        cmd_help(); return;
    }

    // ---- scan ----
    if(!strcmp(key,"scan")){
        scan_i2c_quick(g_i2c);
        return;
    }

    // ---- status ----
    if(!strcmp(key,"status")){
        uint8_t s=0, oe=0, c0=0;
        rd8(REG_STAT0,&s); rd8(REG_OE,&oe); rd8(REG_CLK0_CTRL,&c0);
        serial_printf("STAT0=0x%02X  OE=0x%02X  CLK0_CTRL=0x%02X",1,s,oe,c0);
        // 参考: STAT0 bits: SYS_INIT=bit7, LOL_B=6, LOL_A=5, LOS=4
        return;
    }

    // ---- peek ----
    if(!strcmp(key,"peek")){
        char*ra=strtok(NULL," \t\r\n");
        if(!ra){ serial_printf("usage: peek <hexReg>",1); return; }
        unsigned reg=strtoul(ra,NULL,16);
        uint8_t v=0xFF;
        if (i2c_read(g_i2c,g_addr,(uint8_t)reg,&v,1)==0)
            serial_printf("REG[0x%02X]=0x%02X",1,(unsigned)reg,v);
        else
            serial_printf("READ FAIL reg=0x%02X",1,(unsigned)reg);
        return;
    }

    // ---- poke ----
    if(!strcmp(key,"poke")){
        char*ra=strtok(NULL," \t\r\n");
        char*va=strtok(NULL," \t\r\n");
        if(!ra||!va){ serial_printf("usage: poke <hexReg> <hexVal>",1); return; }
        unsigned reg=strtoul(ra,NULL,16);
        unsigned val=strtoul(va,NULL,16);
        (void)wr8((uint8_t)reg,(uint8_t)val);
        return;
    }

    // ---- init ----
    if(!strcmp(key,"init")){
        si5351_init_basic();
        return;
    }

    // ---- force_on（強制有効化）----
    if(!strcmp(key,"force_on")){
        // 1) 全閉 → 2) CLK0だけ開 → 3) CLK0_CTRL=0x4F
        oe_mask_all(0xFF);
        oe_mask_all(0xFE);
        clk_ctrl_set(REG_CLK0_CTRL, 0x4F);
        // 読みバック
        uint8_t oe=0, c0=0; rd8(REG_OE,&oe); rd8(REG_CLK0_CTRL,&c0);
        serial_printf("FORCE: OE=0x%02X CLK0_CTRL=0x%02X",1,oe,c0);
        return;
    }

    // ---- oe on/off ----
    if(!strcmp(key,"oe")){
        char*m=strtok(NULL," \t\r\n");
        if(!m){ serial_printf("usage: oe on|off",1); return; }
        to_lower_inplace(m);
        if(!strcmp(m,"on"))  { oe_mask_all(0x00); serial_printf("OE: ON (all enabled)",1); }
        else if(!strcmp(m,"off")) { oe_mask_all(0xFF); serial_printf("OE: OFF (all disabled)",1); }
        else serial_printf("usage: oe on|off",1);
        return;
    }

    // ---- freq（互換）: freq <MHz> → CLK0 ----
    if(!strcmp(key,"freq")){
        char*p=strtok(NULL," \t\r\n");
        if(!p){ serial_printf("usage: freq <MHz>",1); return; }
        si5351_set_freq_ch(0,(unsigned)atoi(p));
        return;
    }

    // ---- 汎用: clk <ch> <MHz> ----
    if(!strcmp(key,"clk")){
        char*ch=strtok(NULL," \t\r\n");
        char*mhz=strtok(NULL," \t\r\n");
        if(!ch||!mhz){ serial_printf("usage: clk <ch:0|1|2> <MHz>",1); return; }
        si5351_set_freq_ch((unsigned)atoi(ch),(unsigned)atoi(mhz));
        return;
    }

    // ---- 個別: freqX / chX / clkX / cllX ----
    for(int i=0;i<3;i++){
        char pat1[8],pat2[8],pat3[8],pat4[8];
        sprintf(pat1,"freq%d",i); sprintf(pat2,"ch%d",i);
        sprintf(pat3,"clk%d",i);  sprintf(pat4,"cll%d",i); // typo tolerant
        if(!strcmp(key,pat1) || !strcmp(key,pat2) || !strcmp(key,pat3) || !strcmp(key,pat4)){
            char*p=strtok(NULL," \t\r\n");
            si5351_set_freq_ch((unsigned)i, p ? (unsigned)atoi(p) : 0U);
            return;
        }
    }

    // ---- 不明コマンド ----
    serial_printf("Unknown command. Type 'help' / 'H' / '?'",1);
}
