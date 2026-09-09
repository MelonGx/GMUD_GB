/* goods.h - 物品查詢(移植原版 goods.s + aux.s find_name) */
#ifndef GOODS_H
#define GOODS_H
#include <stdint.h>
#include <gb/gb.h>      /* BANKED */

/* 等效原版 get_goods_attr 後 a1 所指的 8 字節記錄(拷貝到 WRAM):
 * [0]=類型 [1]=子類 [2..5]=效果(BOOK:[2]=書表索引) [6..7]=價格 */
extern uint8_t goods_attr[8];

/* BANKED = 實作在 goods_impl.c(bank 31);無標記者留在 HOME,原因見
 * goods_impl.h(選單函式指標表的目標必須恆常可達)。 */

/* 名字查詢:回傳 0xFF 結尾字串(輪替緩衝 3 格,npc_look 需同持 3 名) */
const uint8_t *gd_find_name(const uint8_t *tbl, uint8_t id);  /* HOME:自切 bank */
const uint8_t *get_goods_name(uint8_t id);

void get_goods_attr(uint8_t id);            /* → goods_attr */
uint8_t find_goods(uint8_t id);             /* → man_goods 字節偏移,0xFF=無 */
uint8_t add_goods(uint8_t id) BANKED;       /* 取得一件,1=成功 */
void lose_wielded_weapon(void) BANKED;      /* 擊落/投擲現在武器，回退數值並消耗 1 */

uint8_t show_goods(void);                   /* 主選單「物品」(menu_fn) */
void set_all_goods(void) BANKED;            /* 全部物品 → dmenu_buf(賣出) */

#endif
