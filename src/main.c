/* main.c - 英雄坛说 GBC 移植:啟動入口
 *
 * 目前:直接進入地圖引擎(原版新遊戲初始位置)。
 * 原版 game: 的存檔載入/建角流程隨選單/文本模組移植後接入;
 * 建角已定案:密碼功能移除,名字用選擇或英文輸入。
 */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "fb.h"
#include "font.h"
#include "save.h"
#include "game.h"

/* 仿文曲星青灰 LCD 的四階配色 */
static const palette_color_t pal0[4] = {
    RGB(26, 29, 24),
    RGB(17, 20, 16),
    RGB(8, 10, 8),
    RGB(2, 3, 2),
};

void main(void)
{
    if (_cpu != CGB_TYPE)
        dmg_guard();            /* DMG/MGB:提示畫面,不返回 */
    cpu_fast();
    set_bkg_palette(0, 1, pal0);

    /* SRAM 常開:0xA000 起 288B 為存檔映像,0xA200 起為工作緩衝
     * (OutBuf/任務狀態/ptemp;WRAM 見底 → 棧曾溢底毀 you_char/ss_ptr,
     * 症狀=戰鬥訊息 $n 亂碼/整行亂字,2026-07-10)。存檔區僅
     * save_store 寫,斷電風險面不變。 */
    ENABLE_RAM;
    SWITCH_RAM(0);

    fb_init();

    SHOW_BKG;
    DISPLAY_ON;

    /* 等效原版 game: 段的取檔→進遊戲循環;
     * gmud_loop 返回=結束遊戲(exit_game),回到取檔重入 */
    for (;;) {
        game_boot();
        gmud_loop();
    }
}
