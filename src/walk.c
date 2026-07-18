/* walk.c - 行走控制,逐分支移植原版 gmud.s
 *
 * proc_up/down/left/right → 同名函數;每按一次走 4 步(每步 speed=8px,
 * 共一格),步間 delay。原版 delay 是 6502 空轉迴圈(≈50ms),GBC 以
 * 幀數近似(硬體時基不同,屬時序替代)。
 * 原版按鍵→GBC:方向鍵=十字鍵,CR=A,F4 選單=START(選單模組後續移植)。
 *
 * 代碼在 bank 25(僅呼叫 HOME 例程與同 bank 選單/NPC 模組,無自身
 * ROM 切換;遷出為 bank0 騰空間)。
 */
#pragma bank 25
#include <gb/gb.h>
#include "game.h"
#include "fb.h"
#include "text.h"
#include "ui.h"

static void step_flush(void)
{
    fb_flush_stream();  /* 逐子步全刷(8px/更新;混合 DMA 後單次 ~1.5 幀) */
}

/* 等效 up_key_rts:每步收尾 */
static void step_end(void)
{
    adjust_status();
    show_player();
    step_flush();
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
            a = Unit_Height - G_role_speed;
            G_Dy = a ? a : Unit_Height;
            G_Sy--;
        }
    } else if (G_Sy == 0 && G_Dy != 0) {        /* 收尾細偏移 */
        G_Dy -= G_role_speed;
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
        if (G_Dy >= 32) {
            G_Dy = 0;
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
            a = Unit_Width - G_role_speed;
            G_Dx = a ? a : Unit_Width;
            G_Sx--;
        }
    } else if (G_Sx == G_Min_Sx && G_Dx != 0) {
        G_Dx -= G_role_speed;
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
        if (G_Dx >= 32) {
            G_Dx = 0;
            G_Sx++;
        }
    } else {
        G_role_X += G_role_speed;
    }
    step_end();
}

/* 等步調:每子步固定 WALK_PACE 幀。2026-07-14 順滑化後逐子步全刷
 * (8px/更新);2026-07-15 提速:flush 管線把 sync/轉置挪進翻面等待,
 * 子步自然耗時 ~2 幀,PACE=2 → 每格 4×2=8 幀、30Hz 均勻節奏
 * (用戶反饋 PACE 3 拖慢感)。
 * 排程錨 walk_t0 為絕對時間網格,連續按住跨格保持
 * (格與格之間亦恰 1 個 PACE),中斷後自動重錨;偶發超時自動追回。 */
#define WALK_PACE 2

static uint16_t walk_t0;                        /* 絕對排程錨 */
static const uint8_t way_pad[4] = { J_DOWN, J_LEFT, J_RIGHT, J_UP };

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
    walk_t0 += 4 * WALK_PACE;   /* 網格推進:下一格無縫銜接 */
    message_loop();             /* 站上門格→換圖 */
    /* 換圖幀須立即呈現;按住同方向續走時翻面延到下一子步的
     * VBlank(與子步節奏同拍),停走則立即收掉掛起翻面 */
    if ((G_Curr_Id >= DOOR_OFF && G_Curr_Id < NPC_OFF)
        || !(joypad() & way_pad[way]))
        fb_flush();
}

/* 等效 gmud.s proc_cr:CR→check_hit→object_say */
static void proc_cr(void)
{
    if (check_hit())
        object_say();
}

/* 等效 gmud.s proc_main:F1(START)→主選單 */
static void proc_main(void)
{
    show_game_menu();
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
            proc_walk(3, up_one);
            continue;
        }
        if (k & J_DOWN) {
            proc_walk(0, down_one);
            continue;
        }
        if (k & J_LEFT) {
            proc_walk(1, left_one);
            continue;
        }
        if (k & J_RIGHT) {
            proc_walk(2, right_one);
            continue;
        }
        if (k & J_A) {
            fb_flush();
            proc_cr();
            if (quit_flag)
                return;         /* 自殺刪檔/戰死 → 回開機流程 */
            waitpadup();
            fb_flush();
            continue;
        }
        if (k & J_START) {
            fb_flush();
            waitpadup();
            proc_main();
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
