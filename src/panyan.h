/* panyan.h - 遊戲山莊小遊戲(panyan.s,bank 27) */
#ifndef PANYAN_H
#define PANYAN_H
#include <gb/gb.h>

/* GBC 原創:跳舞毯最高分達此分數 → 凌波微步(發放邏輯在 pyh_quest.c)。
 * 每答對 +3 分,故 573 分需連續答對 191 次(答錯即結束)。 */
#define LINGBO_SCORE 573

uint8_t panyan_dance(void) BANKED;  /* 跳舞毯(top_dance);1=最高分達門檻 */
void panyan_ball(void) BANKED;      /* 投篮球(top_ball) */

#endif
