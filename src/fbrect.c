/* fbrect.c - fb_fill_rect(bank 26;HOME 溢出遷出,呼叫者全為 banked 代碼)
 *
 * 像素精確:首尾字節只動 [x,x+w) 覆蓋的位。字節粒度會把同字節內的
 * 鄰接像素一併抹掉(建角姓名清單游標擦除啃掉名字左緣,2026-07-10)。 */
#pragma bank 27
#include <gb/gb.h>
#include <stdint.h>
#include "fb.h"

void fb_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                  uint8_t on) BANKED
{
    uint8_t bx0 = x >> 3;
    uint8_t bx1 = (uint8_t)(x + w - 1) >> 3;
    uint8_t m0 = (uint8_t)(0xFF >> (x & 7));
    uint8_t m1 = (uint8_t)(0xFF << (7 - ((uint8_t)(x + w - 1) & 7)));
    uint8_t v = on ? 0xFF : 0x00;
    uint8_t *p;
    uint8_t r, c;

    if (bx0 == bx1)
        m0 &= m1;

    for (r = 0; r < h; r++) {
        p = &fb[(uint16_t)(y + r) * FB_STRIDE + bx0];
        if (on)
            *p |= m0;
        else
            *p &= (uint8_t)~m0;
        if (bx0 == bx1)
            continue;
        p++;
        for (c = (uint8_t)(bx0 + 1); c < bx1; c++)
            *p++ = v;
        if (on)
            *p |= m1;
        else
            *p &= (uint8_t)~m1;
    }
    fb_mark_dirty(y, h);
}
