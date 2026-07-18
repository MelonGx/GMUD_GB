/* link.h - Game Boy 對戰線傳輸層(取代原版 netengine.s 的 NC2000
 * UART+IrDA;硬體替代,用戶決定 2026-07-17)。
 * 全部入口 BANKED(bank 30),供 nf.c(同 bank 直呼亦可)使用。 */
#ifndef LINK_H
#define LINK_H
#include <stdint.h>
#include <gb/gb.h>

#define LINK_PKT_SIZE 250       /* 原版 data_size */

extern uint8_t link_mock;       /* 1=環回自測(等效原版 nf.s debug 模式) */
extern uint8_t link_patience;   /* 0=握手期(~10s 視窗) 1=對局期(等對方
                                 * 出招無上限,B 取消);link_init 清 0 */

void link_init(uint8_t master);         /* CommunicateInit;1=創建(內時鐘) */
void link_exit(void);                   /* CommunicateExit */
uint8_t link_send_block(const uint8_t *buf);    /* 250B;0=成功 1=失敗 */
uint8_t link_recv_block(uint8_t *buf);          /* 250B;0=成功 1=失敗 */

#endif
