/* blit.h - scroll_buf 離屏繪製(等效原版 lee_block/draw.s + lcd.s 複製)
 *
 * scroll_buf = 20 字節 × 80 行離屏緩衝(原版同名),
 * scroll_to_lcd 把它複製進 fb 的遊戲區(0..79 行)。
 */
#ifndef BLIT_H
#define BLIT_H
#include <stdint.h>
#include <gb/gb.h>      /* BANKED */

#define SCROLL_H 144
extern __at(0xA800) uint8_t scroll_buf[20 * SCROLL_H];

/* lcmd 模式,取值同原版 */
#define LCMD_PRINT 0
#define LCMD_OR    3
#define LCMD_AND   4

/* 把 w_bytes×h 的 1bpp 圖塊畫到 scroll_buf 的像素座標 (x,y),可負值,自動剪裁 */
void lee_block(int16_t x, int16_t y, const uint8_t *img,
               uint8_t w_bytes, uint8_t h, uint8_t lcmd) BANKED;

/* scroll_buf → fb 全畫面；保留最左像素，不再製造 1px 白條。 */
void scroll_to_lcd(void) BANKED;

/* fastops.c 匯編例程(對齊整塊專用) */
extern uint8_t *fo_dst;
extern const uint8_t *fo_src;
extern uint8_t fo_h;
extern uint16_t fo_len;
void fo_copy(void);
void fo_or(void);
void fo_and(void);
void fo_copy16(void);       /* 大塊直拷:fo_h = 16B 單位數 */
void fo_copy_pop(void);     /* 棧彈跳拷貝:fo_h = 320B 塊數(sync 用) */
void fo_copy_fwd(void);     /* 起點升序拷貝 fo_len 字節(重疊左移安全) */
void fo_copy_bwd(void);     /* 末字節降序拷貝(重疊右移安全) */
void fo_save(void);         /* 行距20×4B×fo_h → 連續(底圖快照) */
#endif
