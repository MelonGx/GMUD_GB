/* keyrep.c - 方向鍵連發版的 wait_key(HOME)
 *
 * wait_key() 開頭是 waitpadup():必須先鬆開全部按鍵才會取下一個鍵。
 * 游標類畫面因此完全沒有連發——實測建角取名三個畫面「按住方向鍵
 * 180 幀(3 秒)只走 1 格」(2026-07-29 用戶回報「很卡」)。
 *
 * 本函式把方向鍵改成逐幀取樣:首次立即,連續按住 REP_DELAY 次取樣後
 * 每次取樣都重複。A/B/START 維持 wait_key 的一次性語義(先 waitpadup
 * 再回報),確認鍵不連發。
 *
 * 節流其實落在呼叫端的重畫上:每動一格都要 fb_flush(全屏約 6 幀),
 * 所以這裡不必再加 rate 分頻——menu.c/battle_loot.c 那邊重畫便宜,
 * 才需要「每 4 幀一次」的分頻(見 docs/hidden.md §17)。
 *
 * rep_ticks 是 static:呼叫端重畫期間不在本函式內,取樣自然暫停,
 * 回來後接著數,節奏 = max(重畫時間, 取樣間隔)。
 *
 * 放 HOME:呼叫端散在 bank 23(input_py)、25(cheat)、28(create/pyh_quest)。
 */
#include <gb/gb.h>
#include <stdint.h>
#include "ui.h"

#define REP_DELAY 12            /* 按住幾次取樣後開始連發 */

static uint8_t rep_dir;         /* 上次取樣時按著的方向位 */
static uint8_t rep_ticks;

uint8_t wait_key_rep(void) NONBANKED
{
    uint8_t j, d, fresh, prev;

    if (ui_pushed_key) {                /* 與 wait_key 一致:回推優先 */
        j = ui_pushed_key;
        ui_pushed_key = 0;
        return j;
    }
    /* 進本函式時「已經按著」的 A/B/START 不算新按下,必須等放開再按。
     * wait_key 是靠開頭的 waitpadup() 擋這件事;這裡不能 waitpadup
     * (會連方向鍵連發一起擋掉),改成邊緣觸發。
     * 漏掉會出真 bug:開場動畫按住 A 跳過,動畫一結束名字清單立刻把
     * 那個還按著的 A 當成確認,直接選走第一個預設名——pinyin_test /
     * create_test 當場抓到(2026-07-29)。 */
    prev = joypad();
    for (;;) {
        vsync();
        heart_beat();                   /* 等效原版 wkey 迴圈跑 sys_refresh */
        j = joypad();
        fresh = (uint8_t)(j & ~prev);
        prev = j;

        if (fresh & (J_A | J_B | J_START)) {
            rep_dir = 0;
            rep_ticks = 0;
            if (fresh & J_A)
                return K_CR;
            if (fresh & J_B)
                return K_ESC;
            return K_F1;
        }

        d = j & (J_UP | J_DOWN | J_LEFT | J_RIGHT);
        if (!d) {
            rep_dir = 0;
            rep_ticks = 0;
            continue;
        }
        if (d != rep_dir) {             /* 換方向:立即回報並重新計時 */
            rep_dir = d;
            rep_ticks = 0;
        } else if (rep_ticks < 0xFF) {
            rep_ticks++;
        }
        if (rep_ticks == 0 || rep_ticks >= REP_DELAY) {
            if (d & J_UP)
                return K_UP;
            if (d & J_DOWN)
                return K_DOWN;
            if (d & J_LEFT)
                return K_LEFT;
            return K_RIGHT;
        }
    }
}
