#ifndef LE16_WRITE_BODY_H
#define LE16_WRITE_BODY_H

/* Expand inside the existing putw(p, v) / wr16(p, v) function bodies. */
#define LE16_PUTW_BODY(tmp) \
    uint16_t tmp = (uint16_t)v; \
    p[0] = (uint8_t)tmp; \
    p[1] = (uint8_t)(tmp >> 8)

#define LE16_WR16_BODY \
    p[0] = (uint8_t)v; \
    p[1] = (uint8_t)(v >> 8)

#endif
