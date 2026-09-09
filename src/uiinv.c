/* uiinv.c - ui_invert(HOME 遷出,2026-07-29)
 *
 * 原本掛在 ui.c 且標 NONBANKED,理由只是 create(28)/input_py(23)/menu(26)
 * 三個 bank 都要用。它的參數全是純量、不吃也不回傳 ROM 指標,BANKED
 * 呼叫本來安全——但它的呼叫點在 menu.c(bank 26),而 bank 26 只剩
 * 76 字節:改成 BANKED 後每個呼叫點多幾字節,_CODE_26 漲到 0x4007,
 * ASlink 不報錯,ROM 靜靜地在進世界那一幀永久卡死(ui_invert 本身
 * 一次都沒被呼叫)。所以這裡仍是 NONBANKED,只是從 ui.c 拆出來單獨
 * 成檔,等 bank 26 騰出 ~340 字節再改 BANKED。詳見 check_home.py。
 */
#include <gb/gb.h>
#include <stdint.h>
#include "ui.h"
#include "fb.h"

void ui_invert(uint8_t x, uint8_t y, uint8_t w, uint8_t h) NONBANKED
{
    uint8_t first, last, first_mask, last_mask, r, b;
    uint8_t end;

    if (!w || !h)
        return;
    end = x + w - 1;
    first = x >> 3;
    last = end >> 3;
    first_mask = (uint8_t)(0xFFu >> (x & 7));
    last_mask = (uint8_t)(0xFFu << (7 - (end & 7)));
    for (r = 0; r < h; r++) {
        uint8_t *row = fb + (uint16_t)(y + r) * FB_STRIDE;

        if (first == last) {
            row[first] ^= first_mask & last_mask;
        } else {
            row[first] ^= first_mask;
            for (b = first + 1; b < last; b++)
                row[b] ^= 0xFF;
            row[last] ^= last_mask;
        }
    }
    fb_mark_dirty(y, h);
}
