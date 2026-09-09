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

/* 一级字直算的起點索引:l1_base[0]=set12,[1]=set16;0xFFFF=尚未探測,
 * 0xFFFE=該碼表沒有完整一级字區(font16 是小碼表),永久走二分。 */
static uint16_t l1_base[2] = { 0xFFFF, 0xFFFF };

static uint16_t bsearch_code(uint16_t code)
{
    uint16_t lo = 0, hi = cur->count - 1, mid;
    uint16_t r = 0;

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
    return r;
}

/* GB 碼 → 字模索引(當前字體碼表,在 MISC bank);缺字回傳 0
 *
 * 攻略整頁 143 個全形字,二分查找 3872 項要 12 探,實測佔整頁 ~10%。
 * charset12 含全部一级字(B0A1..D7F9),碼表升序 ⇒ 該段在索引空間裡是
 * 連續的(B0..D6 每列滿 94 字,D7 到 D7F9 共 89 字),可直接算:
 *     idx = l1_base + (hi-0xB0)*94 + (lo-0xA1)
 * 算完仍比對 codes[idx] 自我校驗,不合(font16 小碼表/非一级字/缺字)
 * 才回退二分,所以對任何碼表都安全。 */
static uint16_t cjk_index(uint16_t code)
{
    uint8_t save = _current_bank;
    uint8_t which = (cur == &set12) ? 0 : 1;
    uint8_t chi = (uint8_t)(code >> 8);
    uint8_t clo = (uint8_t)code;
    uint16_t b, r;

    SWITCH_ROM(FONT12_MISC_BANK);
    b = l1_base[which];
    if (b == 0xFFFF) {
        b = bsearch_code(0xB0A1);
        if (cur->codes[b] != 0xB0A1)
            b = 0xFFFE;
        l1_base[which] = b;
    }
    if (b != 0xFFFE && chi >= 0xB0 && chi <= 0xD7 &&
        clo >= 0xA1 && clo <= 0xFE) {
        r = b + (uint16_t)(chi - 0xB0) * 94 + (clo - 0xA1);
        if (r < cur->count && cur->codes[r] == code) {
            SWITCH_ROM(save);
            return r;
        }
    }
    r = bsearch_code(code);
    SWITCH_ROM(save);
    return r;
}

/* 把 16bit 行資料(bit15 最左,寬 w 位)畫進 fb;覆寫模式邊緣位精確
 *
 * 內迴圈是全遊戲最熱的一段(攻略整頁 11 行 ≈ 143 個全形字),寫法刻意
 * 避開 SDCC 的慢路徑:
 *  - fb 行指標改成迴圈外算一次、每行 += FB_STRIDE,省掉每行一次
 *    (y+r)*20 的 16 位乘法;
 *  - 位移全部落在 8 位。原本 row>>(8+shift) / row>>shift 是「16 位變數
 *    位移」,SDCC 走 __rrushort 輔助函式(帶呼叫開銷、每位 ~16 cy);
 *    拆成 g0/g1 兩個字節後同樣結果只要 8 位變數位移(djnz 迴圈 ~8 cy/位),
 *    且 shift==0(x 為 8 的倍數)直接短路不位移;
 *  - 邊界判斷與遮罩取反在迴圈外算好。
 * 實測攻略每行 CJK 繪製 2.5 幀 → 1.7 幀。 */
static void blit_rows(uint8_t x, uint8_t y, const uint8_t *g,
                      uint8_t n, uint8_t w, uint8_t two_bytes)
{
    uint8_t shift = x & 7;
    uint8_t bx = x >> 3;
    uint8_t rsh = 8 - shift;
    uint8_t rep = rep_mode;
    uint8_t ok1 = (uint8_t)(bx + 1) < FB_STRIDE;
    uint8_t ok2 = (uint8_t)(bx + 2) < FB_STRIDE;
    uint8_t r, g0, g1;
    uint8_t *p;
    uint8_t d0, d1, d2;
    uint8_t m2 = 0, n0 = 0, n1 = 0, n2 = 0;

    if (rep) {          /* 有效位遮罩:w 位從 shift 起跨 3 字節 */
        uint32_t m = (0xFFFFFFUL << (24 - w)) & 0xFFFFFFUL;
        m >>= shift;
        n0 = (uint8_t)~(uint8_t)(m >> 16);
        n1 = (uint8_t)~(uint8_t)(m >> 8);
        m2 = (uint8_t)m;
        n2 = (uint8_t)~m2;
    }

    p = &fb[(uint16_t)y * FB_STRIDE + bx];
    for (r = 0; r < n; r++, p += FB_STRIDE) {
        g0 = *g++;
        g1 = two_bytes ? *g++ : 0;
        if (shift) {
            d0 = g0 >> shift;
            d1 = (uint8_t)((uint8_t)(g0 << rsh) | (uint8_t)(g1 >> shift));
            d2 = (uint8_t)(g1 << rsh);
        } else {
            d0 = g0;
            d1 = g1;
            d2 = 0;
        }
        if (rep) {
            p[0] = (uint8_t)((p[0] & n0) | d0);
            if (ok1)
                p[1] = (uint8_t)((p[1] & n1) | d1);
            if (m2 && ok2)
                p[2] = (uint8_t)((p[2] & n2) | d2);
        } else {
            p[0] |= d0;
            if (ok1)
                p[1] |= d1;
            if (d2 && ok2)
                p[2] |= d2;
        }
    }
    fb_mark_dirty(y, n);
}

uint8_t font_draw_cjk(uint8_t x, uint8_t y, uint16_t code)
{
    static uint8_t glyph[32];
    uint16_t idx = cjk_index(code);
    uint16_t per = cur->per_bank;
    uint8_t bank_save = _current_bank;
    uint8_t b = 0;

    /* idx/per_bank 與 idx%per_bank 本來是兩次 __divuint/__moduint(各數百
     * 週期);字模最多 7 個 bank,連減一次跑完商和餘數便宜得多。 */
    while (idx >= per) {
        idx -= per;
        b++;
    }
    SWITCH_ROM(cur->first_bank + b);
    memcpy(glyph, cur->chunks[b] + idx * (uint16_t)cur->glyph_bytes,
           cur->glyph_bytes);
    SWITCH_ROM(bank_save);

    /* 攻略公式的瘦上標:3px 點陣+1px 間隔。其餘全形字維持原寬。 */
    if (cur == &set12 && (code == 0xA2FC || code == 0xA2FD)) {
        blit_rows(x, y, glyph, cur->cell_h, 4, 1);
        return 4;
    }
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
