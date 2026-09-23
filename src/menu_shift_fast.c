/* HOME loops for moving a pixel-exact framebuffer rectangle. */
#include <stdint.h>
#include "fb.h"

extern uint8_t *menu_shift_dst;
extern const uint8_t *menu_shift_src;
extern uint8_t menu_shift_width;
extern uint8_t menu_shift_rows;
extern uint8_t menu_shift_gap;
extern uint8_t menu_shift_back;
extern uint8_t menu_shift_first_mask;
extern uint8_t menu_shift_last_mask;

void menu_shift_copy_up(void);
void menu_shift_copy_down(void);
void menu_shift_clear(void);

/* Keep the banked entry point small: bank 27 has no spare ROM space. */
void menu_shift_view_home(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                          uint8_t up)
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
    menu_shift_first_mask = 0xffu >> (x & 7);
    menu_shift_last_mask = 0xffu << (7 - ((x + w - 1) & 7));
    if (bx0 == bx1)
        menu_shift_first_mask &= menu_shift_last_mask;

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

/* A = pixels inside rectangle. Preserve BC (row and byte counts). */
void menu_shift_masked_copy_byte(void) __naked
{
__asm
    push bc
    ld   c, a
    ld   a, (hl+)
    and  c
    ld   b, a
    ld   a, c
    cpl
    ld   c, a
    ld   a, (de)
    and  c
    or   b
    ld   (de), a
    inc  de
    pop  bc
    ret
__endasm;
}

/* A = pixels to clear. Preserve BC. */
void menu_shift_masked_clear_byte(void) __naked
{
__asm
    push bc
    cpl
    ld   c, a
    ld   a, (de)
    and  c
    ld   (de), a
    inc  de
    pop  bc
    ret
__endasm;
}

void menu_shift_copy_up(void) __naked
{
__asm
    ld  a, (#_menu_shift_src)
    ld  l, a
    ld  a, (#_menu_shift_src+1)
    ld  h, a
    ld  a, (#_menu_shift_dst)
    ld  e, a
    ld  a, (#_menu_shift_dst+1)
    ld  d, a
    ld  a, (#_menu_shift_rows)
    ld  b, a
1$:
    ld  a, (#_menu_shift_width)
    ld  c, a
    ld  a, (#_menu_shift_first_mask)
    call _menu_shift_masked_copy_byte
    dec c
    jr  z, 5$
2$:
    dec c
    jr  z, 6$
    ld  a, (hl+)
    ld  (de), a
    inc de
    jr  2$
6$:
    ld  a, (#_menu_shift_last_mask)
    call _menu_shift_masked_copy_byte
5$:
    ld  a, (#_menu_shift_gap)
    ld  c, a
    ld  a, l
    add a, c
    ld  l, a
    jr  nc, 3$
    inc h
3$:
    ld  a, e
    add a, c
    ld  e, a
    jr  nc, 4$
    inc d
4$:
    dec b
    jr  nz, 1$
    ret
__endasm;
}

void menu_shift_copy_down(void) __naked
{
__asm
    ld  a, (#_menu_shift_src)
    ld  l, a
    ld  a, (#_menu_shift_src+1)
    ld  h, a
    ld  a, (#_menu_shift_dst)
    ld  e, a
    ld  a, (#_menu_shift_dst+1)
    ld  d, a
    ld  a, (#_menu_shift_rows)
    ld  b, a
1$:
    ld  a, (#_menu_shift_width)
    ld  c, a
    ld  a, (#_menu_shift_first_mask)
    call _menu_shift_masked_copy_byte
    dec c
    jr  z, 5$
2$:
    dec c
    jr  z, 6$
    ld  a, (hl+)
    ld  (de), a
    inc de
    jr  2$
6$:
    ld  a, (#_menu_shift_last_mask)
    call _menu_shift_masked_copy_byte
5$:
    ld  a, (#_menu_shift_back)
    ld  c, a
    ld  a, l
    sub a, c
    ld  l, a
    jr  nc, 3$
    dec h
3$:
    ld  a, e
    sub a, c
    ld  e, a
    jr  nc, 4$
    dec d
4$:
    dec b
    jr  nz, 1$
    ret
__endasm;
}

void menu_shift_clear(void) __naked
{
__asm
    ld  a, (#_menu_shift_dst)
    ld  e, a
    ld  a, (#_menu_shift_dst+1)
    ld  d, a
    ld  a, (#_menu_shift_rows)
    ld  b, a
1$:
    ld  a, (#_menu_shift_width)
    ld  c, a
    ld  a, (#_menu_shift_first_mask)
    call _menu_shift_masked_clear_byte
    dec c
    jr  z, 5$
    xor a, a
2$:
    dec c
    jr  z, 6$
    ld  (de), a
    inc de
    jr  2$
6$:
    ld  a, (#_menu_shift_last_mask)
    call _menu_shift_masked_clear_byte
5$:
    ld  a, (#_menu_shift_gap)
    ld  c, a
    ld  a, e
    add a, c
    ld  e, a
    jr  nc, 3$
    inc d
3$:
    dec b
    jr  nz, 1$
    ret
__endasm;
}
