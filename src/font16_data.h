/* 自動產生:build_font.py,勿手改 */
#ifndef FONT16_DATA_H
#define FONT16_DATA_H
#include <stdint.h>

#define FONT16_COUNT 270
#define FONT16_CELL_H 16
#define FONT16_GLYPH_BYTES 32
#define FONT16_PER_BANK 500
#define FONT16_BANKS 1
#define FONT16_FIRST_BANK 5
#define FONT16_MISC_BANK 23

extern const uint16_t font16_codes[FONT16_COUNT];
extern const uint8_t font16_ascii[95][FONT16_CELL_H];
extern const uint8_t* const font16_chunks[FONT16_BANKS];

#endif
