/* fight.h - 回合制戰鬥(移植原版 fight.s + string2.s get_result/py_action
 *           + digit.s show_line/show_digit + skilly.s random_kf_action)
 *
 * man_state/npc_state 同構前 41 字節(h/id.h fight_off);combat 引擎在
 * bank 27,選單處理器在 HOME(fight_home.c),對局入口 fight() BANKED。
 */
#ifndef FIGHT_H
#define FIGHT_H
#include <stdint.h>
#include <gb/gb.h>          /* BANKED */
#include "menu.h"

/* ---- fight_off 偏移(h/id.h,man_state hero.man_name 起 / npc &npc) ---- */
#define NAME_OFF     0
#define BUSY_OFF     9
#define PAI_OFF     10
#define GENDER_OFF  11
#define AGE_OFF     12
#define DAODE_OFF   13
#define ATTACK_OFF  14
#define DEFENSE_OFF 15
#define DAMAGE_OFF  16
#define ARMOR_OFF   17
#define EXP_OFF     18      /* 4B */
#define FORCE_OFF   22      /* 2B */
#define STR_OFF     24
#define DEX_OFF     25
#define INT_OFF     26
#define CON_OFF     27
#define PER_OFF     28
#define KAR_OFF     29
#define HP_OFF      30      /* 2B */
#define MAXHP_OFF   32      /* 2B */
#define FP_OFF      34      /* 2B */
#define MAXFP_OFF   36      /* 2B */
#define EFFHP_OFF   38      /* 2B */
#define WEAPON_OFF  40

/* ---- 對局入口 ---- */
void fight(void) BANKED;            /* 等效 fight:主迴圈 */
uint8_t final_fight(uint8_t ex) BANKED; /* BOSS 開場白+對局,ex=endx 0-2 */

/* ---- fight_menu 處理器實作(bank 27),由 HOME thunk 呼叫 ---- */
uint8_t fight_normal_attack(void) BANKED;   /* 攻擊 */
uint8_t fight_perform_action(void) BANKED;  /* 絕招(Task#3 perform 接入) */
uint8_t fight_xiqi(void) BANKED;            /* 吸氣(Task#3 recover 接入) */
uint8_t fight_goods(void) BANKED;           /* 物品(item 模組接入) */
uint8_t fight_escape(void) BANKED;          /* 逃跑 */

/* ---- fight_menu(fight_home.c,bank 25)---- */
extern const cmenu_t fight_menu;
/* 絕招清單選單(fight_home.c,與 pop_menu 同 bank);回 perform_id 或 0xFF */
uint8_t show_perform(void) BANKED;

extern uint8_t fight_exit_code;     /* who_win 結果:0 敗 1 未殺 2 逃/放過 3 勝殺 */

#endif
