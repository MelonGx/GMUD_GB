/* textui.c - 對話框層(自 text.c 拆出;等效 talk.s show_talk_msg 族)
 *
 * 代碼在 bank 25(呼叫端均為 25 端模組或選單會話期的 HOME 處理器)。
 * 讀取來源 OutBuf(WRAM),跨 bank 安全。
 */
#pragma bank 25
#include <gb/gb.h>
#include "text.h"
#include "font.h"
#include "fb.h"
#include "res.h"
#include "textbank_res.h"

/* 等效 stringx.s get_text_data 的 is_npc_data 全段(自 text.c 遷入,
 * 僅 npc.c 同 bank 呼叫):屬性 53B → npc;kf 表;商販貨單;長描述 */
void text_load_npc(uint8_t id)
{
    uint16_t off = text_get_ptr(TC_NPC_DATA, id);
    uint8_t n;

    res_read(&textbank_res, off, (uint8_t *)&npc, NPC_DATA_LEN);
    off += NPC_DATA_LEN;

    n = npc.npc_kfnum << 1;
    if (n) {
        if (n > sizeof(npc_kf))
            n = sizeof(npc_kf);
        res_read(&textbank_res, off, npc_kf, n);
        off += (uint16_t)(npc.npc_kfnum << 1);
    }

    vendor_goods[0] = 0;
    if (npc.npc_pai == 0xFF) {          /* TRADE_PAI:貨單 */
        res_read(&textbank_res, off, vendor_goods, 1);
        off++;
        n = vendor_goods[0];
        if (n) {
            if (n >= sizeof(vendor_goods))
                n = sizeof(vendor_goods) - 1;
            res_read(&textbank_res, off, vendor_goods + 1, n);
            off += vendor_goods[0];
        }
    }

    /* 長描述:0 結尾字串 → npc_desc(上限 200,含結尾雙 0) */
    res_read(&textbank_res, off, npc_desc, 198);
    npc_desc[198] = 0;
    npc_desc[199] = 0;
}

static void hline(uint8_t x0, uint8_t x1, uint8_t y)
{
    uint8_t x;
    for (x = x0; x <= x1; x++)
        fb[(uint16_t)y * FB_STRIDE + (x >> 3)] |= 0x80 >> (x & 7);
    fb_mark_dirty(y, 1);
}

static void vline(uint8_t x, uint8_t y0, uint8_t y1)
{
    uint8_t y;
    for (y = y0; y <= y1; y++)
        fb[(uint16_t)y * FB_STRIDE + (x >> 3)] |= 0x80 >> (x & 7);
    fb_mark_dirty(y0, y1 - y0 + 1);
}

static void wait_key_any(void)
{
    waitpadup();
    while (!(joypad() & (J_A | J_B | J_START)))
        vsync();
    waitpadup();
}

/* 通用分頁框:顯示 OutBuf(項=行,超寬軟換行) */
static void box_pages(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1,
                      uint8_t wait_final)
{
    const uint8_t *p = OutBuf;
    uint8_t nl = (uint8_t)(y1 - y0 - 2) / 13;
    uint8_t line, x, w, done = 0;

    if (!nl)
        nl = 1;
    while (!done) {
        fb_fill_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, 0);
        hline(x0, x1, y0);
        hline(x0, x1, y1);
        vline(x0, y0, y1);
        vline(x1, y0, y1);

        line = 0;
        x = x0 + 2;
        while (line < nl) {
            if (*p == 0) {
                if (p[1] == 0) {
                    done = 1;
                    break;
                }
                p++;
                line++;
                x = x0 + 2;
                continue;
            }
            w = (p[0] >= 0xA1 && p[1] >= 0xA1) ? 12 : 6;
            if (x + w > x1 - 1) {
                line++;
                x = x0 + 2;
                continue;
            }
            if (w == 12) {
                font_draw_cjk(x, y0 + 2 + line * 13,
                              ((uint16_t)p[0] << 8) | p[1]);
                p += 2;
            } else {
                font_draw_ascii(x, y0 + 2 + line * 13, *p);
                p += 1;
            }
            x += w;
        }
        fb_flush();
        if (!done || wait_final)
            wait_key_any();
    }
}

void show_talk_msg(void) BANKED
{
    box_pages(1, 0, 159, 28, 1);
}

/* Pause between overflow pages, but let fight.c handle the final key. */
void show_talk_msg_no_wait(void) BANKED
{
    box_pages(1, 0, 159, 28, 0);
}

/* 等效 talk.s show_talk_msg0(key=CR|80h 回推):畫頂框首兩行不等鍵,
 * 供交易/學習選單當標題;僅用於 ≤2 行訊息 */
void show_talk_msg0(void) BANKED
{
    const uint8_t *p = OutBuf;
    uint8_t line, x, w;

    fb_fill_rect(1, 0, 159, 29, 0);
    hline(1, 159, 0);
    hline(1, 159, 28);
    vline(1, 0, 28);
    vline(159, 0, 28);

    for (line = 0, x = 3; line < 2 && *p; ) {
        if (*p == 0) {
            if (p[1] == 0)
                break;
            p++;
            line++;
            x = 3;
            continue;
        }
        w = (*p & 0x80) ? 12 : 6;
        if (x + w > 158) {
            line++;
            x = 3;
            continue;
        }
        if (w == 12) {
            font_draw_cjk(x, 2 + line * 13, ((uint16_t)p[0] << 8) | p[1]);
            p += 2;
        } else {
            font_draw_ascii(x, 2 + line * 13, *p);
            p += 1;
        }
        x += w;
    }
    fb_flush();
}

void message_box_more(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) BANKED
{
    box_pages(x0, y0, x1, y1, 1);
}
