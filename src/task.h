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
extern __at(0xA320) uint8_t quest_temp[72];
extern __at(0xA368) uint8_t task_buf[8];
extern __at(0xA388) uint8_t home_buf;   /* 義工:0 無 1 掃地 2 挑水 3 劈柴 */
extern __at(0xA370) uint8_t task_gbuf[24];  /* game_buf 共享區(跨模組別名) */

/* 官方秘技(用戶定版 2026-07-18,取代原版 super_man 密碼→cheat_mode):
 * 主角名=yobdc → 開局經驗 999999/潛能拉滿、婆婆義工無經驗上限、
 * 菜花寶典免邪派條件(不必殺人降道德=不用 KO 小頑童;老花鏡仍要)。
 * 其他名字無效。非 BANKED,僅 bank 28 內(create/task/pyh_quest)直呼。 */
uint8_t yobdc_mode(void);

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
