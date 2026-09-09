/* learnfast.c - 原版 4ms 學習 tick 的常態快路徑。
 *
 * 完整 pyh_learn() 會在每 tick 重算經驗門檻、搜尋技能兩次並跨 bank
 * 取亂數，GBC 即使 batch 64 也只有約 130 tick/s。本檔只合併「已證明
 * 不會失敗或升級」的常態 tick；任何邊界都在改狀態前退回 pyh_learn，
 * 因而不跨過潛能/金錢耗盡、升級訊息或繼續學習詢問。
 */
#pragma bank 31
#include <gb/gb.h>
#include "learnfast.h"
#include "save.h"
#include "skill.h"
#include "serve.h"
#include "gamedata.h"
#include "ui.h"
#include "process_input.h"

static uint8_t learn_fast_done;
static uint8_t learn_y;
static uint8_t learn_first;
static uint16_t learn_frame_anchor;
static uint32_t learn_phase;

void learn_process_begin(void) BANKED
{
    learn_y = find_kf(kf_id);
    learn_phase = 0;
    learn_frame_anchor = sys_time;
    learn_first = 1;
}

/* 與 ui.c::random_it 完全相同的 LCG 結果，但學習的 range 都是 8-bit。
 * (s*65531+1)%range 可先把 65531 對 range 取模，避免每 tick 的
 * 32-bit 乘除；mud_seed 仍寫入同一個 32-bit 結果的低 16 位。 */
static uint16_t learn_rand(uint16_t range, uint16_t coefficient)
{
    uint16_t s, rem;

    if (!range)
        return 0;
    s = (mud_seed & 0xFF00)
      | (uint8_t)((uint8_t)(DIV_REG << 1) + DIV_REG + (uint8_t)mud_seed);
    mud_seed = (uint16_t)(1u - s - (uint16_t)(s << 2));
    if (range == 1)
        return 0;
    rem = s % range;
    return (uint16_t)((rem * coefficient + 1u) % range);
}

static uint16_t literate_fee(uint8_t level)
{
    if (level < 20)  return 5;
    if (level < 30)  return 10;
    if (level < 60)  return 50;
    if (level < 80)  return 150;
    if (level < 100) return 300;
    if (level < 120) return 500;
    return 1000;
}

static uint8_t learn_fast_batch(uint8_t ticks)
{
    uint8_t level, int_range, kar_range, max_first;
    uint16_t pot, threshold = 0, fee = 0, gain;
    uint16_t int_coef, kar_coef, max_gain, high;
    uint32_t need;
    uint8_t status = LEARN_FAST_OK;

    learn_fast_done = 0;
    if (learn_y == 0xFF || hero.man_kf[learn_y] != kf_id)
        return LEARN_FAST_BOUNDARY;

    level = hero.man_kf[learn_y + 1];
    /* 門檻只有 serve.c 一份(exp_need)。這裡原本抄了一份 level³/10——
     * 那正是 2026-07-30「改了一百多萬經驗仍報武學經驗不足」的形狀:
     * 門檻有兩份就遲早走岔。一批呼叫一次,跨 bank 成本可忽略。 */
    need = exp_need(level);
    if (learn_master_lvl < level || hero.man_exp < need)
        return LEARN_FAST_BOUNDARY;

    pot = (uint16_t)hero.man_kf[learn_y + 2]
        | ((uint16_t)hero.man_kf[learn_y + 3] << 8);
    if (level != 0xFF)
        threshold = (uint16_t)(level + 1) * (level + 1);
    if (kf_id == LITERATE_KF)
        fee = literate_fee(level);

    int_range = hero.man_int;
    kar_range = hero.man_kar / 5;
    int_coef = int_range ? (uint16_t)(65531u % int_range) : 0;
    kar_coef = kar_range ? (uint16_t)(65531u % kar_range) : 0;

    high = (uint16_t)(int_range >> 1)
         + (int_range ? (uint16_t)int_range - 1 : 0);
    max_first = (high >= 256) ? 127 : (uint8_t)(high >> 1);
    max_gain = (uint16_t)max_first + (kar_range ? kar_range - 1 : 0);

    while (learn_fast_done < ticks) {
        if ((learn_fast_done & 3) == 0
                && (process_take_cancel()
                    || (joypad() & (J_A | J_B | J_START)))) {
            waitpadup();
            status = LEARN_FAST_CANCEL;
            break;
        }
        if (!hero.man_pot || (fee && hero.man_money < fee)) {
            status = LEARN_FAST_BOUNDARY;
            break;
        }
        /* 若本 tick 有任何可能升級，交由 pyh_learn 做完整 RNG、狀態與 UI。
         * max_gain=0 時則可安全批次，避免零悟性角色永遠落回慢路徑。 */
        if (level != 0xFF
                && (pot >= threshold
                    || (max_gain && threshold - pot <= max_gain))) {
            status = LEARN_FAST_BOUNDARY;
            break;
        }

        hero.man_pot--;
        if (fee)
            hero.man_money -= fee;
        gain = (uint8_t)((int_range >> 1)
                         + learn_rand(int_range, int_coef)) >> 1;
        gain += learn_rand(kar_range, kar_coef);
        if (level != 0xFF)
            pot += gain;                    /* 已證明不會達升級線或溢位 */
        learn_fast_done++;
    }

    hero.man_kf[learn_y + 2] = (uint8_t)pot;
    hero.man_kf[learn_y + 3] = (uint8_t)(pot >> 8);
    return status;
}

#define LEARN_PHASE_DEN 262144UL
#define LEARN_PHASE_REM 48674UL

uint8_t learn_process_tick(void) BANKED
{
    uint16_t elapsed, due = 0;

    if (process_take_cancel()) {
        waitpadup();
        return 1;
    }
    /* 初次 full framebuffer 已畫完才起算，不能補扣玩家尚未看見畫面時
     * 經過的 tick。 */
    if (learn_first) {
        learn_first = 0;
        learn_frame_anchor = sys_time;
        return 0;
    }

    elapsed = (uint16_t)(sys_time - learn_frame_anchor);
    learn_frame_anchor = sys_time;
    /* 250 tick/s ÷ (4194304/70224 frame/s)
     * = 4 + 48674/262144 tick/frame，不使用 60Hz 近似。 */
    while (elapsed--) {
        due += 4;
        learn_phase += LEARN_PHASE_REM;
        if (learn_phase >= LEARN_PHASE_DEN) {
            learn_phase -= LEARN_PHASE_DEN;
            due++;
        }
    }

    while (due) {
        uint8_t chunk = (due > 32) ? 32 : (uint8_t)due;
        uint8_t status = learn_fast_batch(chunk);

        due -= learn_fast_done;
        if (status == LEARN_FAST_CANCEL)
            return 1;
        if (status == LEARN_FAST_BOUNDARY) {
            uint8_t old_level = (learn_y == 0xFF)
                              ? 0xFF : hero.man_kf[learn_y + 1];

            /* 失敗或即將升級的單一 tick 交回完整原版路徑，訊息、扣款與
             * 升級詢問不被快路徑跨過。 */
            if (pyh_learn())
                return 1;
            if (learn_y != 0xFF && hero.man_kf[learn_y + 1] != old_level) {
                /* 玩家看升級訊息/確認框的時間不能補算成大量學習 tick，
                 * 確認框按鍵也不能殘留成下一輪取消。 */
                (void)process_take_cancel();
                waitpadup();
                learn_phase = 0;
                learn_frame_anchor = sys_time;
                return 0;
            }
            if (due)
                due--;                     /* 慢路徑剛完成 1 tick */
        }
    }
    return 0;
}
