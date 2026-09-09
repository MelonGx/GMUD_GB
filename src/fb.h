/* fb.h - 1bpp 偽幀緩衝,對應原版 NC2000 的 LCD 顯存模型
 *
 * 全屏 160x144(20 字節 x 144 行,bit7 為最左像素)。
 * 原版遊戲區 160x80 建議畫在 y=FB_GAME_Y0 起(垂直置中偏上),
 * 下方留給移植版狀態列/訊息區。
 */
#ifndef FB_H
#define FB_H
#include <stdint.h>
#include <gb/gb.h>      /* BANKED */

#define FB_STRIDE   20
#define FB_ROWS     144
#define FB_GAME_Y0  0    /* 原版 160x80 遊戲區置頂,y=80 起為移植版狀態列 */

extern uint8_t fb[FB_ROWS * FB_STRIDE];

/* 初始化 tilemap/attr(360 個獨立 tile 鋪滿 20x18),清屏 */
void fb_init(void) BANKED;

/* 全部清 0(白)並標記全屏髒 */
void fb_clear(void) NONBANKED;

/* 把 (x,y) 起 w*h 像素矩形填 0/1(像素精確;bank 26,fbrect.c) */
void fb_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                  uint8_t on) BANKED;

/* 標記像素行區間 [y, y+h) 為髒(字模繪製後呼叫) */
void fb_mark_dirty(uint8_t y, uint8_t h) NONBANKED;

/* Present an already composed 160x144 WRAM buffer without first copying it
 * into fb. A later regular fb_flush() synchronizes it back for UI drawing. */
void fb_present(const uint8_t *src) NONBANKED;
void fb_present_overlay(const uint8_t *src, const uint8_t *overlay,
                        uint8_t width_px) NONBANKED;

/* 把髒的 tile 行轉 2bpp 寫入 VRAM(STAT 安全,LCD 開著也能用) */
void fb_flush(void) BANKED;

/* 行走子步專用串流刷新:批次 B 走 HBlank HDMA 後台滴灌、翻面掛起,
 * 阻塞僅 1 個 VBlank;子步間不得有其他 VRAM 寫入 */
void fb_flush_stream(void) BANKED;

/* Finish an outstanding walk-stream HDMA and its VBlank palette commit.
 * Call before any HALT/vsync path; CGB HALT is unsafe while HDMA is active. */
void fb_stream_drain(void) BANKED;

#endif
