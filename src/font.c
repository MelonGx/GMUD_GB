#include <gb/gb.h>
#include <string.h>
#include "font.h"
#include "font12_data.h"
#include "font16_data.h"
#include "fb.h"

/* 字體集描述(常駐 ROM0)。碼表已分家(2026-07-18):font12=全一级字
 * (拼音輸入名字任選),font16=16px 實際用字小碼表 */
typedef struct {
    const uint8_t *const *chunks;
    const uint8_t *ascii;       /* 95 × cell_h 字節 */
    const uint16_t *codes;      /* GB 碼表(升序,MISC bank) */
    uint16_t count;
    uint8_t cell_h;
    uint8_t glyph_bytes;
    uint8_t first_bank;
    uint16_t per_bank;
    uint8_t cjk_w, ascii_w;
} fontset_t;

static const fontset_t set12 = {
    font12_chunks, (const uint8_t *)font12_ascii, font12_codes, FONT12_COUNT,
    FONT12_CELL_H, FONT12_GLYPH_BYTES, FONT12_FIRST_BANK, FONT12_PER_BANK,
    12, 6,
};
static const fontset_t set16 = {
    font16_chunks, (const uint8_t *)font16_ascii, font16_codes, FONT16_COUNT,
    FONT16_CELL_H, FONT16_GLYPH_BYTES, FONT16_FIRST_BANK, FONT16_PER_BANK,
    16, 8,
};

static const fontset_t *cur = &set12;
static uint8_t rep_mode;    /* 0=OR 疊加,1=覆寫(等效原版系統字模行為) */

void font_set_height(uint8_t h)
{
    cur = (h == 16) ? &set16 : &set12;
}

/* 二分查找 GB 碼 → 字模索引(當前字體碼表,在 MISC bank);缺字回傳 0 */
static uint16_t cjk_index(uint16_t code)
{
    uint16_t lo = 0, hi = cur->count - 1, mid;
    uint16_t r = 0;
    uint8_t save = _current_bank;

    SWITCH_ROM(FONT12_MISC_BANK);
    while (lo <= hi) {
        mid = (lo + hi) >> 1;
        if (cur->codes[mid] == code) {
            r = mid;
            break;
        }
        if (cur->codes[mid] < code) {
            lo = mid + 1;
        } else {
            if (mid == 0)
                break;
            hi = mid - 1;
        }
    }
    SWITCH_ROM(save);
    return r;
}

/* 把 16bit 行資料(bit15 最左,寬 w 位)畫進 fb;覆寫模式邊緣位精確 */
static void blit_rows(uint8_t x, uint8_t y, const uint8_t *g,
                      uint8_t n, uint8_t w, uint8_t two_bytes)
{
    uint8_t shift = x & 7;
    uint8_t bx = x >> 3;
    uint8_t r;
    uint16_t row;
    uint8_t *p;
    uint8_t d0, d1, d2;
    uint8_t m0 = 0, m1 = 0, m2 = 0;

    if (rep_mode) {     /* 有效位遮罩:w 位從 shift 起跨 3 字節 */
        uint32_t m = (0xFFFFFFUL << (24 - w)) & 0xFFFFFFUL;
        m >>= shift;
        m0 = (uint8_t)(m >> 16);
        m1 = (uint8_t)(m >> 8);
        m2 = (uint8_t)m;
    }

    for (r = 0; r < n; r++) {
        if (two_bytes) {
            row = ((uint16_t)g[0] << 8) | g[1];
            g += 2;
        } else {
            row = (uint16_t)*g++ << 8;
        }
        d0 = (uint8_t)(row >> (8 + shift));
        d1 = (uint8_t)(row >> shift);
        d2 = shift ? (uint8_t)((row & 0xFF) << (8 - shift)) : 0;

        p = &fb[(uint16_t)(y + r) * FB_STRIDE + bx];
        if (rep_mode) {
            p[0] = (uint8_t)((p[0] & ~m0) | d0);
            if (bx + 1 < FB_STRIDE)
                p[1] = (uint8_t)((p[1] & ~m1) | d1);
            if (m2 && bx + 2 < FB_STRIDE)
                p[2] = (uint8_t)((p[2] & ~m2) | d2);
        } else {
            p[0] |= d0;
            if (bx + 1 < FB_STRIDE)
                p[1] |= d1;
            if (bx + 2 < FB_STRIDE)
                p[2] |= d2;
        }
    }
    fb_mark_dirty(y, n);
}

uint8_t font_draw_cjk(uint8_t x, uint8_t y, uint16_t code)
{
    static uint8_t glyph[32];
    uint16_t idx = cjk_index(code);
    uint8_t bank_save;

    bank_save = _current_bank;
    SWITCH_ROM(cur->first_bank + idx / cur->per_bank);
    memcpy(glyph,
           cur->chunks[idx / cur->per_bank]
               + (idx % cur->per_bank) * (uint16_t)cur->glyph_bytes,
           cur->glyph_bytes);
    SWITCH_ROM(bank_save);

    blit_rows(x, y, glyph, cur->cell_h, cur->cjk_w, 1);
    return cur->cjk_w;
}

uint8_t font_draw_ascii(uint8_t x, uint8_t y, uint8_t c)
{
    uint8_t g[16];
    uint8_t save = _current_bank;

    if (c < 0x20 || c > 0x7E)
        c = ' ';
    SWITCH_ROM(FONT12_MISC_BANK);   /* ascii 字模在 MISC bank */
    memcpy(g, cur->ascii + (uint16_t)(c - 0x20) * cur->cell_h, cur->cell_h);
    SWITCH_ROM(save);
    blit_rows(x, y, g, cur->cell_h, cur->ascii_w, 0);
    return cur->ascii_w;
}

static uint8_t draw_text(uint8_t x, uint8_t y, const uint8_t *s)
{
    uint8_t b;
    while ((b = *s) != 0) {
        if (b >= 0xA1 && s[1] >= 0xA1) {
            x += font_draw_cjk(x, y, ((uint16_t)b << 8) | s[1]);
            s += 2;
        } else {
            x += font_draw_ascii(x, y, b);
            s++;
        }
    }
    return x;
}

uint8_t font_draw_text(uint8_t x, uint8_t y, const uint8_t *s)
{
    rep_mode = 0;
    return draw_text(x, y, s);
}

uint8_t font_draw_text_replace(uint8_t x, uint8_t y, const uint8_t *s)
{
    uint8_t end;
    rep_mode = 1;
    end = draw_text(x, y, s);
    rep_mode = 0;
    return end;
}
