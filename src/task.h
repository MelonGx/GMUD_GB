/* task.h - 平一指任務系統(移植原版 task.s + npc_quest.s + qlist.s)
 *
 * 任務工作區在 GBC 版由 save.c 按 slot 快照，通緝犯的動態位置
 * 也存入 task_gbuf 未用位元組，因此可跨斷電恢復。
 * BANKED 入口刻意收攏(每個 BANKED 函數在 HOME 佔一份跳板,HOME 緊)。
 */
#ifndef TASK_H
#define TASK_H
#include <stdint.h>
#include <gb/gb.h>      /* BANKED */
#include "sram_layout.h"

/* 任務類型(原版 h/id.h task 段) */
#define QUEST_NPC   0
#define QUEST_GOODS 1
#define QUEST_KILL  2
#define QUEST_GHOST 3
#define QUEST_HOME  4
#define QUEST_BRICK 5

#define HAS_REWARD  0xFE
#define QUEST_OVER  0xFF

/* 任務狀態在 SRAM 0xA320 起(main 常開;跨斷電保留=原機 RAM 語義,
 * 新遊戲由 game_boot 清零)。task_buf:[0]=quest_type [1]=quest_id
 * [2..3]=bonus exp [4..5]=bonus pot [6..7]=bonus money;
 * quest_temp(72B=12B×6 槽):{index,exp[4],id,time[4],reward[2]} */
extern __at(SRAM_QUEST_TEMP) uint8_t quest_temp[SRAM_QUEST_TEMP_LEN];
extern __at(SRAM_TASK_BUF) uint8_t task_buf[SRAM_TASK_BUF_LEN];
extern __at(SRAM_HOME_BUF) uint8_t home_buf;   /* 義工:0 無 1 掃地 2 挑水 3 劈柴 */
extern __at(SRAM_TASK_GBUF) uint8_t task_gbuf[SRAM_TASK_GBUF_LEN];  /* game_buf 共享區(跨模組別名) */

/* 官方秘技(原版 super_man→cheat_mode;GBC 版主角名=yobdc):
 * 功能選單多「作弊」項(查看/修改數值+技能等級)、婆婆義工無經驗上限、
 * 菜花宝典免邪派條件。非 BANKED,bank 25/28 直呼。 */
uint8_t yobdc_mode(void) BANKED;

/* npc_talk 任務鏈(pyh_task→尋人達成→npc_quest);0=沒話說(接 dunno) */
uint8_t talk_task(void) BANKED;
/* fight 勝利後任務判定(QUEST_KILL 留 Opus;QUEST_GHOST 發賞) */
void fight_win_task(void) BANKED;
/* KILLER_NPC → 由玩家屬性生成 npc;1=已載入 */
uint8_t init_ghost(void) BANKED;
/* 續玩時從當前 slot 的 task_gbuf 復原通緝犯；新遊戲則清掉舊 WRAM */
void ghost_task_restore(void) BANKED;
/* string.s $t/$q/$g/$k/$p 轉義主體(format_string 轉呼;out→OutBuf 內) */
uint8_t *task_escape(uint8_t c, uint8_t *out) BANKED;
/* 物件動作分派(talk.s item_action;idx=located_id-200);1=刪檔退出 */
uint8_t item_action_b(uint8_t idx) BANKED;

#endif
