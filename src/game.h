/* game.h - 引擎全域狀態,變數名沿用原版 h/gmud.h 以便逐行對照
 *
 * 常量對應原版:ScreenX/Y、Unit、Role 尺寸,BLAK/DOOR_OFF/NPC_OFF 分段。
 */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>
#include <gb/gb.h>      /* BANKED */

#define ScreenX      160
#define ScreenY      144
#define Unit_Width   32
#define Unit_Height  32
#define Role_Width   32
#define Role_Height  48
#define ScreenX_Num  (ScreenX / Unit_Width)   /* 5 */
#define ScreenY_Num  ((ScreenY + Unit_Height - 1) / Unit_Height)
#define Role_Anchor_Y ((ScreenY - Role_Height) / 2)

#define BLAK      1024
#define DOOR_OFF  2048
#define NPC_OFF   3072
#define INIT_MAP_LEN    11
#define CHANGE_MAP_LEN  (2 + 16 + 16)
#define DEAD_NPC_IMG    315    /* 死亡 NPC 顯示的墳頭圖 */

/* ---- init_map 讀入的檔頭(佈局與原版 G_Total_Map 起連續變數一致) ---- */
extern uint16_t G_Total_Map;
extern uint16_t G_Total_Door;
extern uint8_t  G_Curr_Map;
extern uint8_t  G_Init_X, G_Init_Y;
extern uint16_t G_Door_Addr;
extern uint16_t G_Npc_Addr;
extern uint16_t G_Map_Addr;

/* ---- change_map0 讀入的地圖頭 ---- */
extern uint8_t  G_Map_Width, G_Map_Height;
extern uint8_t  G_Map_Ground[16];
extern uint8_t  G_MapName[17];      /* +1 給 C 字串結尾 */
extern uint16_t G_Scene;            /* 格子陣列在 map blob 中的偏移 */

/* ---- 視窗與主角 ---- */
extern uint8_t  G_Sx, G_Sy;         /* 視窗左上角的格座標 */
extern uint8_t  G_Dx, G_Dy;         /* 捲動細偏移 0..31 */
extern uint8_t  G_Min_Sx, G_Max_Sx; /* hide fully solid side borders */
extern uint8_t  G_role_status;      /* 步態 0..2 */
extern uint8_t  G_role_way;         /* 0下 1左 2右 3上 */
extern uint8_t  G_role_speed;
extern uint8_t  G_role_X, G_role_Y;

/* ---- 碰撞/互動 ---- */
extern uint16_t G_Curr_Id;          /* 主角面對(或所站)格的原始值 */
extern uint16_t G_id_item;          /* 碰撞 NPC 的圖 id */
extern uint8_t  located_id;         /* 碰撞 NPC 的編號 */

extern uint8_t  npc_stat_buf[32];   /* NPC 存活位圖(0xFF=全活) */
extern uint8_t  G_Task_Flag;        /* bit7:殺手任務進行中 */

/* ---- 殺手系統 + busy(map.c 定義,原版 gmud.h 同名) ---- */
extern uint8_t  G_Killer_Map, G_Killer_X, G_Killer_Y;
extern uint8_t  ghost_gender_bak;
extern uint8_t  busy_flag;          /* bit7:戰鬥/學習/釣魚(暫停回復) */

uint8_t npc_alive(uint8_t id);      /* 等效 check_stat 位圖查詢(HOME) */
void change_map0(void);             /* 讀圖頭(不動視窗;random_map 用) */
uint16_t get_object0(uint16_t cell_index);  /* 格子原始值(去地面位) */

/* ---- map.c(tools.s 移植;換圖冷路徑在 map_cold.c bank26) ---- */
void tile_fetch(uint16_t id, uint8_t *dst);   /* HOME:圖庫 id→128B */
void init_map(void) BANKED;
void change_map(void) BANKED;
void make_ground(void);
void show_player(void);             /* = main_move(增量捲動,見 map.c) */
void map_invalidate(void);          /* scroll_buf 內容作廢 → 下次全量 */
extern uint8_t iv_force_full;       /* 調試:1=停用增量路徑 */
void adjust_status(void);
uint8_t move_blak(void);            /* 1=擋路(sec) 0=可走(clc) */
uint8_t check_hit(void);            /* 1=面前有 NPC */
void message_loop(void) BANKED;     /* 走上門格→換圖 */

/* ---- walk.c(gmud.s 移植,bank 25) ---- */
void gmud_loop(void) BANKED;

/* ---- npc.c(npc.s/talk.s 移植) ---- */
void object_say(void) BANKED;
void master1(void) BANKED;  /* 學習選單(TODO ⑥ 拜師/學習) */

/* ---- game.c(game.s 移植,bank 25) ---- */
extern uint8_t quit_flag;   /* 結束遊戲 → 回標題畫面 */
extern uint32_t save_time;  /* 下次可存檔時刻(game_boot 錨定) */
void show_game_menu(void) BANKED; /* F1/START 主選單 */
void setup_maxhp_cmd(void) BANKED; /* 等效 setup_maxhp(年齡/精血上限) */

/* ---- create.c(建角流程③,bank 28):標題/捲軸/取名/屬性 ---- */
void game_boot(void) BANKED; /* 標題→載檔或建角(等效 game: 段,密碼已移除) */
void dmg_guard(void) BANKED; /* 非 CGB 機型:提示畫面後停機(不返回) */
/* 16px 捲軸(主題/結局共用;非 BANKED,僅限 bank 28 內直呼) */
void scroll_text_28(const uint8_t *txt, uint8_t total, uint8_t skippable);

/* ---- npc.c ---- */
void init_npc_b(void) BANKED;   /* 載 located_id 的 NPC 記錄+動態加成 */

#endif
