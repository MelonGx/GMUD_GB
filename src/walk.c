/* walk.c - 行走控制,逐分支移植原版 gmud.s
 *
 * proc_up/down/left/right → 同名函數;每格仍走滿 32px 並逐子步送屏。
 * 普通走路每格 4 個 8px 子步,每個子步都真正送屏(節奏見 proc_walk)。
 * 原版 delay 是 6502 空轉迴圈(≈50ms),GBC 以幀數近似(硬體時基不同,
 * 屬時序替代)。
 * 原版按鍵→GBC:方向鍵=十字鍵,CR=A,F4 選單=START(選單模組後續移植)。
 *
 * 代碼在 bank 25(僅呼叫 HOME 例程與同 bank 選單/NPC 模組,無自身
 * ROM 切換;遷出為 bank0 騰空間)。
 */
#pragma bank 26
#include <gb/gb.h>
#include "game.h"
#include "fb.h"
#include "save.h"
#include "text.h"
#include "ui.h"

/* ADV/BSC only: holding B batches twelve cells (48 ordinary 8px substeps)
 * between screen commits.  The GBC renderer makes an EXT substep much more
 * expensive than WALK_PACE alone; the batch size is calibrated against the
 * measured EXT world-position rate to deliver x32 walking.  Position still
 * advances through the ordinary substep functions; collision and door
 * handling remain cell-by-cell. */
#define WALK_TURBO_CELLS 12

static uint8_t walk_silent;

/* At the map's minimum scroll edge, a 16px ordinary frame may consume the
 * remaining camera offset and continue into the actor coordinate.  Keep this
 * in HOME so the nearly-full walking bank only pays a small direct call. */
static void edge_sub(uint8_t vertical) NONBANKED
{
    uint8_t rest;

    if (vertical) {
        if (G_Dy >= G_role_speed) {
            G_Dy -= G_role_speed;
            return;
        }
        rest = G_role_speed - G_Dy;
        G_Dy = 0;
        G_role_Y = (G_role_Y >= rest) ? G_role_Y - rest : 0;
    } else {
        if (G_Dx >= G_role_speed) {
            G_Dx -= G_role_speed;
            return;
        }
        rest = G_role_speed - G_Dx;
        G_Dx = 0;
        G_role_X = (G_role_X >= rest) ? G_role_X - rest : 0;
    }
}

static void step_flush(void)
{
    fb_flush_stream();  /* 每個 8/16px 子幀全刷;混合 DMA 後單次 ~1.5 幀 */
}

/* 等效 up_key_rts:每步收尾 */
static void step_end(void)
{
    adjust_status();
    if (!walk_silent) {
        show_player();
        step_flush();
    }
}

/* ---- gmud.s up_one ---- */
static void up_one(void)
{
    uint8_t a;

    if (G_Sy != 0 && G_role_Y <= Role_Anchor_Y + 8) { /* 捲動 */
        a = G_Dy - G_role_speed;
        if (G_Dy >= G_role_speed) {             /* 無借位 */
            G_Dy = a;
        } else {
            G_Dy = (uint8_t)(G_Dy + Unit_Height - G_role_speed);
            G_Sy--;
        }
    } else if (G_Sy == 0 && G_Dy != 0) {        /* 收尾細偏移 */
        edge_sub(1);
    } else {                                    /* 移動主角 */
        a = G_role_Y - G_role_speed;
        G_role_Y = (G_role_Y >= G_role_speed) ? a : 0;
    }
    step_end();
}

/* ---- gmud.s down_one ---- */
static void down_one(void)
{
    if ((uint8_t)(G_Map_Height - ScreenY_Num) > G_Sy
        && G_role_Y >= Role_Anchor_Y) {
        G_Dy += G_role_speed;
        if (G_Dy >= Unit_Height) {
            G_Dy -= Unit_Height;
            G_Sy++;
        }
    } else {
        G_role_Y += G_role_speed;
    }
    step_end();
}

/* ---- gmud.s left_one ---- */
static void left_one(void)
{
    uint8_t a;

    if (G_Sx > G_Min_Sx
        && G_role_X <= ScreenX_Num / 2 * Unit_Width - 8) {
        a = G_Dx - G_role_speed;
        if (G_Dx >= G_role_speed) {
            G_Dx = a;
        } else {
            G_Dx = (uint8_t)(G_Dx + Unit_Width - G_role_speed);
            G_Sx--;
        }
    } else if (G_Sx == G_Min_Sx && G_Dx != 0) {
        edge_sub(0);
    } else {
        a = G_role_X - G_role_speed;
        G_role_X = (G_role_X >= G_role_speed) ? a : 0;
    }
    step_end();
}

/* ---- gmud.s right_one ---- */
static void right_one(void)
{
    if (G_Sx < G_Max_Sx
        && G_role_X >= ScreenX_Num / 2 * Unit_Width - 8) {
        G_Dx += G_role_speed;
        if (G_Dx >= Unit_Width) {
            G_Dx -= Unit_Width;
            G_Sx++;
        }
    } else {
        G_role_X += G_role_speed;
    }
    step_end();
}

/* 等步調:WALK_PACE 是每子步最低幀數,不是完整每格耗時。逐子步全刷
 * (8px/更新)受合成/提交限制,ROM 實測 30 格/623 幀=20.77 幀/格。
 * 排程錨 walk_t0 為絕對時間網格,連續按住跨格保持;中斷後自動重錨,
 * 偶發超時自動追回。 */
/* WALK_PACE 實測不影響節奏(一次更新 ~5 幀本來就超過期限,等待迴圈幾乎
 * 不執行);設 5 讓它真的生效反而慢 2%,故維持原值。 */
#define WALK_PACE 2

static uint16_t walk_t0;                        /* 絕對排程錨 */
static const uint8_t way_pad[4] = { J_DOWN, J_LEFT, J_RIGHT, J_UP };

/* One turbo display beat.  Each complete cell is collision-tested before
 * its four normal 8px substeps.  Poll between cells as well, limiting key
 * release overshoot and never walking through a blocker or NPC. */
static void proc_walk_turbo(uint8_t way, void (*one)(void))
{
    uint8_t cell, i, door;

    G_role_way = way;
    walk_silent = 1;
    for (cell = 0; cell < WALK_TURBO_CELLS; cell++) {
        if (cell && ((joypad() & (way_pad[way] | J_B))
                     != (way_pad[way] | J_B)))
            break;
        if (move_blak()) {
            adjust_status();
            break;
        }
        door = (G_Curr_Id >= DOOR_OFF && G_Curr_Id < NPC_OFF);
        for (i = 0; i < 4; i++)
            one();
        message_loop();
        if (door)
            break;
    }
    walk_silent = 0;
    show_player();
    step_flush();
    walk_t0 = sys_time;         /* release B resumes the ordinary pace cleanly */
}

/* 等效 proc_up/down/left/right 的公共骨架。
 * 原版兩步間有 6502 空轉 delay(≈50ms);GBC 端每子步的合成+刷新
 * 本身已 ≥ 原版停頓,額外 delay 只添頓挫,故移除(用戶已要求更順)。 */
static void proc_walk(uint8_t way, void (*one)(void))
{
    uint8_t i;

    G_role_way = way;
    if (move_blak()) {          /* 擋路:轉身原地一幀 */
        step_end();
        return;
    }
    if ((uint16_t)(sys_time - walk_t0) >= WALK_PACE)
        walk_t0 = sys_time;     /* 非連續行走:重錨 */

    /* 每格一律 4 個 8px 子步。
     *
     * 這裡曾經是「4 格循環 8+8+16 / 8+8+16 / 8+8+16 / 8+8+8+8」,靠少畫
     * 3 張畫面換 x1.25(2026-08-01 用戶回報「有跳幀的感覺」)。原因不是
     * 掉幀而是速度不等:一次更新的成本幾乎與位移無關(實測 8px 5.3 幀、
     * 16px 5.6 幀),所以 16px 那一步的視覺速度剛好是相鄰兩步的兩倍——
     * 每 3 次更新衝一次,再疊上「3 子步格 / 4 子步格」的 4 格大週期。
     *
     * 均速就不能混步長。實測(map0 y=13 長廊,300 幀):
     *     8+8+16 混合  17.33 幀/格   速度 8px→1.51px/幀、16px→2.86px/幀
     *     一律 8px     19.93 幀/格   全程 1.61px/幀
     * 只慢 13%(不是 25%,因為 16px 子步本身也比較貴),換掉 100% 的速度
     * 擺動,值得。更快又均速的唯一辦法是把一次更新的 5.3 幀壓下來
     * (組成 2.8 幀 + 提交 2.6 幀),那是 map.c/blit.c/fb.c 的事,見交付說明。 */
    G_role_speed = 8;
    for (i = 0; i < 4; i++) {
        one();
        while ((uint16_t)(sys_time - walk_t0) < (uint16_t)(i + 1) * WALK_PACE) {
            /* CGB HALT/vsync while HBlank DMA owns the bus is undefined.
             * Draining may itself reach the deadline; recheck before HALT. */
            fb_stream_drain();
            if ((uint16_t)(sys_time - walk_t0) <
                (uint16_t)(i + 1) * WALK_PACE)
                vsync();
        }
    }
    walk_t0 = sys_time;
    message_loop();             /* 站上門格→換圖 */
    /* 換圖幀須立即呈現;按住同方向續走時翻面延到下一子步的
     * VBlank(與子步節奏同拍),停走則立即收掉掛起翻面 */
    if ((G_Curr_Id >= DOOR_OFF && G_Curr_Id < NPC_OFF)
        || !(joypad() & way_pad[way]))
        fb_flush();
}

/* 等效 gmud.s gmud: 入口 + wait_for_key 主循環 */
void gmud_loop(void) BANKED
{
    uint8_t k;

    G_role_speed = 8;
    G_role_status = 0;
    G_role_way = 0;

    init_map();
    show_player();
    fb_flush();

    while (1) {
        /* 先輪詢後閒置等幀:連續行走不因此多耗 1 幀 */
        k = joypad();
        /* 注意:寫成長 else-if 鏈會觸發 SDCC 優化器缺陷
         * (warning 110 EVELYN),尾端分支被吃掉;改用獨立 if+continue */
        if (k & J_UP) {
            if ((k & J_B) && speed_mode)
                proc_walk_turbo(3, up_one);
            else
                proc_walk(3, up_one);
            continue;
        }
        if (k & J_DOWN) {
            if ((k & J_B) && speed_mode)
                proc_walk_turbo(0, down_one);
            else
                proc_walk(0, down_one);
            continue;
        }
        if (k & J_LEFT) {
            if ((k & J_B) && speed_mode)
                proc_walk_turbo(1, left_one);
            else
                proc_walk(1, left_one);
            continue;
        }
        if (k & J_RIGHT) {
            if ((k & J_B) && speed_mode)
                proc_walk_turbo(2, right_one);
            else
                proc_walk(2, right_one);
            continue;
        }
        if (k & J_A) {
            fb_flush();
            if (check_hit())
                object_say();
            if (quit_flag)
                return;         /* 自殺刪檔/戰死 → 回開機流程 */
            waitpadup();
            fb_flush();
            continue;
        }
        if (k & J_START) {
            fb_flush();
            waitpadup();
            show_game_menu();
            waitpadup();
            fb_flush();
            if (quit_flag)
                return;         /* 等效 exit_game:回開機流程 */
        }
        fb_flush();             /* 閒置:收掉行走串流掛起的翻面 */
        vsync();
        heart_beat();           /* 等效原版主迴圈 sys_refresh */
    }
}
