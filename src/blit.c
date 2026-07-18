/* 代碼在 bank 25(呼叫端 HOME/25;fastops 留 HOME) */
#pragma bank 25
#include <gb/gb.h>
#include <string.h>
#include "blit.h"
#include "fb.h"

__at(0xA800) uint8_t scroll_buf[20 * SCROLL_H];

/* 一行內把 src 的 w_bytes 字節按位移合成到 scroll_buf 行,含水平剪裁。
 * dst_x 可負。模式:print=範圍內覆寫,or=疊加,and=遮罩(範圍外不動)。 */
static void blit_row(int16_t dst_x, uint8_t *row, const uint8_t *src,
                     uint8_t w_bytes, uint8_t lcmd)
{
    int16_t bx = dst_x >> 3;            /* 目標起始字節(向下取整) */
    uint8_t shift = (uint8_t)(dst_x & 7);
    uint8_t i, v, out, valid;
    uint8_t carry = 0;
    int16_t o;

    for (i = 0; i <= w_bytes; i++) {
        v = (i < w_bytes) ? src[i] : 0;
        out = (uint8_t)((v >> shift) | carry);
        carry = shift ? (uint8_t)(v << (8 - shift)) : 0;

        o = bx + i;
        if (o < 0 || o >= 20)
            continue;

        /* 此輸出字節中屬於圖形範圍的位 */
        valid = 0xFF;
        if (i == 0)
            valid = (uint8_t)(0xFF >> shift);
        if (i == w_bytes)
            valid &= shift ? (uint8_t)(0xFF << (8 - shift)) : 0x00;

        if (lcmd == LCMD_OR)
            row[o] |= out;                          /* out 範圍外必為 0 */
        else if (lcmd == LCMD_AND)
            row[o] &= (uint8_t)(out | ~valid);
        else                                        /* LCMD_PRINT */
            row[o] = (uint8_t)((row[o] & ~valid) | out);
    }
}

void lee_block(int16_t x, int16_t y, const uint8_t *img,
               uint8_t w_bytes, uint8_t h, uint8_t lcmd) BANKED
{
    uint8_t r, r0 = 0, i0 = 0, i1, n, i;
    int16_t bx;
    uint8_t *d;
    const uint8_t *s;

    /* 垂直剪裁 */
    if (y < 0) {
        r0 = (uint8_t)-y;
        if (r0 >= h)
            return;
    }
    if (y + h > SCROLL_H) {
        if (y >= SCROLL_H)
            return;
        h = (uint8_t)(SCROLL_H - y);
    }

    if ((x & 7) == 0 && x >= 0 && w_bytes == 4 && x + 32 <= 160
        && y >= 0 && (uint8_t)(y + h) <= SCROLL_H) {
        /* 最快路徑:整塊在屏內且對齊 → 匯編例程 */
        fo_dst = &scroll_buf[(uint16_t)y * 20 + (x >> 3)];
        fo_src = img;
        fo_h = h;
        if (lcmd == LCMD_OR)
            fo_or();
        else if (lcmd == LCMD_AND)
            fo_and();
        else
            fo_copy();
        return;
    }

    if ((x & 7) == 0) {
        /* 快路徑:字節對齊(速度為 8 的倍數時恆成立),剪裁一次算好 */
        bx = x >> 3;
        i1 = w_bytes;
        if (bx < 0) {
            if (bx + w_bytes <= 0)
                return;
            i0 = (uint8_t)-bx;
        }
        if (bx + w_bytes > 20) {
            if (bx >= 20)
                return;
            i1 = (uint8_t)(20 - bx);
        }
        n = i1 - i0;
        d = &scroll_buf[(uint16_t)(y + r0) * 20 + (bx + i0)];
        s = img + (uint16_t)r0 * w_bytes + i0;
        switch (lcmd) {
        case LCMD_OR:
            for (r = r0; r < h; r++, d += 20 - n, s += w_bytes - n)
                for (i = 0; i < n; i++)
                    *d++ |= *s++;
            break;
        case LCMD_AND:
            for (r = r0; r < h; r++, d += 20 - n, s += w_bytes - n)
                for (i = 0; i < n; i++)
                    *d++ &= *s++;
            break;
        default:
            for (r = r0; r < h; r++, d += 20 - n, s += w_bytes - n)
                for (i = 0; i < n; i++)
                    *d++ = *s++;
            break;
        }
        return;
    }

    /* 一般路徑:任意位移(低速/特殊繪製用) */
    for (r = r0; r < h; r++)
        blit_row(x, &scroll_buf[(uint16_t)(y + r) * 20],
                 img + (uint16_t)r * w_bytes, w_bytes, lcmd);
}

void scroll_to_lcd(void) BANKED
{
    memcpy(fb, scroll_buf, 20 * SCROLL_H);
    fb_mark_dirty(0, SCROLL_H);
}
