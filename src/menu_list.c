/* Cold two-column list-frame drawing, moved out of the hot menu bank. */
#pragma bank 27
#include <gb/gb.h>
#include "menu.h"
#include "fb.h"
#include "ui.h"

uint8_t menu_list_frame(uint8_t x, uint8_t y, const cmenu_t *m) BANKED
{
    uint8_t x0 = x - 2, y0 = y - 2;
    uint8_t x1 = x0 + 18 * 6;
    uint8_t y1 = y0 + 5 * 12 + 3;
    uint8_t xd = x0 + 4 * 6 + 3;

    fb_fill_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, 0);
    ui_hline(x0, x1, y0);
    ui_hline(x0, x1, y1);
    ui_vline(x0, y0, y1);
    ui_vline(x1, y0, y1);
    ui_vline(xd, y0, y1);

    list_x0 = xd + 1;
    list_y0 = y;
    list_x1 = x1 - 1;
    list_y1 = y1 - 1;

    return pop_menu(x, y, m);
}

void menu_list_clean(void) BANKED
{
    fb_fill_rect(list_x0, list_y0, list_x1 - list_x0 + 1,
                 list_y1 - list_y0 + 1, 0);
}
