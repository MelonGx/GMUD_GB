/* Banked setup for the HOME assembly rectangle mover. */
#pragma bank 27
#include <gb/gb.h>
#include <stdint.h>
#include "fb.h"

uint8_t *menu_shift_dst;
const uint8_t *menu_shift_src;
uint8_t menu_shift_width;
uint8_t menu_shift_rows;
uint8_t menu_shift_gap;
uint8_t menu_shift_back;

void menu_shift_copy_up(void);
void menu_shift_copy_down(void);
void menu_shift_clear(void);

void menu_shift_view(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                     uint8_t up) BANKED
{
    uint8_t bx0, bx1;

    if (!w || h <= 12)
        return;
    bx0 = x >> 3;
    bx1 = (x + w - 1) >> 3;
    menu_shift_width = bx1 - bx0 + 1;
    menu_shift_rows = h - 12;
    menu_shift_gap = FB_STRIDE - menu_shift_width;
    menu_shift_back = FB_STRIDE + menu_shift_width;

    if (up) {
        menu_shift_dst = fb + (uint16_t)y * FB_STRIDE + bx0;
        menu_shift_src = menu_shift_dst + 12 * FB_STRIDE;
        menu_shift_copy_up();
        menu_shift_dst = fb + (uint16_t)(y + h - 12) * FB_STRIDE + bx0;
    } else {
        menu_shift_dst = fb + (uint16_t)(y + h - 1) * FB_STRIDE + bx0;
        menu_shift_src = menu_shift_dst - 12 * FB_STRIDE;
        menu_shift_copy_down();
        menu_shift_dst = fb + (uint16_t)y * FB_STRIDE + bx0;
    }
    menu_shift_rows = 12;
    menu_shift_clear();
    fb_mark_dirty(y, h);
}
