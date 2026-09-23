/* Banked entry point for the HOME framebuffer rectangle mover. */
#pragma bank 27
#include <gb/gb.h>
#include <stdint.h>

uint8_t *menu_shift_dst;
const uint8_t *menu_shift_src;
uint8_t menu_shift_width;
uint8_t menu_shift_rows;
uint8_t menu_shift_gap;
uint8_t menu_shift_back;
uint8_t menu_shift_first_mask;
uint8_t menu_shift_last_mask;

void menu_shift_view_home(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                          uint8_t up);

void menu_shift_view(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                     uint8_t up) BANKED
{
    menu_shift_view_home(x, y, w, h, up);
}
