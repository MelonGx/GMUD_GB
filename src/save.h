/* save.h - SRAM 電池存檔(對應原版 save.s + gmud.h 的 save_data 佈局)
 *
 * 原版存成文曲星檔案 /gmud.sav(XOR 加密 + 6 字節 ECC 校驗);
 * GBC 版直接映射到 MBC5 電池 SRAM,保留 'HERO' 魔數與版本檢查,
 * 校驗簡化為 16bit 和 + XOR(SRAM 無檔案系統風險,不需要加密)。
 */
#ifndef SAVE_H
#define SAVE_H
#include <stdint.h>
#include <gb/gb.h>      /* BANKED */

/* ---- 常量對應 gmud.h(rom access 區,不可改) ---- */
#define MAX_GOODS   20
#define MAX_EQUIP   11
#define MAX_KF      20
#define MAX_USEKF   5
#define MAX_NAME    8
#define MAX_PASS    6
#define RESERVE_SIZE 18
#define GAME_VERSION 3      /* 原版 VERSION equ 3 */

/* 玩家存檔本體,字段順序 = 原版 save_data 起的內存佈局 */
typedef struct {
    uint8_t  game_ver;
    uint8_t  game_pid[4];           /* 'HERO' */
    uint32_t mud_age;
    uint8_t  attr_str, attr_dex, attr_int;
    uint8_t  attr_con, attr_per, attr_kar;

    uint8_t  man_picid;
    uint8_t  man_master;
    uint16_t man_respect;

    /* ---- man_state ---- */
    uint8_t  man_name[MAX_NAME + 1];
    uint8_t  man_busy, man_pai, man_gender, man_age, man_daode;
    uint8_t  man_attack, man_defense, man_damage, man_armor;
    uint32_t man_exp;
    uint16_t man_force;
    uint8_t  man_str, man_dex, man_int, man_con, man_per, man_kar;
    uint16_t man_hp, man_maxhp, man_fp, man_maxfp, man_effhp;
    uint8_t  man_weapon;
    /* ---- man_state 到此(MAN_SIZE) ---- */

    uint16_t man_food, man_maxfood, man_water, man_maxwater;
    uint16_t man_pot;
    uint32_t man_money;

    uint8_t  man_usekf[MAX_USEKF];  /* bit7: enable */
    uint8_t  man_kfnum;
    uint8_t  man_kf[4 * MAX_KF];    /* id 等級 當前潛能值 */

    uint8_t  man_pwd[MAX_PASS + 1];
    uint8_t  man_equip[MAX_EQUIP];  /* bit7: enable */
    uint8_t  man_goods[2 * MAX_GOODS]; /* id 數量 */
    uint16_t top_dance, top_ball;
    uint8_t  npc_stat_buf[32];

    /* ---- zero_area(死亡時清零區) ---- */
    uint8_t  shiban, npc_kill, guan_kill, si_kill;
    uint8_t  game_sec, game_min;
    uint16_t game_hour;
    uint8_t  reserve_buf[RESERVE_SIZE];
} hero_save_t;   /* = 原版 SAVE_SIZE,見 save_assert.c 靜態檢查 */

#define SAVE_SIZE 280

extern hero_save_t hero;    /* WRAM 工作副本(等效原版 save_data 變量區) */

/* GBC 新增設定(用戶要求 2026-07-08),佔用 reserve 區不改存檔佈局:
 *   0=EXT:打坐/練功按原版表值 400ms/tick,學習按表值 250 tick/s
 *   1=ADV:打坐/練功保留約 x8,學習暫同 EXT;服務/婆婆顯示 x4、
 *         戰鬥訊息 x2,獎勵不加倍
 *   2=BSC:速度同 ADV,另任務經驗/潛能 x8(婆婆義工除外)
 * 原版表值是核准的 x1 規格,不代表已由文曲星實機量得牆鐘時間。
 * A/B/START 取消及菜單/游標即時反應是刻意保留的 GBC 優化。 */
#define speed_mode (hero.reserve_buf[0])
/* Walking only: in ADV/BSC, hold B for x32 movement versus EXT.
 * Menu/dialog B handling is unchanged. */
#define disp_shift()   ((speed_mode) ? 2 : 0)       /* 婆婆任務顯示/服務訊息 */

#define fight_shift()  ((speed_mode) ? 1 : 0)       /* 戰鬥訊息等待 */
#define reward_shift() ((speed_mode == 2) ? 3 : 0)  /* BSC 任務獎勵(婆婆除外) */

uint8_t save_exists(void) BANKED;   /* SRAM 有合法存檔?(當前 slot) */
uint8_t save_load(void) BANKED;     /* SRAM → hero,1=成功(當前 slot) */
void    save_store(void) BANKED;    /* hero → SRAM(當前 slot) */
void    save_wipe(void) BANKED;     /* 等效原版 delete_file(當前 slot) */

/* ---- 雙存檔 slot(0=0xA000 舊相容 / 1=0xA500;遊戲內存檔恆寫當前) */
extern uint8_t active_slot;
uint8_t save_probe(uint8_t slot, uint8_t *name9) BANKED; /* 1=有檔,抄名 */
void    task_snap_out(void) BANKED;     /* 任務工作區 → 擁有者快照 */
void    task_bind_slot(void) BANKED;    /* 續玩:綁工作區到 active_slot */
void    task_bind_new(void) BANKED;     /* 新遊戲:清工作區並改綁 */

#endif
