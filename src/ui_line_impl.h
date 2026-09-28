/* Internal source-only implementations for the identical UI line primitives.
 * Keep each existing function, bank annotation, and call site in place. */
#ifndef UI_LINE_IMPL_H
#define UI_LINE_IMPL_H

#define UI_HLINE_BODY \
    uint8_t x; \
    for (x = x0; x <= x1; x++) \
        fb[(uint16_t)y * FB_STRIDE + (x >> 3)] |= 0x80 >> (x & 7); \
    fb_mark_dirty(y, 1);

#define UI_VLINE_BODY \
    uint8_t y; \
    for (y = y0; y <= y1; y++) \
        fb[(uint16_t)y * FB_STRIDE + (x >> 3)] |= 0x80 >> (x & 7); \
    fb_mark_dirty(y0, y1 - y0 + 1);

/* Keep each caller's own line functions, arguments, and call order. */
#define UI_BOX_EDGES(HLINE, VLINE, X0, Y0, X1, Y1) \
    HLINE(X0, X1, Y0); \
    HLINE(X0, X1, Y1); \
    VLINE(X0, Y0, Y1); \
    VLINE(X1, Y0, Y1);

#endif
