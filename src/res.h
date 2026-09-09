/* res.h - 分 bank 資源讀取(等效原版 s_read.s 的 speed_read) */
#ifndef RES_H
#define RES_H
#include <stdint.h>

typedef struct {
    uint8_t first_bank;
    uint8_t nbanks;
    const uint8_t *const *chunks;   /* 每 bank 16384B,ROM0 指標表 */
} res_t;

/* 從資源 off 起讀 n 字節到 dst(自動跨 bank)。
 * off 為 16 位:所有經此 API 的資源均 <64KB(tiles 128KB 由
 * map.c tile_fetch 直接分 bank 定址,不經此處);SDCC 32 位運算
 * 極慢,地圖內圈每格都在付這筆帳,故降級 */
void res_read(const res_t *r, uint16_t off, uint8_t *dst, uint16_t n);

/* 讀 16bit 小端(等效 speed_read_2) */
uint16_t res_read16(const res_t *r, uint16_t off);

#endif
