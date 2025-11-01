/**
 * @file    si5351_cli.c
 * @brief   Si5351A CLI (USBシリアル制御, CLK0~2独立制御, 入力ゆるめ)
 * @date    2025-11-01
 * @version 1.7.1
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
static uint8_t g_addr = 0x60;

// ===== レジスタ定義 =====
#define XTAL_FREQ                 25000000UL
#define PLLA_FREQ                 800000000UL
#define REG_OE                    0x03
#define REG_CLK0_CTRL             0x10
#define REG_CLK1_CTRL             0x11
#define REG_CLK2_CTRL             0x12
#define REG_MS0_BASE              0x2A
#define REG_MS1_BASE              0x34
#define REG_MS2_BASE              0x3E
#define REG_CRYSTAL_LOAD          0xB7
#define REG_PLLA_BASE             0x1A
#define REG_PLL_RESET             0xB1

static const uint8_t k_clk_ctrl[3] = { REG_CLK0_CTRL, REG_CLK1_CTRL, REG_CLK2_CTRL };
static const uint8_t k_ms_base [3] = { REG_MS0_BASE , REG_MS1_BASE , REG_MS2_BASE  };

static inline void wr8(uint8_t reg, uint8_t v) { i2c_write(g_i2c, g_addr, reg, &v, 1); }
static inline int  rd8(uint8_t reg, uint8_t *v) { return i2c_read(g_i2c, g_addr, reg, v, 1); }

// ===== 初期化 =====
void si5351_cli_init(i2c_inst_t *port, uint8_t addr) {
    g_i2c = port;
    g_addr = addr & 0x7F;
    serial_printf("[CLI] I2C addr=0x%02X", 1, g_addr);
}

static void si5351_init_basic(void) {
    // いったん全出力OFF
    wr8(REG_OE, 0xFF);
    // 水晶負荷キャパシタ設定（8 pF）
    wr8(REG_CRYSTAL_LOAD, 0b11000000);

    // PLLA = 800 MHz (a=32, b=0, c=1 → P1=128a-512=3584)
    uint32_t a=32,b=0,c=1;
    uint32_t P1=128*a-512, P2=0, P3=1;
    uint8_t d[8]={(P3>>8)&0xFF,P3&0xFF,(P1>>16)&0x03,(P1>>8)&0xFF,P1&0xFF,
                  ((P3>>12)&0xF0)|((P2>>16)&0x0F),(P2>>8)&0xFF,P2&0xFF};
    i2c_write(g_i2c,g_addr,REG_PLLA_BASE,d,8);
    wr8(REG_PLL_RESET,0xA0); // PLLA/Bリセット（PLLAのみ使用でも可）

    serial_printf("Si5351A initialized (PLLA=800 MHz)",1);
}

// ===== 周波数設定 =====
static void si5351_set_freq_ch(unsigned ch, unsigned freq_mhz) {
    if (ch>2) {serial_printf("ERR: ch=%u (use 0..2)",1,ch);return;}

    // 0指定でチャンネル停止（OEビットを立てる）
    if (freq_mhz==0){
        uint8_t oe=0xFF; rd8(REG_OE,&oe);
        oe|=(1u<<ch); wr8(REG_OE,oe);
        serial_printf("CLK%u disabled",1,ch);
        return;
    }

    unsigned long freq_hz=(unsigned long)freq_mhz*1000000UL;
    if (freq_hz>150000000UL){serial_printf("Freq too high (>150 MHz)",1);return;}

    // 分周器（整数で近似）
    double divf=(double)PLLA_FREQ/freq_hz;
    unsigned div=(unsigned)(divf+0.5); if(div<4)div=4;

    // MSn: a=div, b=0, c=1 → P1=128a-512
    uint32_t a=div,b=0,c=1;
    uint32_t P1=128*a-512,P2=0,P3=1;
    uint8_t d[8]={(P3>>8)&0xFF,P3&0xFF,(P1>>16)&0x03,(P1>>8)&0xFF,P1&0xFF,
                  ((P3>>12)&0xF0)|((P2>>16)&0x0F),(P2>>8)&0xFF,P2&0xFF};
    i2c_write(g_i2c,g_addr,k_ms_base[ch],d,8);

    // CLKコントロール: 0x4F = （整数分周／PLL A／パワーオン／ソース選択）
    wr8(k_clk_ctrl[ch],0x4F);

    // OEを下げて有効化
    uint8_t oe=0xFF; rd8(REG_OE,&oe);
    oe&=~(1u<<ch); wr8(REG_OE,oe);

    serial_printf("CLK%u = %u MHz (div=%u)",1,ch,freq_mhz,div);
}

// ===== ヘルプ =====
static void cmd_help(void){
    serial_printf("",1);
    serial_printf("==================  HELP MENU (Si5351A)  ==================",1);
    serial_printf(" help / h / H / ?           : show this help",1);
    serial_printf(" scan                       : I2C scan (quiet)",1);
    serial_printf(" whoami                     : read Reg0/Reg1",1);
    serial_printf(" init                       : re-init Si5351A (PLLA=800MHz)",1);
    serial_printf(" freq=<MHz>                 : set CLK0 (compat)",1);
    serial_printf(" clk <ch> <MHz>             : set CLKch (ch=0|1|2, MHz=0 disables)",1);
    serial_printf(" clk0=<MHz> / clk1=<MHz> / clk2=<MHz>",1);
    serial_printf(" ch0=<MHz>  / ch1=<MHz>  / ch2=<MHz>",1);
    serial_printf(" cll0=<MHz> / cll1=<MHz> / cll2=<MHz> (typo tolerant)",1);
    serial_printf(" oe on|off                  : enable/disable all outputs",1);
    serial_printf("",1);
    serial_printf(" Examples:",1);
    serial_printf("  (boot) CLK0=10MHz active, others OFF",1);
    serial_printf("  clk 1 20    -> CLK1 = 20 MHz",1);
    serial_printf("  clk 2 25    -> CLK2 = 25 MHz",1);
    serial_printf("  freq1=50    -> CLK1 = 50 MHz",1);
    serial_printf("  ch2=0       -> disable CLK2",1);
    serial_printf("  cll0=100    -> CLK0 = 100 MHz",1);
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

    // '=' をスペースに置換して "clk0=100" などを "clk0 100" に
    replace_char(buf,'=',' ');

    // 先頭トークン取得
    char *tok = strtok(buf," \t\r\n");
    if(!tok) return;

    // コマンドキーを小文字化
    char key[32]; strncpy(key,tok,sizeof(key)); key[sizeof(key)-1]='\0';
    to_lower_inplace(key);

    // ---- help 系 ----
    if(!strcmp(key,"help") || !strcmp(key,"h") || !strcmp(key,"?")){
        cmd_help(); return;
    }

    // ---- scan ----
    if(!strcmp(key,"scan")){
        scan_i2c_quick(g_i2c);  // 出力控えめのスキャン
        return;
    }

    // ---- whoami ----
    if(!strcmp(key,"whoami")){
        uint8_t r0=0,r1=0; rd8(0x00,&r0); rd8(0x01,&r1);
        serial_printf("Reg0:0x%02X, Reg1:0x%02X",1,r0,r1);
        return;
    }

    // ---- init ----
    if(!strcmp(key,"init")){
        si5351_init_basic();
        return;
    }

    // ---- oe on/off ----
    if(!strcmp(key,"oe")){
        char*m=strtok(NULL," \t\r\n");
        if(!m){ serial_printf("usage: oe on|off",1); return; }
        to_lower_inplace(m);
        if(!strcmp(m,"on"))  { wr8(REG_OE,0x00); serial_printf("OE: ON (all enabled)",1); }
        else if(!strcmp(m,"off")) { wr8(REG_OE,0xFF); serial_printf("OE: OFF (all disabled)",1); }
        else serial_printf("usage: oe on|off",1);
        return;
    }

    // ---- freq（互換）: freq <MHz> / freq=<MHz> → CLK0 ----
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
