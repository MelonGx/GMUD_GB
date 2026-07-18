/* baishi.h - 拜師條件判定(移植原版 serve.s baishi;
 * 檔名取 baishi 因本工程 serve.c 已對應 lee1.s) */
#ifndef BAISHI_H
#define BAISHI_H
#include <stdint.h>
#include <gb/gb.h>      /* BANKED */

/* 等效 serve.s baishi:input located_id;
 * 回傳 1=收徒(原版 clc)0=拒絕;師父台詞已拷入 gfx_scratch+192
 * (等效原版 move_to_img → img_buf,跨 bank 出境) */
uint8_t baishi(uint8_t id) BANKED;

#endif
