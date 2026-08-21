/* goods_impl.h - goods.c 的 banked 實作層(goods_impl.c,bank 31)
 *
 * 為什麼要拆:goods.c 的選單處理器是靠函式指標表(use_h/goods_h/…)被
 * menu.c(bank 26)呼叫的,那些指標的目標必須恆常可達 ⇒ 表和處理器
 * 本體只能留在 HOME。真正佔位的實作(物品掃描/使用/描述)沒有這個
 * 限制,直接呼叫即可,故遷出 HOME 換空間(2026-07-29,bank0 只剩 168B)。
 *
 * 留在 HOME 的還有 show_study/study_cmd:它們引用 mt_*(menutext.c 在
 * bank 26),那批字串只在選單會話期間、bank 26 掛著時才讀得到。
 */
#ifndef GOODS_IMPL_H
#define GOODS_IMPL_H
#include <stdint.h>
#include <gb/gb.h>

/* goods.c(HOME)的 UI 層與本層共用的狀態,原版同名 */
extern uint8_t goods_type;      /* 目前分類 */
extern uint8_t goods_id;        /* 選中項(含 bit7 裝備位) */

void gi_set_goods(void) BANKED;         /* 等效 set_goods */
void gi_set_other_goods(void) BANKED;   /* 等效 set_other_goods */
void gi_clear_goods_desc(void) BANKED;
void gi_show_desc(uint8_t stride) BANKED;   /* 等效 goods_show_desc */

void gi_use_food(uint8_t x) BANKED;
void gi_use_drug(uint8_t x) BANKED;
void gi_use_weapon(uint8_t x) BANKED;
void gi_use_equip(uint8_t x) BANKED;

#endif
