/* fight_internal.h - fight.c(bank 27)與 perform.c(bank 26,絕招)之間的
 * 跨 bank 介面。原版 perform.s 與 fight.s 同 bank 直呼;GBC 因 bank 27
 * (fight)已近滿,絕招移 bank 26,故以 BANKED 薄封裝跨 bank 呼叫:
 *
 *   perform.c → fight.c:攻防(pf_attack_*)、招式威力(pf_skill_power)、
 *                       訊息(pf_show_fight_msg)——經 HOME 跳板。
 *                       (fight.c 內部熱路徑仍直呼自身 static 例程,無跳板)
 *   fight.c → perform.c:施展(perform_dispatch)、ptemp(init_ptemp/call_out)。
 *
 * combat_over/perform_flag 為 WRAM 全域,跨 bank 直接存取。 */
#ifndef FIGHT_INTERNAL_H
#define FIGHT_INTERNAL_H
#include <stdint.h>
#include <gb/gb.h>

/* ---- perform.c 呼叫的 fight.c 原語(BANKED 跳板)---- */
void pf_attack_npc(void) BANKED;    /* 主角攻擊 NPC(內含 who_win) */
void pf_attack_man(void) BANKED;    /* NPC 攻擊主角(內含 who_win) */
uint32_t pf_skill_power(uint8_t is_man, uint8_t defense, uint8_t micro) BANKED;
void pf_show_fight_msg(const uint8_t *msg) BANKED;      /* msg 指 WRAM */

extern uint8_t combat_over;         /* who_win 命中 → 1(對局結束) */
extern uint8_t perform_flag;        /* 絕招連擊固定動作序號(random_kf_action) */

/* ---- fight.c 呼叫的 perform.c 入口(BANKED)---- */
uint8_t perform_dispatch(void) BANKED;  /* 依 perform_id 施展;1=失敗 */
void init_ptemp(void) BANKED;           /* 對局開始清 ptemp */
void call_out(void) BANKED;             /* 每回合遞減 buff 到期還原 */
void fenshen_fight_end(void) BANKED;     /* 提前結束:EXT/ADV 還原,BSC 保留 */
void fenshen_disable_bsc_feature(void) BANKED; /* 離開 BSC 即還原殘留 */

/* ---- nf.c(bank 30,聯機)呼叫的 fight.c 原語(BANKED 跳板)---- */
uint8_t pf_who_win(void) BANKED;
void pf_refresh_fight(void) BANKED;
void pf_init_fight(void) BANKED;        /* escape_factor/busy/ptemp 復位 */
void pf_fight_menu(void) BANKED;        /* 彈戰鬥主選單(座標=單機同位) */

#endif
