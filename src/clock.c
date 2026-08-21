/* clock.c - 遊戲世界時鐘與自然回復(等效 system.s sys_refresh)
 *
 * 2026-08-02 從 ui.c 遷出:bank 26(UI 引擎組)已經逼到上限,而本模組
 * 只碰 WRAM 全域與 HOME 的 find_kf,放哪個 bank 都一樣。兩個入口本來就是
 * BANKED,呼叫端(walk.c/game.c)不受影響。
 */
#pragma bank 31
#include <gb/gb.h>
#include "ui.h"
#include "game.h"
#include "save.h"
#include "skill.h"

#define TICK_TIME 15                /* 原版 gmud.h:15 秒一跳 */

/* 一秒是 4194304/70224 = 262144/4389 幀 ≈ 59.7275,不是 60。寫 60 會讓
 * 遊戲鐘每秒慢 0.456%——一小時慢 16 秒,一天慢 6 分半。改成 59 幀為底、
 * 餘數 3193/4389 累加(262144 = 59×4389 + 3193),長期平均精確。 */
#define SEC_REM 3193
#define SEC_DEN 4389

static uint16_t hb_anchor;
static uint16_t sec_phase;
static uint8_t sec_frames = 60;
static uint8_t timetick;

void heart_beat_reset(void) BANKED
{
    hb_anchor = sys_time;
    timetick = TICK_TIME;
}

void heart_beat(void) BANKED
{
    uint16_t v;
    uint8_t y;

    while ((uint16_t)(sys_time - hb_anchor) >= sec_frames) {
        hb_anchor += sec_frames;
        sec_phase += SEC_REM;           /* 排下一秒是 59 還是 60 幀 */
        if (sec_phase >= SEC_DEN) {
            sec_phase -= SEC_DEN;
            sec_frames = 60;
        } else {
            sec_frames = 59;
        }

        if (++hero.game_sec >= 60) {
            hero.game_sec = 0;
            if (++hero.game_min >= 60) {
                hero.game_min = 0;
                hero.game_hour++;
            }
        }
        hero.mud_age++;

        if (busy_flag & 0x80)       /* 戰鬥/學習/釣魚中不回復 */
            continue;
        if (--timetick)
            continue;
        timetick = TICK_TIME;

        if (hero.man_food == 0)
            continue;
        hero.man_food--;
        if (hero.man_water == 0)
            continue;
        hero.man_water--;

        /* 精血:con/2 + maxfp/16;到頂時 effhp 緩升 */
        v = (hero.man_maxfp >> 4) + (hero.man_con >> 1);
        hero.man_hp += v;
        if (hero.man_hp >= hero.man_effhp) {
            hero.man_hp = hero.man_effhp;
            if (hero.man_effhp < hero.man_maxhp)
                hero.man_effhp++;
        }

        /* 內力:基本內功等級/次 */
        if (hero.man_maxfp == 0)
            continue;
        if (hero.man_fp >= hero.man_maxfp)
            continue;
        y = find_kf(0);             /* BASIC_FORCE_KF */
        if (y == 0xFF)
            continue;
        hero.man_fp += hero.man_kf[y + 1];
        if (hero.man_fp >= hero.man_maxfp)
            hero.man_fp = hero.man_maxfp;
    }
}
