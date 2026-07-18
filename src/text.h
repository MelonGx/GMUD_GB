/* text.h - 文本 bank 讀取(等效 stringx.s get_text_data)+ 格式化/對話框 */
#ifndef TEXT_H
#define TEXT_H
#include <stdint.h>
#include <gb/gb.h>      /* BANKED */

/* 類別序 = text_addr.s text_class_tbl */
#define TC_NPC_NAME 0
#define TC_NPC_DATA 1
#define TC_KF 2
#define TC_OTHER 3
#define TC_NPC_ID 4
#define TC_NPC_QUEST 5
#define TC_NPC_EXP 6
#define TC_PERFORM 7
#define TC_GOODS_DESC 8

/* npc_state,佈局 = 原版 gmud.h(NPC_DATA_LEN=53 本體) */
typedef struct {
    uint8_t  npc_name[9];
    uint8_t  npc_busy, npc_pai, npc_gender, npc_age, npc_daode;
    uint8_t  npc_attack, npc_defense, npc_damage, npc_armor;
    uint32_t npc_exp;
    uint16_t npc_force;
    uint8_t  npc_str, npc_dex, npc_int, npc_con, npc_per, npc_kar;
    uint16_t npc_hp, npc_maxhp, npc_fp, npc_maxfp, npc_effhp;
    uint8_t  npc_goods[5];
    uint16_t npc_money;
    uint8_t  npc_usekf[5];
    uint8_t  npc_kfnum;
} npc_state_t;
#define NPC_DATA_LEN 53

extern npc_state_t npc;
extern uint8_t npc_kf[40];          /* id,等級 × kfnum */
extern uint8_t vendor_goods[10];
extern uint8_t obj_flag;        /* bit7: 1=主角視角(原版同名) */
extern uint8_t varbuf[16];      /* 格式化變數暫存(原版同名) */
extern uint8_t limb_flag;       /* 命中部位 0..15($l/$1;fight.c 設定) */

/* 連線對戰(nf.s;單機恆 0)。net_vars = 封包協議頭順序,整塊拷貝 */
extern uint8_t net_flag;        /* bit7=連線中 bit1=本方回合 */
extern uint8_t net_vars[7];
#define net_game_version (net_vars[0])
#define net_quit_flag    (net_vars[1])
#define net_msg_vision   (net_vars[2])
#define net_limb_flag    (net_vars[3])
#define net_repeat       (net_vars[4])
#define net_perform_flag (net_vars[5])
#define net_init_flag    (net_vars[6])
extern __at(0xA200) uint8_t scratch288[288];    /* SRAM;與 file_buf 分時複用 */
#define OutBuf scratch288       /* format_string 輸出(原版同名) */

/* gfx_scratch 分時複用配置(WRAM 緊張):
 * 地圖合成期間:+0 img_a,+128 img_b,+256 ground,+384 player(192B)
 * UI 期間(選單/訊息框,無地圖合成):
 *   +0   look_msg 工作副本(npc.c,79B)
 *   +80  skill_desc_buf(8)/+88 strong_desc_buf(4)(skill.h)
 *   +92  wuyi/chushou type9 記錄(npc.c,2×4B)
 *   +100 get_goods_name 輪替名字池(goods.c,3×12B)
 *   +376 npc_desc(200B,text_load_npc 起至對話結束) */
extern uint8_t gfx_scratch[576];    /* map.c 定義 */
#define npc_desc (gfx_scratch + 376)    /* NPC 長描述(200B,分時複用) */

uint16_t text_get_ptr(uint8_t cls, uint8_t id);
void text_load_npc(uint8_t id);     /* NPC_DATA → npc(bank25 限定) */
extern uint8_t perform_id;                  /* 原版同名($p 用;task.c 定義) */

/* format_string 子集:字面量 + $$/$r/$N/$n/$o;
 * 數值類型項與 $w$t$q$g$k$p 隨 goods/kf/task 模組補全 */
void format_string(const uint8_t *src);

/* 對話框(textui.c,bank 25):頂部兩行 + 分頁(等效 show_talk_msg) */
void show_talk_msg(void) BANKED;
void show_talk_msg_no_wait(void) BANKED; /* fight handles the final key */
void show_talk_msg0(void) BANKED;   /* 頂框首兩行,不等鍵(交易/學習標題) */

extern uint16_t goods_price;    /* 交易價格(npc.c 定義;模板槽用) */

/* 通用訊息框:清框+邊線+分頁顯示 OutBuf(等效 message_box_more) */
void message_box_more(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) BANKED;

/* C 端格式文本裡嵌入本機指針的巨集(小端) */
#define FW(p) (uint8_t)((uint16_t)(p) & 0xFF), (uint8_t)((uint16_t)(p) >> 8)

#endif
