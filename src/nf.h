/* nf.h - 聯機對戰(nf.s 移植,bank 30)跨 bank 介面 */
#ifndef NF_H
#define NF_H
#include <stdint.h>
#include <gb/gb.h>

/* 封包(原版 data_size=250):[0..9]協議頭 [10..129]人物 [130..229]訊息。
 * SRAM bank0 常開,任何 bank 可讀寫。 */
extern __at(0xBD80) uint8_t net_pkt[250];
#define NET_PKT_MSG (net_pkt + 130)

extern uint8_t nf_over;         /* 網戰結束旗(退出/錯誤/勝負) */

void netfight(void) BANKED;             /* 擂台入口(pyh_quest) */
uint8_t nf_create_game(void) BANKED;    /* 選單處理器(nf_home.c 經 thunk) */
uint8_t nf_join_game(void) BANKED;

/* 發送當前狀態+已暫存訊息(fight.c 先以 net_stage_msg 拷入 NET_PKT_MSG,
 * 訊息可能指向呼叫方 bank 的 ROM,必須在其 bank 映射時拷貝)。
 * 0=成功;1=失敗(已顯示錯誤並置 nf_over/combat_over) */
uint8_t net_send_data(void) BANKED;

#endif
