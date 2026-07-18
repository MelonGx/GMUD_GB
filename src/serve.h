/* serve.h - 內力四式與練功邏輯(移植原版 lee1.s + pyh.s pyh_practice) */
#ifndef SERVE_H
#define SERVE_H
#include <stdint.h>
#include <gb/gb.h>      /* BANKED */

uint8_t dazuo(void) BANKED; /* 打坐 tick:1=停(原版 sec 路徑) */
uint8_t recover(void) BANKED; /* 吸氣(內力換精血) */
uint8_t heal(void) BANKED;  /* 療傷(重傷恢復) */
void    jiali(void) BANKED; /* 設定加力值 */
uint8_t pyh_practice(void) BANKED; /* 練功 tick:1=停 */

extern uint8_t learn_master_lvl;   /* 師父等級(=原版 game_buf) */
uint8_t pyh_learn(void) BANKED;    /* 學習 tick:1=停 */

#endif
