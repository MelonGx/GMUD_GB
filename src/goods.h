/* goods.h - 物品查詢(移植原版 goods.s + aux.s find_name) */
#ifndef GOODS_H
#define GOODS_H
#include <stdint.h>

/* 等效原版 get_goods_attr 後 a1 所指的 8 字節記錄(拷貝到 WRAM):
 * [0]=類型 [1]=子類 [2..5]=效果(BOOK:[2]=書表索引) [6..7]=價格 */
extern uint8_t goods_attr[8];

/* 名字查詢:回傳 0xFF 結尾字串(輪替緩衝 3 格,npc_look 需同持 3 名) */
const uint8_t *gd_find_name(const uint8_t *tbl, uint8_t id);
const uint8_t *get_goods_name(uint8_t id);

void get_goods_attr(uint8_t id);            /* → goods_attr */
uint8_t find_goods(uint8_t id);             /* → man_goods 字節偏移,0xFF=無 */
uint8_t add_goods(uint8_t id);              /* 取得一件,1=成功 */

uint8_t show_goods(void);                   /* 主選單「物品」(menu_fn) */
void set_all_goods(void);                   /* 全部物品 → dmenu_buf(賣出) */

#endif
