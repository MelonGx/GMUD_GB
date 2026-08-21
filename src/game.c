/* game.c - 主選單與角色維護,逐例程移植原版 game.s
 *
 * 對照表:show_game_menu/main_menu/sys_menu/neili_menu/show_look_menu/
 *   show_hp/show_desc/show_score/show_more/save_game/end_game/
 *   setup_maxhp → 同名
 * 開機流程 game: 段與建角維護(game_boot/new_game/set_attr/clear_apply/
 *   check_player)在 create.c(bank 28,隨模組③遷出救 bank25 空間)
 * 結束遊戲:原版退回文曲星系統;GBC 無宿主 → quit_flag 回標題畫面
 * 代碼在 bank 25。
 */
#pragma bank 26
#include <gb/gb.h>
#include <string.h>
#include "game.h"
#include "save.h"
#include "text.h"
#include "menu.h"
#include "menutext.h"
#include "ui.h"
#include "font.h"
#include "fb.h"
#include "blit.h"
#include "skill.h"
#include "goods.h"
#include "serve.h"
#include "cheat.h"
#include "gamedata.h"
#include "task.h"
#include "fight_internal.h"

#define MAIN_X0   8
#define MAIN_Y0   1
#define FRAME_Y0  4
#define SYS_X0    114
#define LOOK_X0   6
#define LOOK_X1   156
#define SAVE_INTERVAL 300

uint8_t quit_flag;
uint32_t save_time;             /* create.c(game_boot)錨定 */

#define tplbuf (gfx_scratch + 192)      /* 模板工作副本(≤184B) */

static void putw(uint8_t *p, const void *v)
{
    uint16_t a = (uint16_t)v;
    p[0] = (uint8_t)a;
    p[1] = (uint8_t)(a >> 8);
}

/* ---- 查看:show_more 分頁(5 行;DOWN 續頁,LEFT/RIGHT/ESC 回推) ---- */
static void show_more(void)
{
    uint8_t k;

    for (;;) {
        fb_fill_rect(LOOK_X0 + 1, FRAME_Y0 + 12,
                     LOOK_X1 - LOOK_X0 - 1, 6 * 12 - 12 - 1, 0);
        if (!show_string(5, LOOK_X0 / 6, FRAME_Y0 + 12)) {
            fb_flush();
            return;
        }
        font_draw_ascii(108, 68, 'v');      /* 原版閃爍游標位(18,70) */
        fb_flush();
        for (;;) {
            k = wait_key();
            if (k == K_DOWN)
                break;                      /* 續頁 */
            if (k == K_LEFT || k == K_RIGHT || k == K_ESC) {
                push_key(k);                /* 回推給查看頁籤 */
                return;
            }
        }
    }
}

/* ---- 查看·狀態(show_hp) ---- */
static void show_hp(void)
{
    varbuf[0] = percent(hero.man_effhp, hero.man_maxhp);
    memcpy(tplbuf, mt_hp_msg, sizeof mt_hp_msg);
    fix_mt_hp_msg(tplbuf);
    format_string(tplbuf);
    ss_ptr = OutBuf;
    show_more();
}

/* ---- 查看·描述(show_desc) ---- */
static void show_desc(void)
{
    uint8_t g = hero.man_gender;
    uint8_t per;

    putw(varbuf, gd_find_name(pai_name_tbl, hero.man_pai));

    varbuf[2] = mt_xingbei[g << 1];         /* 男/女/人 字符本體 */
    varbuf[3] = mt_xingbei[(g << 1) + 1];

    if (hero.man_age < 16) {
        putw(varbuf + 4, mt_no_per_msg);
    } else {
        const uint8_t *tbl;
        if (g == 0)
            tbl = mt_man_per_desc;
        else if (g == 1)
            tbl = mt_girl_per_desc;
        else
            tbl = (hero.game_hour & 1) ? mt_man_per_desc
                                       : mt_girl_per_desc;
        per = hero.man_per;
        if (per < 10)
            per = 10;
        if (per > 31)
            per = 31;
        per = (uint8_t)(per - 10) / 3;      /* 8 檔;低值使用第一檔 */
        putw(varbuf + 4, tbl + (uint16_t)per * 18);
    }

    obj_flag = 0x80;
    set_obj(0);
    get_skill_desc();
    putw(varbuf + 6, skill_desc_buf);
    putw(varbuf + 8, strong_desc_buf);
    mt_wuyi_rec[0] = 8;                     /* {8, dw varbuf+6} */
    putw(mt_wuyi_rec + 1, varbuf + 6);
    mt_chushou_rec[0] = 4;
    putw(mt_chushou_rec + 1, varbuf + 8);

    memcpy(tplbuf, mt_desc_msg, sizeof mt_desc_msg);
    fix_mt_desc_msg(tplbuf);
    format_string(tplbuf);
    ss_ptr = OutBuf;
    show_more();
}

/* ---- 查看·屬性(show_score) ---- */
static void show_score(void)
{
    memcpy(tplbuf, mt_score_msg, sizeof mt_score_msg);
    fix_mt_score_msg(tplbuf);
    format_string(tplbuf);
    ss_ptr = OutBuf;
    show_more();
}

static const show_fn look_s[3] = { show_hp, show_desc, show_score };
/* look_menu(01000100b/3/00010000b/ICON_MENU1):CR 即退 */
static const cmenu_t look_menu =
    { 3, 3, 1, 0, MSTYLE_ICON1, 4, MF_SHOW_OWNS_INPUT,
      0, look_s, mt_look_menu_items, 0 };

/* 等效 show_look_menu:外框 + 頁籤選單 */
static uint8_t show_look_menu(void)
{
    fb_fill_rect(LOOK_X0 - 1, FRAME_Y0 - 2,
                 LOOK_X1 - LOOK_X0 + 2, 6 * 12 + 3, 0);
    ui_hline(LOOK_X0 - 1, LOOK_X1, FRAME_Y0 - 2);
    ui_hline(LOOK_X0 - 1, LOOK_X1, FRAME_Y0 + 6 * 12);
    ui_vline(LOOK_X0 - 1, FRAME_Y0 - 2, FRAME_Y0 + 6 * 12);
    ui_vline(LOOK_X1, FRAME_Y0 - 2, FRAME_Y0 + 6 * 12);

    pop_menu(LOOK_X0 + 8 * 6, FRAME_Y0, &look_menu);
    scroll_to_lcd();
    fb_flush();
    return 0;
}

/* ---- 功能·內力(neili_menu) ---- */
static uint8_t to_dazuo(void)
{
    dazuo_cmd();
    return 0;
}

static uint8_t to_jiali(void)
{
    jiali();
    scroll_to_lcd();
    fb_flush();
    return 0;
}

static uint8_t to_xiqi(void)
{
    recover();
    scroll_to_lcd();
    fb_flush();
    return 0;
}

static uint8_t to_heal(void)
{
    heal();
    scroll_to_lcd();
    fb_flush();
    return 0;
}

static const menu_fn neili_h[4] = { to_dazuo, to_jiali, to_xiqi, to_heal };
static const cmenu_t neili_menu =
    { 4, 1, 0, 1, MSTYLE_ARROW, 0, 0, neili_h, 0, mt_neili_menu_items, 0 };

/* 等效 show_neili_menu:PullMenu 1 → 獨立運行,退出即全退 */
static uint8_t show_neili_menu(void)
{
    scroll_to_lcd();
    fb_flush();
    pop_menu(SYS_X0, FRAME_Y0 + 8, &neili_menu);
    scroll_to_lcd();
    fb_flush();
    return 2;
}

/* 等效 show_practice(PullMenu 1 同上) */
static uint8_t show_practice(void)
{
    scroll_to_lcd();
    fb_flush();
    practice_cmd();
    return 2;
}

/* ---- 存檔/結束 ---- */
static uint8_t save_game(void)
{
    memcpy(hero.npc_stat_buf, npc_stat_buf, 32);
    save_store();
    setup_maxhp_cmd();
    save_time = hero.mud_age + SAVE_INTERVAL;
    format_string(mt_save_succ_msg);
    message_box(72, 10, 72 + 12 * 5, 26);
    scroll_to_lcd();
    fb_flush();
    return 1;                               /* 回主選單 */
}

static uint8_t end_game(void)
{
    if (!confirm_box(6, 20, 6 + 12 * 12, 48, mt_confirm_msg)) {
        scroll_to_lcd();
        fb_flush();
        return 1;                           /* no:回主選單 */
    }
    if (hero.mud_age >= save_time) {
        if (confirm_box(12, 32, 12 + 12 * 12, 32 + 38, mt_if_save_msg)) {
            memcpy(hero.npc_stat_buf, npc_stat_buf, 32);
            save_store();
        }
    }
    quit_flag = 1;                          /* 等效 exit_game */
    return 2;
}

/* ---- 速度設定(GBC 新增,用戶要求)----
 * EXT:打坐/練功 400ms,學習 250 tick/s;ADV/BSC:打坐/練功約 x8,
 * 學習暫同 EXT。其餘顯示與獎勵差異見 save.h;選單不顯示倍率。 */
static const uint8_t it_ext[] = "EXT";
static const uint8_t it_adv[] = "ADV";
static const uint8_t it_bsc[] = "BSC";
static const uint8_t it_speed[] = "\xCB\xD9\xB6\xC8 ";      /* 速度 */

static uint8_t set_speed(void)
{
    if (menu_set != 2)
        fenshen_disable_bsc_feature();
    speed_mode = menu_set;
    return 1;
}

static const menu_fn spd_h[3] = { set_speed, set_speed, set_speed };
static const uint8_t *const spd_items[3] = { it_ext, it_adv, it_bsc };
static const cmenu_t speed_menu =
    { 3, 1, 0, 1, MSTYLE_RADIO, 4, MF_USESET, spd_h, 0, spd_items, 0 };

static uint8_t show_speed_menu(void)
{
    menu_set = (speed_mode <= 2) ? speed_mode : 0;  /* 游標=現值 */
    pop_menu(84, FRAME_Y0 + 8, &speed_menu);
    return 0;                               /* 回功能選單 */
}

/* ---- 作弊選單(yobdc 專用) — 實作在 cheat.c (bank 29) ---- */
static const uint8_t it_cheat[] = "\xD7\xF7\xB1\xD7 ";  /* 作弊 */

static uint8_t show_cheat_menu(void)
{
    cheat_main();
    scroll_to_lcd();
    fb_flush();
    return 0;
}

/* 功能選單:原版 4 項 + 速度 + 作弊(yobdc) */
static const menu_fn sys_h[6] = {
    show_neili_menu, show_practice, save_game, end_game,
    show_speed_menu, show_cheat_menu,
};
static const uint8_t *const sys_items[6] = {
    mt_sys_menu_i0, mt_sys_menu_i1, mt_sys_menu_i2, mt_sys_menu_i3,
    it_speed, it_cheat,
};
static const cmenu_t sys_menu =
    { 5, 1, 0, 1, MSTYLE_ARROW, 0, 0, sys_h, 0, sys_items, 0 };
static const cmenu_t sys_menu_cheat =
    { 6, 1, 0, 1, MSTYLE_ARROW, 0, 0, sys_h, 0, sys_items, 0 };

static uint8_t show_sys_menu(void)
{
    const cmenu_t *m = yobdc_mode() ? &sys_menu_cheat : &sys_menu;
    return (pop_menu(SYS_X0, FRAME_Y0, m) == 0xFE) ? 2 : 0;
}

/* ---- 主選單 ---- */
static const menu_fn main_h[4] = {
    show_look_menu, show_goods, show_skills, show_sys_menu,
};
/* main_menu(00000000b/4/10000100b/ARROW):橫排 4 項帶框 */
static const cmenu_t main_menu =
    { 4, 4, 1, 1, MSTYLE_ARROW, 0, 0, main_h, 0, mt_main_menu_items, 0 };

void show_game_menu(void) BANKED
{
    scroll_to_lcd();
    fb_flush();
    pop_menu(MAIN_X0, MAIN_Y0, &main_menu);
    scroll_to_lcd();
    fb_flush();
}

/* setup_maxhp_cmd → create.c(bank 28,省 bank 25 空間)
 * 開機/建角段(game_boot/new_game/set_attr/clear_apply/check_player)
 * → create.c(bank 28) */
