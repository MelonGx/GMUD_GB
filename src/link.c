/* link.c - Game Boy 對戰線(SB/SC)塊傳輸,取代原版 netengine.s
 * (NC2000 UART+IrDA)。硬體替代(用戶決定 2026-07-17):線上位元組
 * 協議為新設計;上層 nf.c 的封包內容/回合語義/錯誤路徑保持原版。
 *
 * 普速 8192Hz(相容性最廣:任何 GB/GBC 線、BGB、TGB Dual;GBA 紫線
 * 腳位不同不可用)。創建遊戲=master(內部時鐘),加入遊戲=slave。
 * 全雙工逐位元組交換;塊 = 同步(HELLO↔READY)→ 250B 資料 → 校驗和
 * → ACK/NAK,NAK/超時整塊重試 3 次。同步靠交換原子性:一次交換雙方
 * 同時看到對方同步字,同拍進入資料段;中途入場的錯位由校驗和攔下,
 * 重試回同步段自愈。
 *
 * 超時:slave 等時鐘、雙方等同步字都有輪詢預算 → 失敗碼,上層顯示
 * 原版「数据传送失败/数据接收失败」。 */
#pragma bank 30
#include <gb/gb.h>
#include <string.h>
#include "link.h"

#define LK_HELLO 0xA5           /* 發送方同步字 */
#define LK_READY 0x5A           /* 接收方同步字 */
#define LK_ACK   0x06
#define LK_NAK   0x15
#define LK_IDLE  0x00
#define LK_RETRY 3
#define LK_SYNC_TRIES 2048      /* 同步預算(對方在場但字節錯位時) */
#define LK_SYNC_TOUTS 400       /* 同步期容忍超時數:×~24ms ≈10s 等待視窗
                                 * (2026-07-17 修:舊制 slave 每次超時 ~0.3s
                                 * ×32×3 輪重試 ≈30s 凍結,體感死機) */

static uint8_t lk_master;
uint8_t link_mock;
uint8_t link_patience;  /* 0=握手期(視窗盡即失敗) 1=對局期(等對方出招,
                         * 可等任意久,B 鍵取消)。link_init 清 0,
                         * nf.c 初始互換成功後置 1 */

/* 環回緩衝(mock):SRAM bank0 尾段(net_pkt 0xBD80+250 之後) */
__at(0xBE80) static uint8_t mock_buf[LINK_PKT_SIZE];

/* 交換 1 位元組(全雙工)。master 供時鐘,交換後留 ~120µs 步調間隙
 * 讓 slave 輪詢迴圈重裝 SB;slave 以輪詢計數防死等(≈0.5s)。
 * 回 <0x100 = 對方位元組;0x100 = slave 等時鐘超時。 */
static uint16_t xfer(uint8_t out)
{
    uint16_t guard;
    uint8_t in;

    SB_REG = out;
    if (lk_master) {
        SC_REG = SIOF_XFER_START | SIOF_CLOCK_INT;
        guard = 0;
        while (SC_REG & SIOF_XFER_START) {
            if (++guard == 4096) {      /* 硬體 ~1ms 必完成;此保險防
                                         * 無串列實作的模擬器死等 */
                SC_REG = 0;
                return 0x100;
            }
        }
        in = SB_REG;
        for (guard = 0; guard < 30; guard++)
            ;                           /* 步調間隙 */
        return in;
    }
    SC_REG = SIOF_XFER_START;           /* 外部時鐘:等 master */
    guard = 0;
    while (SC_REG & SIOF_XFER_START) {
        if (++guard == 4096) {          /* ~24ms,與 master 對齊;等待視窗
                                         * 由同步層的 LK_SYNC_TOUTS 累計 */
            SC_REG = 0;                 /* 撤銷本次待傳 */
            return 0x100;
        }
    }
    return SB_REG;
}

void link_init(uint8_t master)
{
    lk_master = master;
    link_patience = 0;
    SC_REG = 0;
    SB_REG = 0xFF;
}

void link_exit(void)
{
    SC_REG = 0;
    SB_REG = 0xFF;
}

/* 同步:反覆送 my_word 直到收到 peer_word。
 * 回 0=同步成功 1=整體失敗(視窗盡/B 取消) 2=本輪預算盡(重試一輪)。
 * 對局期(link_patience)無視窗上限,只認 B 取消;B 需先見過釋放才受理,
 * 免得選單殘留的按壓誤觸退出。 */
static uint8_t sync_word(uint8_t my_word, uint8_t peer_word)
{
    uint16_t tries, touts = 0;
    uint16_t r;
    uint8_t b_armed = (joypad() & J_B) ? 0 : 1;

    for (tries = 0; ; tries++) {
        if (tries == LK_SYNC_TRIES) {
            if (!link_patience)
                return 2;
            tries = 0;
        }
        r = xfer(my_word);
        if (r == peer_word)
            return 0;
        if (joypad() & J_B) {
            if (b_armed)
                return 1;
        } else {
            b_armed = 1;
        }
        if (r > 0xFF && !link_patience && ++touts == LK_SYNC_TOUTS)
            return 1;
    }
}

/* 發送 250B 塊。0=成功 1=失敗(超時/重試耗盡) */
uint8_t link_send_block(const uint8_t *buf)
{
    uint8_t try_n, sum;
    uint16_t r;
    uint8_t i;

    if (link_mock) {                    /* 環回:寫入 mock 緩衝 */
        memcpy(mock_buf, buf, LINK_PKT_SIZE);
        return 0;
    }
    for (try_n = 0; try_n < LK_RETRY; try_n++) {
        r = sync_word(LK_HELLO, LK_READY);
        if (r == 1)
            return 1;                   /* 視窗盡/B 取消:不燒重試 */
        if (r == 2)
            goto next_try;
        sum = 0;
        for (i = 0; i < LINK_PKT_SIZE; i++) {
            r = xfer(buf[i]);
            if (r > 0xFF)
                goto next_try;
            sum += buf[i];
        }
        r = xfer(sum);                  /* 校驗和 */
        if (r > 0xFF)
            goto next_try;
        r = xfer(LK_IDLE);              /* 讀 ACK/NAK */
        if (r == LK_ACK)
            return 0;
next_try: ;
    }
    return 1;
}

/* 接收 250B 塊。0=成功 1=失敗 */
uint8_t link_recv_block(uint8_t *buf)
{
    uint8_t try_n, sum;
    uint16_t r;
    uint8_t i;

    if (link_mock) {                    /* 環回:讀 mock 緩衝 */
        memcpy(buf, mock_buf, LINK_PKT_SIZE);
        return 0;
    }
    for (try_n = 0; try_n < LK_RETRY; try_n++) {
        r = sync_word(LK_READY, LK_HELLO);
        if (r == 1)
            return 1;                   /* 視窗盡/B 取消:不燒重試 */
        if (r == 2)
            goto next_try;
        sum = 0;
        for (i = 0; i < LINK_PKT_SIZE; i++) {
            r = xfer(LK_IDLE);
            if (r > 0xFF)
                goto next_try;
            buf[i] = (uint8_t)r;
            sum += (uint8_t)r;
        }
        r = xfer(LK_IDLE);              /* 收校驗和 */
        if (r <= 0xFF && (uint8_t)r == sum) {
            xfer(LK_ACK);
            return 0;
        }
        xfer(LK_NAK);
next_try: ;
    }
    return 1;
}
