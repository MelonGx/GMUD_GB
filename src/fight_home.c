/* fight_home.c - 戰鬥主選單(HOME):fight_menu + 5 處理器 thunk
 *
 * pop_menu(bank 25)直呼 handler 函指標,故處理器必須在 HOME 或 bank 25。
 * 戰鬥引擎在 bank 27(fight.c),此處薄 thunk 以 BANKED 跨 bank 呼叫。
 *
 * 原版 fight_menu 為 ICON_MENU(攻击/绝招/吸气/物品/逃跑)。GBC 12px CJK
 * 字寬下 5 項橫排超出 160px 螢幕。用戶決定(2026-07-09):改直排放到
 * GBC 新增的下方狀態列(y≥80),不擠壓 160×80 戰鬥畫面;NORMAL 反白、
 * 方向鍵 ▲▼ 選擇。此為字寬硬體限制下的排版取捨。
 *
 * 置 bank 25(與 pop_menu 同 bank,處理器可直呼;紓解 HOME bank0 餘裕)。
 */
#pragma bank 25
#include <gb/gb.h>
#include "fight.h"
#include "menu.h"
#include "save.h"
#include "text.h"
#include "skill.h"
#include "gamedata.h"

static const uint8_t it_attack[] = { 0xB9, 0xA5, 0xBB, 0xF7, 0xFF };  /* 攻击 */
static const uint8_t it_perform[] = { 0xBE, 0xF8, 0xD5, 0xD0, 0xFF }; /* 绝招 */
static const uint8_t it_xiqi[] = { 0xCE, 0xFC, 0xC6, 0xF8, 0xFF };    /* 吸气 */
static const uint8_t it_goods[] = { 0xCE, 0xEF, 0xC6, 0xB7, 0xFF };   /* 物品 */
static const uint8_t it_escape[] = { 0xCC, 0xD3, 0xC5, 0xDC, 0xFF };  /* 逃跑 */

static uint8_t h_attack(void)  { return fight_normal_attack(); }
static uint8_t h_perform(void) { return fight_perform_action(); }
static uint8_t h_xiqi(void)    { return fight_xiqi(); }
static uint8_t h_goods(void)   { return fight_goods(); }
static uint8_t h_escape(void)  { return fight_escape(); }

static const menu_fn fight_h[5] =
    { h_attack, h_perform, h_xiqi, h_goods, h_escape };
static const uint8_t *const fight_items[5] =
    { it_attack, it_perform, it_xiqi, it_goods, it_escape };

/* 暫:橫排放狀態列(line_form=5)。狀態列僅 32px 高,直排 5 項(60px)
 * 放不下;最終方案待用戶定(見交付說明)。 */
const cmenu_t fight_menu =
    { 5, 5, 0, 0, MSTYLE_NORMAL, 0, 0, fight_h, 0, fight_items, 0 };

/* ================= 絕招清單(skill.s show_perform/get_all_perform) =========
 * pf_req_kf(=skill.dat pf_attr_tbl 的 bank25 本地副本;gamedata 版在 bank24
 * 需切 bank,此處逐位掃描故留本地):perform_id → 所需 kf_id(掃已啟用的
 * 攻/閃/內功槽,命中即列入)。門檻(judge_kf/judge_force)施展時再驗。 */
static const uint8_t pf_req_kf[25] = {
    11, 11, 12, 13,             /* daoying×2 / zhangdao×2:八卦刀/掌/阵 */
    17, 18, 20,                 /* luoying / liulang / sanhua */
    23, 25, 25,                 /* feizhi / honglian / leidong */
    26, 26, 29, 29,             /* fenshen / yianmu / lianzhan / yidao */
    30, 30, 30,                 /* chan / lian / taoyue */
    31, 31, 31, 31,             /* ji / luanhuan / yinyang / zhen */
    36, 38, 39,                 /* bingxin / liuchu / shengui */
    0xFF,
};

#define PERFORM_X0 40
#define PERFORM_Y0 40

static uint8_t pf_choice;               /* select_it:選中的 perform_id */
static uint8_t pf_count;                /* get_all_perform 條目數 */

/* get_perform_loop:usekf_byte 啟用中 → pf_attr_tbl 掃匹配,perform_id 入列 */
static void get_perform_loop(uint8_t usekf_byte)
{
    uint8_t x;

    if (!(usekf_byte & 0x80))
        return;
    kf_id = usekf_byte & 0x7F;
    for (x = 0; pf_req_kf[x] != 0xFF; x++) {
        if (pf_req_kf[x] == kf_id)
            dmenu_buf[1 + pf_count++] = x;
    }
}

/* get_all_perform:攻(兵器需 check_skill;否則拳腳)+ 閃 + 內功 各掃一輪 */
static void get_all_perform(void)
{
    pf_count = 0;
    if (hero.man_weapon & 0x80) {
        if (hero.man_usekf[1] & 0x80
            && check_skill(hero.man_usekf[1] & 0x7F))
            get_perform_loop(hero.man_usekf[1]);        /* 兵器招 */
    } else {
        get_perform_loop(hero.man_usekf[0]);            /* 拳腳招 */
    }
    get_perform_loop(hero.man_usekf[2]);                /* 輕功招 */
    get_perform_loop(hero.man_usekf[3]);                /* 內功招 */
    dmenu_buf[0] = pf_count;
}

/* select_it:CR → 記下 perform_id,退選單(PullMenu) */
static uint8_t select_pf(void)
{
    pf_choice = dmenu_buf[1 + menu_set];
    return 1;
}

static const menu_fn perform_h[1] = { select_pf };

/* 原版 perform_menu:ARROW、動態、框、scr_form=3、char_form=10、單程序 */
static const cmenu_t perform_menu =
    { 0, 1, 3, 1, MSTYLE_ARROW, 10, 0, perform_h, 0, 0, pf_name_tbl };

/* 等效 skill.s show_perform:列可用絕招,回選中 perform_id;0xFF=取消/無 */
uint8_t show_perform(void) BANKED
{
    get_all_perform();
    if (pf_count == 0)
        return 0xFF;                    /* 無可用絕招 → 視同取消 */
    pf_choice = 0xFF;
    pop_menu(PERFORM_X0, PERFORM_Y0, &perform_menu);
    return pf_choice;
}
