/* link.c - Game Boy 對戰線(SB/SC)塊傳輸,取代原版 netengine.s
 * (NC2000 UART+IrDA)。硬體替代(用戶決定 2026-07-17):線上位元組
 * 協議為新設計;上層 nf.c 的封包內容/回合語義/錯誤路徑保持原版。
 *
 * 時鐘:SC 用普速檔(SIOF_CLOCK_INT,非 256KHz 快檔)。但注意 main.c 開了
 * cpu_fast(),CGB 倍速模式下串列時鐘同樣加倍 → 實際線速是 16384Hz 不是
 * 8192Hz,一個位元組 488µs(寶可夢金銀是 8192Hz/976µs)。倍速把從機的
 * 重裝視窗砍半,是本移植連線比金銀脆弱的主因之一,見下方 LK_GAP 註解。
 * 創建遊戲=master(內部時鐘),加入遊戲=slave。
 * 全雙工逐位元組交換;塊 = 同步(HELLO↔READY)→ 250B 資料 → 校驗和
 * → ACK/NAK,NAK/超時整塊重試 3 次。同步靠交換原子性:一次交換雙方
 * 同時看到對方同步字,同拍進入資料段;中途入場的錯位由校驗和攔下,
 * 重試回同步段自愈。
 *
 * 超時:slave 等時鐘、雙方等同步字都有輪詢預算 → 失敗碼,上層顯示
 * 原版「数据传送失败/数据接收失败」。 */
#pragma bank 31
#include <gb/gb.h>
#include <string.h>
#include "link.h"
#include "fb.h"                 /* fb_stream_drain:進資料段前收乾畫面串流 */

#define LK_HELLO 0xA5           /* 發送方同步字 */
#define LK_READY 0x5A           /* 接收方同步字 */
#define LK_ACK   0x06
#define LK_NAK   0x15
#define LK_IDLE  0x00
#define LK_RETRY 8              /* 資料段重試(2026-08-02 由 3 提高:一個位元組
                                 * 掉拍就整塊作廢,3 次太薄) */
#define LK_GAP   60             /* master 每位元組後的步調間隙迴圈次數。
                                 * 從機在這段時間內要:退出 xfer→存位元組→
                                 * 累加校驗和→再進 xfer→寫 SB→寫 SC。
                                 * 30 次(≈100µs)對「沒有中斷插進來」剛好夠,
                                 * 但沒有餘裕;倍速下一個位元組才 488µs,
                                 * 加倍到 ≈200µs 只讓整塊慢 ~20%,很划算。 */
#define LK_SYNC_TRIES 2048      /* 同步預算(對方在場但字節錯位時) */

/* 塊校驗:Fletcher(mod 256),兩個位元組。2026-08-02 由單一 8 位元和換掉。
 * 舊的純和有兩個要命的性質:
 *   · 誤收機率 1/256——連線對戰每回合傳一塊,玩久了必然撞上一次,撞上就是
 *     對方的角色資料被靜默寫錯;
 *   · 對「位元組錯位」完全免疫不了也完全查不出——而錯位正是本連線最常見
 *     的故障模式(從機漏掉一拍,之後整塊平移),純和換個順序結果不變。
 * s2 帶位置權重,錯位一格就變號;誤收降到約 1/65536。
 *
 * 注意這改變了線上協議(校驗和從 1 位元組變 2 位元組):**雙方都要換新
 * ROM**。舊版對新版會在校驗段對不上 → NAK → 重試耗盡 → 「数据传送失败」,
 * 不會靜默吃壞資料,但也連不起來。 */
#define LK_SUM(b)   do { s1 += (b); s2 += s1; } while (0)
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

/* 交換 1 位元組(全雙工)。master 供時鐘,交換後留 LK_GAP 步調間隙讓
 * slave 重裝 SB/SC;slave 以輪詢計數防死等。
 * 回 <0x100 = 對方位元組;0x100 = slave 等時鐘超時。
 *
 * 這裡沒有逐位元組的重同步:master 只要在 slave 還沒把 SC 重新武裝好之前
 * 就送出下一個時鐘,那個位元組對 slave 就是整個消失,之後全部錯位一格。
 * 所以資料段的正確性完全押在「slave 的重裝一定趕得上 LK_GAP」——呼叫端
 * 因此必須關中斷,見 link_send_block 上方。 */
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
        for (guard = 0; guard < LK_GAP; guard++)
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

/* 資料段全程關中斷。理由:從機在 master 兩個位元組之間,只有 LK_GAP 那段
 * 時間可以把 SB/SC 重新裝好。VBlank ISR(fb.c fb_vbl_isr:present_now /
 * tail_reap / joypad)只要插進這個視窗,從機就來不及重裝,那個位元組整個
 * 掉,之後每個位元組錯位一格 → 校驗和必錯 → 整塊重來。倍速下一個位元組
 * 488µs、一幀 16.7ms,250 位元組的塊會被 VBlank 打斷約 7 次,幾乎塊塊中招
 * ——這正是實機「数据传送失败」的形狀(2026-08-02 用戶回報)。
 *
 * 關中斷前先把畫面串流收乾:HBlank DMA 鏈在傳輸期間會週期性停住 CPU,
 * 一樣吃從機的餘裕;而且中斷關著時 fb_vbl_isr 不會去收鏈。一塊約 150ms
 * 沒有 VBlank 不影響任何東西(畫面靜止,sys_time 只用於行走/練功節奏)。
 *
 * 同步段不關:那裡要輪詢 joypad 等對方,可能等上十秒。 */

/* 發送 250B 塊。0=成功 1=失敗(超時/重試耗盡) */
uint8_t link_send_block(const uint8_t *buf)
{
    uint8_t try_n, s1, s2, ok;
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
            continue;

        fb_stream_drain();
        disable_interrupts();
        s1 = s2 = 0;
        ok = 1;
        for (i = 0; i < LINK_PKT_SIZE; i++) {
            r = xfer(buf[i]);
            if (r > 0xFF) {
                ok = 0;
                break;
            }
            LK_SUM(buf[i]);
        }
        if (ok) {
            r = xfer(s1);               /* 校驗和低位 */
            if (r > 0xFF)
                ok = 0;
        }
        if (ok) {
            r = xfer(s2);               /* 校驗和高位 */
            if (r > 0xFF)
                ok = 0;
        }
        if (ok) {
            r = xfer(LK_IDLE);          /* 讀 ACK/NAK */
            ok = (r == LK_ACK);
        }
        enable_interrupts();
        if (ok)
            return 0;
    }
    return 1;
}

/* 接收 250B 塊。0=成功 1=失敗 */
uint8_t link_recv_block(uint8_t *buf)
{
    uint8_t try_n, s1, s2, ok, got1;
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
            continue;

        fb_stream_drain();              /* 見 link_send_block 上方註解 */
        disable_interrupts();
        s1 = s2 = 0;
        got1 = 0;                       /* 只在低位收到後才被讀,先給值免警告 */
        ok = 1;
        for (i = 0; i < LINK_PKT_SIZE; i++) {
            r = xfer(LK_IDLE);
            if (r > 0xFF) {
                ok = 0;
                break;
            }
            buf[i] = (uint8_t)r;
            LK_SUM((uint8_t)r);
        }
        if (ok) {
            r = xfer(LK_IDLE);          /* 收校驗和低位 */
            if (r > 0xFF)
                ok = 0;
            else
                got1 = (uint8_t)r;
        }
        if (ok) {
            r = xfer(LK_IDLE);          /* 收校驗和高位 */
            if (r <= 0xFF && got1 == s1 && (uint8_t)r == s2) {
                xfer(LK_ACK);
                enable_interrupts();
                return 0;
            }
            xfer(LK_NAK);
        }
        enable_interrupts();
    }
    return 1;
}
