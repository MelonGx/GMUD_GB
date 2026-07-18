/* font.h - 文字渲染,等效原版 font.s write_one_char 的雙模式
 *
 * 對應原版 char_height(12 or 16):
 *   12px 模式:12×13 全形 / 6×13 半形(行距 13,原機 13×6+2=80)
 *   16px 模式:16×16 全形 / 8×16 半形
 * 字串為 GB2312/ASCII 混排字節流(與原版數據一致)。
 */
#ifndef FONT_H
#define FONT_H
#include <stdint.h>

/* 設定字高,對應原版 char_height:12 或 16 */
void font_set_height(uint8_t h);

/* 畫一個 GB2312 全形字,回傳前進寬度(12/16) */
uint8_t font_draw_cjk(uint8_t x, uint8_t y, uint16_t code);

/* 畫一個半形 ASCII,回傳前進寬度(6/8) */
uint8_t font_draw_ascii(uint8_t x, uint8_t y, uint8_t c);

/* 畫混排字串(GB2312 高字節 >= 0xA1),回傳結束 x */
uint8_t font_draw_text(uint8_t x, uint8_t y, const uint8_t *s);

/* 覆寫式畫字串:先清出字格再畫(等效原版系統字模覆寫行為) */
uint8_t font_draw_text_replace(uint8_t x, uint8_t y, const uint8_t *s);

#endif
