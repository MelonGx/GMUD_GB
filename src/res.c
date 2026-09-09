#include <gb/gb.h>
#include <string.h>
#include "res.h"

void res_read(const res_t *r, uint16_t off, uint8_t *dst, uint16_t n)
{
    uint8_t bank_save = _current_bank;
    uint8_t chunk = (uint8_t)(off >> 14);
    uint16_t rem = off & 0x3FFF;
    uint16_t take;

    while (n) {
        take = 16384 - rem;
        if (take > n)
            take = n;
        SWITCH_ROM(r->first_bank + chunk);
        memcpy(dst, r->chunks[chunk] + rem, take);
        dst += take;
        n -= take;
        rem = 0;
        chunk++;
    }
    SWITCH_ROM(bank_save);
}

uint16_t res_read16(const res_t *r, uint16_t off)
{
    uint8_t b[2];
    res_read(r, off, b, 2);
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}
