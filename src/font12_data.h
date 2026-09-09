/* 自動產生:build_font.py,勿手改 */
#ifndef FONT12_DATA_H
#define FONT12_DATA_H
#include <stdint.h>

#define FONT12_COUNT 3877
#define FONT12_CELL_H 13
#define FONT12_GLYPH_BYTES 26
#define FONT12_PER_BANK 615
#define FONT12_BANKS 7
#define FONT12_FIRST_BANK 16
#define FONT12_MISC_BANK 23

extern const uint16_t font12_codes[FONT12_COUNT];
extern const uint8_t font12_ascii[95][FONT12_CELL_H];
extern const uint8_t* const font12_chunks[FONT12_BANKS];

#endif
