#include <gb/gb.h>
#include <string.h>
#include "text.h"
#include "res.h"
#include "textbank_res.h"
#include "font.h"
#include "fb.h"
#include "blit.h"
#include "game.h"
#include "save.h"
#include "goods.h"
#include "task.h"       /* task_escape($t/$q/$g/$k/$p) */

npc_state_t npc;
uint8_t npc_kf[40];
uint8_t vendor_goods[10];
uint8_t obj_flag;
uint8_t varbuf[16];
uint8_t limb_flag;      /* 命中部位隨機值 0..15(fight.c 設定,$l/$1 用) */

/* 連線對戰協議變量(nf.s game_buf 塊;單機恆 0)。net_vars 順序 =
 * 原版封包協議頭前 7 字節,nf.c 整塊打包/解包,勿重排 */
uint8_t net_flag;
uint8_t net_vars[7];

/* 等效 string.s limbs_data:16 部位 × 2 字(4B),$l/$1 依 limb_flag 取 */
static const uint8_t limbs_data[64] = {
    0xCD,0xB7,0xB2,0xBF, 0xBE,0xB1,0xB2,0xBF, 0xD0,0xD8,0xBF,0xDA,
    0xBA,0xF3,0xD0,0xC4, 0xD7,0xF3,0xBC,0xE7, 0xD3,0xD2,0xBC,0xE7,   /* 後心→后心 */
    0xD7,0xF3,0xB1,0xDB, 0xD3,0xD2,0xB1,0xDB, 0xD7,0xF3,0xCA,0xD6,
    0xD3,0xD2,0xCA,0xD6, 0xD1,0xFC,0xBC,0xE4, 0xD0,0xA1,0xB8,0xB9,
    0xD7,0xF3,0xCD,0xC8, 0xD3,0xD2,0xCD,0xC8, 0xD7,0xF3,0xBD,0xC5,
    0xD3,0xD2,0xBD,0xC5,
};

static uint8_t you_char[2] = { 0xC4, 0xE3 };    /* 「你」(原版 you_char) */

uint16_t text_get_ptr(uint8_t cls, uint8_t id)
{
    uint16_t tbl = res_read16(&textbank_res, (uint16_t)cls << 1);
    return res_read16(&textbank_res, tbl + ((uint16_t)id << 1));
}

/* text_load_npc 已遷 textui.c(bank25,僅 npc.c 同 bank 呼叫)騰 HOME */

/* ---- format_string(stringx.s 全數值類型 + $ 轉義) ----
 * C 端訊息中的 dw 位址即本機指針(textbank 內文本的原版位址
 * 轉換表隨 quest 模組接入)。 */
static uint8_t *fs_out;

static void put_str(const uint8_t *s)       /* 到 0/FF/' ' 止(名字類) */
{
    uint8_t c;
    while ((c = *s++) != 0 && c != 0xFF && c != ' ')
        *fs_out++ = c;
}

static void put_escape(uint8_t c);

static void put_str_fmt(const uint8_t *s)   /* 到 0/FF 止,處理 $ */
{
    uint8_t c;
    while ((c = *s) != 0 && c != 0xFF) {
        s++;
        if (c == '$') {
            put_escape(*s++);
        } else {
            *fs_out++ = c;
        }
    }
}

/* 16 位數:5 位去前導零;width>0 時不足補「後綴」空格(write_digit5) */
static void put_digits16(uint16_t v, uint8_t width)
{
    uint8_t b[5];
    uint8_t i = 5, n;

    do {
        b[--i] = '0' + (uint8_t)(v % 10);
        v /= 10;
    } while (v && i);
    n = 5 - i;
    while (i < 5)
        *fs_out++ = b[i++];
    while (width > n) {
        *fs_out++ = ' ';
        n++;
    }
}

static void put_digits32(uint32_t v)        /* type4:10 位去前導零 */
{
    uint8_t b[10];
    uint8_t i = 10;

    do {
        b[--i] = '0' + (uint8_t)(v % 10);
        v /= 10;
    } while (v && i);
    while (i < 10)
        *fs_out++ = b[i++];
}

static void put_escape(uint8_t c)
{
    switch (c) {
    case '$':
        *fs_out++ = '$';
        break;
    case 'r':
        *fs_out++ = 0;
        break;
    case 'N':
    case 'n':
        if ((c == 'N') == ((obj_flag & 0x80) != 0)) {
            *fs_out++ = you_char[0];
            *fs_out++ = you_char[1];
        } else {
            put_str(npc.npc_name);
        }
        break;
    case 'o':
        put_str(npc.npc_name);
        break;
    case 'm':
        put_str(hero.man_name);
        break;
    case 'l':                               /* 命中部位(等效 $l) */
    case '1':                               /* 同 $l(絕招文本用 $1) */
        {
            const uint8_t *lp = limbs_data + ((uint16_t)(limb_flag & 15) << 2);
            *fs_out++ = lp[0];
            *fs_out++ = lp[1];
            *fs_out++ = lp[2];
            *fs_out++ = lp[3];
        }
        break;
    case 'w':                               /* 兵器名(man/npc 視角) */
        {
            uint8_t wpn = (obj_flag & 0x80) ? hero.man_weapon
                                            : npc.npc_goods[0];
            put_str(get_goods_name(wpn & 0x7F));
        }
        break;
    case 't':                               /* 任務類($t$q$g$k$p 主體 */
    case 'q':                               /* 在 bank28 task_escape, */
    case 'g':                               /* 省 HOME 空間;寫入 */
    case 'k':                               /* OutBuf 為 WRAM,跨 bank */
    case 'p':                               /* 安全) */
        fs_out = task_escape(c, fs_out);
        break;
    default:
        *fs_out++ = '?';
        break;
    }
}

static void put_value(uint8_t type, const uint8_t *p)
{
    switch (type) {
    case 1:
        put_digits16(p[0], 0);
        break;
    case 2:
        put_digits16((uint16_t)p[0] | ((uint16_t)p[1] << 8), 0);
        break;
    case 3:
        put_digits16(p[0], 3);
        break;
    case 5:
        put_digits16((uint16_t)p[0] | ((uint16_t)p[1] << 8), 5);
        break;
    case 4: {
        uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8)
                   | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        put_digits32(v);
        break;
    }
    case 6:                                 /* 字符(高位=雙字節) */
        *fs_out++ = p[0];
        if (p[0] & 0x80)
            *fs_out++ = p[1];
        break;
    case 7:                                 /* 字串位址 */
        put_str_fmt(p);
        break;
    case 8: {                               /* 字串指針 */
        const uint8_t *q =
            (const uint8_t *)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
        if (q)
            put_str_fmt(q);
        break;
    }
    case 9: {                               /* 長度+指針槽(二次間接) */
        uint8_t n = p[0];
        const uint8_t *slot =
            (const uint8_t *)((uint16_t)p[1] | ((uint16_t)p[2] << 8));
        const uint8_t *q =
            (const uint8_t *)((uint16_t)slot[0] | ((uint16_t)slot[1] << 8));
        while (n--)
            *fs_out++ = *q++;
        break;
    }
    default:
        break;
    }
}

void format_string(const uint8_t *src)
{
    uint8_t c, type;

    fs_out = OutBuf;
    for (;;) {
        c = *src++;
        if (c == '$') {
            put_escape(*src++);
            continue;
        }
        if (c > 15) {
            *fs_out++ = c;
            continue;
        }
        /* c ≤ 15:類型字節;0 = 行終(下一字節再為 0 → 全文終) */
        if (c == 0) {
            *fs_out++ = 0;
            if (*src == 0)
                break;
            continue;
        }
        type = c;
        for (;;) {                          /* value_loop:位址對序列 */
            uint16_t w = (uint16_t)src[0] | ((uint16_t)src[1] << 8);
            src += 2;
            if (w == 0) {                   /* dw 0:行終 */
                *fs_out++ = 0;
                break;
            }
            if (w == 10)
                break;                      /* dw 10:續行 */
            put_value(type, (const uint8_t *)w);
        }
        if (*src == 0 && fs_out[-1] == 0)   /* 行終後全文終 */
            break;
    }
    *fs_out = 0;                            /* 雙 0 收尾 */
}
