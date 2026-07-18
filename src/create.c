/* create.c - 開機流程與建角(模組③,bank 28)
 *
 * 對照原版:
 *   game.s game:/create_game/exit_game → game_boot(標題畫面=文曲星宿主
 *     的 GBC 對應;密碼校驗已移除=既定決策)
 *   game.s new_game/set_attr/clear_apply/check_player → 同名(自 game.c
 *     遷入,bank25 已滿)
 *   scroll.s scroll → theme_scroll(16px 大字逐像素上捲,任意鍵跳過;
 *     首行後空一行=show_head 佈局;fb 第 80..95 行為隱形暫存帶)
 *   input.s input_new/adjust_attr → input_new/adjust_attr(取名改
 *     清單+英文字母格=既定決策,無拼音輸入法;密碼段移除)
 * GBC 新增(用戶決策 2026-07-10):雙存檔 slot,標題畫面 繼續/重新開始,
 *   重新開始時選 slot(佔用需確認覆蓋);遊戲內存檔恆寫當前 slot。
 */
#pragma bank 28
#include <gb/gb.h>
#include <string.h>
#include "game.h"
#include "save.h"
#include "ui.h"
#include "font.h"
#include "fb.h"
#include "blit.h"       /* fo_copy_fwd(捲軸平移) */
#include "skill.h"
#include "task.h"
#include "gamedata.h"
#include "theme_data.h"
#include "input_py.h"

#define SAVE_INTERVAL 300

/* ---- UI 文本(GB2312) ---- */
static const uint8_t s_gmud[]  = "GMUD";
static const uint8_t s_title[] = "\xD3\xA2\xD0\xDB\xCC\xB3\xCB\xB5";    /* 英雄坛说 */
static const uint8_t s_cont[] = "\xBC\xCC\xD0\xF8\xD3\xCE\xCF\xB7";     /* 继续游戏 */
static const uint8_t s_restart[] = "\xD6\xD8\xD0\xC2\xBF\xAA\xCA\xBC";  /* 重新开始 */
static const uint8_t s_start[] = "\xBF\xAA\xCA\xBC\xD3\xCE\xCF\xB7";    /* 开始游戏 */
static const uint8_t s_slot1[] = "\xB4\xE6\xB5\xB5\xD2\xBB\x20";        /* 存档一 */
static const uint8_t s_slot2[] = "\xB4\xE6\xB5\xB5\xB6\xFE\x20";        /* 存档二 */
static const uint8_t s_empty[] = "\x28\xBF\xD5\x29";                    /* (空) */
static const uint8_t s_cover[] = "\xB8\xB2\xB8\xC7\xB4\xE6\xB5\xB5\x3F";/* 覆盖存档? */
static const uint8_t s_name[] = "\xD0\xD5\xC3\xFB\x3A";                 /* 姓名: */
static const uint8_t s_en[] = "\xD3\xA2\xCE\xC4\xCA\xE4\xC8\xEB";       /* 英文输入 */
static const uint8_t s_cn[] = "\xD6\xD0\xCE\xC4\xCA\xE4\xC8\xEB";       /* 中文输入 */
static const uint8_t s_left[] = "\xD3\xE0\xB5\xE3";                     /* 余点 */
static const uint8_t s_ok[] = "\x41\x20\xC8\xB7\xB6\xA8";               /* A 确定 */
static const uint8_t s_no[] = "\x42\x20\xC8\xA1\xCF\xFB";               /* B 取消 */

static const uint8_t s_n0[] = "\xB3\xFE\xD4\xB6\xBA\xBD";               /* 楚远航 */
static const uint8_t s_n1[] = "\xC0\xEE\xE5\xD0\xD2\xA3";               /* 李逍遥 */
static const uint8_t s_n2[] = "\xB3\xFE\xC1\xF4\xCF\xE3";               /* 楚留香 */
static const uint8_t s_n3[] = "\xC2\xBD\xD0\xA1\xB7\xEF";               /* 陆小凤 */
static const uint8_t s_n4[] = "\xCE\xF7\xC3\xC5\xB4\xB5\xD1\xA9";       /* 西门吹雪 */
static const uint8_t s_n5[] = "\xD1\xE0\xC4\xCF\xCC\xEC";               /* 燕南天 */
static const uint8_t s_n6[] = "\xCA\xAF\xC6\xC6\xCC\xEC";               /* 石破天 */
static const uint8_t s_n7[] = "\xC1\xEE\xBA\xFC\xB3\xE5";               /* 令狐冲 */
static const uint8_t *const name_tbl[8] = {
    s_n0, s_n1, s_n2, s_n3, s_n4, s_n5, s_n6, s_n7,
};

/* 屬性名(順序=save_data attr_str..attr_con,原版 input_scr 同序) */
static const uint8_t s_str[] = "\xEB\xF6\xC1\xA6\x3A";                  /* 膂力: */
static const uint8_t s_dex[] = "\xC3\xF4\xBD\xDD\x3A";                  /* 敏捷: */
static const uint8_t s_int[] = "\xCE\xF2\xD0\xD4\x3A";                  /* 悟性: */
static const uint8_t s_con[] = "\xB8\xF9\xB9\xC7\x3A";                  /* 根骨: */
static const uint8_t *const attr_lbl[4] = { s_str, s_dex, s_int, s_con };

/* ---- 小工具 ---- */
static void cursor_show(uint8_t x, uint8_t y)
{
    font_draw_ascii(x, y, '>');
    fb_flush();
}

static void cursor_hide(uint8_t x, uint8_t y)
{
    fb_fill_rect(x, y, 8, 13, 0);
}

/* 2 位數(前導空格),v ≤ 99 */
static void draw2(uint8_t x, uint8_t y, uint8_t v)
{
    fb_fill_rect(x, y, 16, 13, 0);
    if (v >= 10)
        font_draw_ascii(x, y, (uint8_t)('0' + v / 10));
    font_draw_ascii(x + 8, y, (uint8_t)('0' + v % 10));
}

/* ================= 角色維護(game.s,自 game.c 遷入) ================= */

/* 等效 new_game(man_init_data 逐字段照抄) */
static void new_game(void)
{
    memset(&hero, 0, sizeof hero);
    hero.game_ver = GAME_VERSION;
    memcpy(hero.game_pid, "HERO", 4);
    hero.attr_str = hero.attr_dex = hero.attr_int = 20;
    hero.attr_con = hero.attr_per = hero.attr_kar = 20;
    hero.man_master = 0xFF;
    memcpy(hero.man_name, s_n0, 6);         /* 楚远航(預設名) */
    hero.man_pai = 0;                       /* NONE_PAI */
    hero.man_age = 14;
    hero.man_daode = 128;
    hero.man_str = hero.man_dex = hero.man_int = 20;
    hero.man_con = hero.man_per = hero.man_kar = 20;
    hero.man_hp = 100;
    hero.man_maxhp = 100;
    hero.man_effhp = 100;
    hero.man_food = 100;
    hero.man_maxfood = 100;
    hero.man_water = 100;
    hero.man_maxwater = 100;
    hero.man_money = 100;
    memset(hero.npc_stat_buf, 0xFF, 32);
}

/* 等效 set_attr:食/水上限由膂力底值決定 */
static void set_attr(void)
{
    hero.man_maxfood = (uint16_t)(hero.attr_str + 5) * 15;
    hero.man_maxwater = (uint16_t)(hero.attr_str + 4) * 15;
}

/* 等效 clear_apply:清物品欄,發一件布衣 */
static void clear_apply(void)
{
    memset(hero.man_goods, 0, sizeof hero.man_goods);
    hero.man_goods[0] = CLOTH_GOODS;
    hero.man_goods[1] = 1;
    hero.top_dance = 100;
    hero.top_ball = 100;
}

/* 等效 check_player:載檔後合法性修整 */
static void check_player(void)
{
    uint8_t i, n;

    hero.man_attack = 0;                    /* 卸下所有武器裝備效果 */
    hero.man_defense = 0;
    hero.man_damage = 0;
    hero.man_armor = 0;
    hero.man_weapon = 0;
    memset(hero.man_equip, 0, sizeof hero.man_equip);
    for (i = 0; i < MAX_GOODS * 2; i += 2)
        hero.man_goods[i] &= 0x7F;

    for (n = 0; n < hero.man_kfnum; n++) {  /* 武功表合法性 */
        if (hero.man_kf[(uint8_t)(n << 2)] >= KF_NUM)
            break;
    }
    hero.man_kfnum = n;

    for (i = 0; i < MAX_USEKF; i++) {
        if ((hero.man_usekf[i] & 0x7F) >= KF_NUM)
            hero.man_usekf[i] = 0;
    }

    if (hero.man_effhp > hero.man_maxhp)
        hero.man_effhp = hero.man_maxhp;
    if (hero.man_hp > hero.man_effhp)
        hero.man_hp = hero.man_effhp;
}

/* ================= 主題捲軸(scroll.s scroll) ================= */

static const uint8_t *sc_text;      /* 捲軸文本(20 字節定寬行) */

/* 畫第 idx 行(20 字節定寬)於像素行 y(16px 大字) */
static void theme_line(uint8_t idx, uint8_t y)
{
    uint8_t buf[21];

    memcpy(buf, sc_text + (uint16_t)idx * 20, 20);
    buf[20] = 0;
    font_draw_text(0, y, buf);
}

/* 等效 scroll/ending 共用 scroll1:首屏=行0/空行/正文行(show_head),
 * 其後逐行 16px 上捲。skippable=任意鍵跳過(do_delay 語義);結局
 * (原版 ending 把 if_esc NOP 掉)不可跳。
 * 全屏化(2026-07-18 用戶要求):內容 0..127(標題+空行+6 正文行),
 * 行 128..143=進場預備帶(原版機制,先前在 80..95);平移 2860B/px
 * 改 fo_copy_fwd(memmove ~40cy/B 會拖慢節奏)。
 * 注意:非 BANKED,僅供 bank 28 內(create/pyh_quest)直呼。 */
void scroll_text_28(const uint8_t *txt, uint8_t total, uint8_t skippable)
{
    uint8_t curr, px, n;

    sc_text = txt;
    font_set_height(16);
    fb_clear();
    theme_line(0, 0);
    n = total - 1;              /* 首屏正文行 1..n(y=16+16i),短文夾住 */
    if (n > 6)
        n = 6;
    for (curr = 1; curr <= n; curr++)
        theme_line(curr, (uint8_t)(16 + curr * 16));
    fb_flush();

    while (joypad())            /* 洩掉前一畫面按住的鍵 */
        vsync();

    /* 原版 to_scroll:curr n → total-1,每輪捲入下一行 */
    for (curr = n; curr < total - 1; curr++) {
        theme_line(curr + 1, 128);
        for (px = 0; px < 16; px++) {
            fo_dst = fb;
            fo_src = fb + FB_STRIDE;
            fo_len = (uint16_t)143 * FB_STRIDE;
            fo_copy_fwd();
            memset(fb + (uint16_t)143 * FB_STRIDE, 0, FB_STRIDE);
            fb_mark_dirty(0, 144);
            fb_flush();
            vsync();
            vsync();            /* ≈3 幀/px ≈ 原版 DELAY_TIME 節奏 */
            if (skippable && joypad())
                goto skip;
        }
    }
skip:
    font_set_height(12);
}

static void theme_scroll(void)
{
    scroll_text_28(theme_text, THEME_LINES, 1);
}

/* ================= 取名(既定決策:清單+英文輸入) ================= */

/* 名字清單:8 預設名 + 英文/中文输入,2 欄 × 5 行。回傳 1=已寫 man_name */
static uint8_t name_ui(void)
{
    uint8_t cur = 0, k, x, y;

    for (;;) {
        fb_clear();
        font_draw_text(4, 2, s_name);
        for (k = 0; k < 8; k++)
            font_draw_text((k < 5) ? 20 : 92,
                           (uint8_t)(16 + (k % 5) * 13), name_tbl[k]);
        font_draw_text(92, 16 + 3 * 13, s_en);      /* 第 9 項 */
        font_draw_text(92, 16 + 4 * 13, s_cn);      /* 第 10 項(拼音) */

        for (;;) {
            x = (cur < 5) ? 12 : 84;
            y = (uint8_t)(16 + (cur % 5) * 13);
            cursor_show(x, y);
            k = wait_key();
            cursor_hide(x, y);

            if (k == K_UP) {
                cur = (cur == 0) ? 9 : (cur == 5) ? 4 : cur - 1;
            } else if (k == K_DOWN) {
                cur = (cur == 9) ? 0 : (cur == 4) ? 5 : cur + 1;
            } else if (k == K_LEFT || k == K_RIGHT) {
                cur = (cur < 5) ? (uint8_t)(cur + 5) : (uint8_t)(cur - 5);
            } else if (k == K_ESC) {
                return 0;
            } else if (k == K_CR) {
                if (cur < 8) {
                    memset(hero.man_name, 0, sizeof hero.man_name);
                    /* 按實長拷(西门吹雪 8B,定長 6 截尾=2026-07-18 bug) */
                    strcpy((char *)hero.man_name, (const char *)name_tbl[cur]);
                    return 1;
                }
                if (cur == 9) {             /* 中文输入(拼音,bank 39) */
                    if (input_pinyin())
                        return 1;
                }
                break;                      /* 取消→重繪清單;8=英文 */
            }
        }

        /* ---- 英文字母格(A=選字 B=刪/退 START=完成) ---- */
        if (cur == 8) {
            static const uint8_t base[4] = { 'A', 'N', 'a', 'n' };
            uint8_t buf[9];
            uint8_t len = 0, row = 0, col = 0, i;

            fb_clear();
            font_draw_text(4, 2, s_name);
            for (row = 0; row < 4; row++)
                for (col = 0; col < 13; col++)
                    font_draw_ascii((uint8_t)(6 + col * 12),
                                    (uint8_t)(24 + row * 14),
                                    (uint8_t)(base[row] + col));
            row = 0;
            col = 0;
            for (;;) {
                /* 名字欄:已輸入字母 + '_' 佔位 */
                fb_fill_rect(48, 2, 64, 13, 0);
                for (i = 0; i < 8; i++)
                    font_draw_ascii((uint8_t)(48 + i * 8), 2,
                                    (i < len) ? buf[i] : (uint8_t)'_');
                x = (uint8_t)(5 + col * 12);
                y = (uint8_t)(24 + row * 14);
                ui_invert(x, y, 10, 13);
                fb_flush();
                k = wait_key();
                ui_invert(x, y, 10, 13);

                if (k == K_UP)
                    row = row ? row - 1 : 3;
                else if (k == K_DOWN)
                    row = (row == 3) ? 0 : row + 1;
                else if (k == K_LEFT)
                    col = col ? col - 1 : 12;
                else if (k == K_RIGHT)
                    col = (col == 12) ? 0 : col + 1;
                else if (k == K_CR) {
                    if (len < 8)
                        buf[len++] = (uint8_t)(base[row] + col);
                } else if (k == K_ESC) {
                    if (len)
                        len--;
                    else
                        break;              /* 空欄 B → 回清單 */
                } else if (k == K_F1 && len) {
                    memset(hero.man_name, 0, sizeof hero.man_name);
                    memcpy(hero.man_name, buf, len);
                    return 1;
                }
            }
        }
    }
}

/* ================= 屬性調配(input.s adjust_attr) ================= */

/* 4 屬性(膂力/敏捷/悟性/根骨)重分配:RIGHT 取 1 點入余點,LEFT 放回;
 * 界 10..30;CR 需余點=0;ESC 取消。按鍵語義照原版。回傳 1=完成 */
static uint8_t adjust_attr(void)
{
    uint8_t *attr = &hero.attr_str;
    uint8_t spare = 0, ptr = 0, i, k, v;

    memset(attr, 20, 6);                    /* 原版 move #20,attr_str,#6 */

    fb_clear();
    ui_hline(3, 157, 2);                    /* 等效 init_attr 雙框 */
    ui_hline(3, 157, 77);
    ui_vline(3, 2, 77);
    ui_vline(157, 2, 77);
    ui_hline(5, 155, 4);
    ui_hline(5, 155, 75);
    ui_vline(5, 4, 75);
    ui_vline(155, 4, 75);
    for (i = 0; i < 4; i++) {
        font_draw_text(24, (uint8_t)(9 + i * 15), attr_lbl[i]);
        draw2(84, (uint8_t)(9 + i * 15), attr[i]);
    }
    font_draw_text(112, 24, s_left);        /* 余点 */
    draw2(120, 39, spare);

    for (;;) {
        cursor_show(12, (uint8_t)(9 + ptr * 15));
        k = wait_key();
        cursor_hide(12, (uint8_t)(9 + ptr * 15));

        if (k == K_UP) {                    /* 原版 1..4 迴繞 */
            ptr = ptr ? ptr - 1 : 3;
        } else if (k == K_DOWN) {
            ptr = (ptr == 3) ? 0 : ptr + 1;
        } else if (k == K_RIGHT) {          /* 原版 right_func:attr-- */
            v = attr[ptr];
            if (v != 10) {
                attr[ptr] = v - 1;
                spare++;
                draw2(84, (uint8_t)(9 + ptr * 15), attr[ptr]);
                draw2(120, 39, spare);
            }
        } else if (k == K_LEFT) {           /* 原版 left_func:attr++ */
            if (spare && attr[ptr] != 30) {
                attr[ptr]++;
                spare--;
                draw2(84, (uint8_t)(9 + ptr * 15), attr[ptr]);
                draw2(120, 39, spare);
            }
        } else if (k == K_CR) {             /* 原版 enter_func:余點須 0 */
            if (spare == 0)
                return 1;
        } else if (k == K_ESC) {            /* 原版 esc_func */
            return 0;
        }
    }
}

/* 等效 input_new(密碼段移除):取名 → 調屬性 → 性別/相貌/緣分 */
static uint8_t input_new(void)
{
    uint8_t r;

    if (!name_ui())
        return 0;
    if (!adjust_attr())
        return 0;

    hero.man_gender = (hero.attr_str >= 16) ? 0 : 1;

    /* 原版 adc #30 後 sbc(進位=0)多減 1:per = rand(21)+29-膂力 */
    r = (uint8_t)random_it(21);
    hero.attr_per = (uint8_t)(r + 30 - 1 - hero.attr_str);
    r = (uint8_t)random_it(21);
    hero.attr_kar = (uint8_t)(r + 10);

    memcpy(&hero.man_str, &hero.attr_str, 6);
    return 1;
}

/* ================= 標題畫面(GBC 新增,exit_game 歸宿) ================= */

/* slot 選擇:newmode=1 兩格皆可選;=0 只可選有檔格。0xFF=返回 */
static uint8_t slot_pick(uint8_t newmode, uint8_t have0, uint8_t have1,
                         const uint8_t *nm0, const uint8_t *nm1)
{
    uint8_t cur, k, y;

    fb_clear();
    font_draw_text(24, 12, s_slot1);
    font_draw_text(68, 12, have0 ? nm0 : s_empty);
    font_draw_text(24, 30, s_slot2);
    font_draw_text(68, 30, have1 ? nm1 : s_empty);

    cur = (!newmode && !have0) ? 1 : 0;
    for (;;) {
        y = cur ? 30 : 12;
        cursor_show(14, y);
        k = wait_key();
        cursor_hide(14, y);

        if (k == K_UP || k == K_DOWN) {
            uint8_t other = cur ^ 1;
            if (newmode || (other ? have1 : have0))
                cur = other;
        } else if (k == K_CR) {
            return cur;
        } else if (k == K_ESC) {
            return 0xFF;
        }
    }
}

/* 覆蓋確認(佔用格重新開始時) */
static uint8_t confirm_cover(void)
{
    uint8_t k;

    fb_fill_rect(0, 46, 160, 34, 0);
    ui_hline(10, 150, 47);
    font_draw_text(46, 50, s_cover);
    font_draw_text(24, 64, s_ok);
    font_draw_text(96, 64, s_no);
    fb_flush();
    for (;;) {
        k = wait_key();
        if (k == K_CR)
            return 1;
        if (k == K_ESC)
            return 0;
    }
}

/* 標題:繼續游戲/重新開始(無檔時只有 開始游戲)。
 * 回傳 0=繼續(active_slot 已定) 1=新遊戲(active_slot 已定) */
static uint8_t title_screen(void)
{
    uint8_t nm0[10], nm1[10];
    uint8_t have0, have1, any, n, cur, k, y, s;

    nm0[9] = 0;
    nm1[9] = 0;
    for (;;) {
        have0 = save_probe(0, nm0);
        have1 = save_probe(1, nm1);
        any = have0 | have1;

        fb_clear();
        font_set_height(16);
        font_draw_text(32, 14, s_gmud);
        font_draw_text(64, 14, s_title);
        font_set_height(12);
        if (any) {
            font_draw_text(56, 52, s_cont);
            font_draw_text(56, 66, s_restart);
            n = 2;
        } else {
            font_draw_text(56, 58, s_start);
            n = 1;
        }

        cur = 0;
        for (;;) {
            y = any ? (cur ? 66 : 52) : 58;
            cursor_show(46, y);
            k = wait_key();
            cursor_hide(46, y);
            if ((k == K_UP || k == K_DOWN) && n == 2)
                cur ^= 1;
            else if (k == K_CR)
                break;
        }

        if (any && cur == 0) {              /* 繼續 */
            if (have0 && have1) {
                s = slot_pick(0, have0, have1, nm0, nm1);
                if (s == 0xFF)
                    continue;
            } else {
                s = have0 ? 0 : 1;
            }
            active_slot = s;
            return 0;
        }

        /* 新遊戲/重新開始 */
        s = slot_pick(1, have0, have1, nm0, nm1);
        if (s == 0xFF)
            continue;
        if (s ? have1 : have0) {
            if (!confirm_cover())
                continue;
        }
        active_slot = s;
        return 1;
    }
}

/* ================= 開機主流程(game.s game:/create_game) ================= */

void game_boot(void) BANKED
{
    quit_flag = 0;
    busy_flag = 0x80;           /* 標題/建角期間 heart_beat 只走鐘 */
    font_set_height(12);

    for (;;) {
        if (title_screen() == 0) {
            /* 等效 game: 載檔支(密碼已移除) */
            task_bind_slot();
            if (!save_load())
                continue;       /* 校驗失敗(已抹)→ 回標題 */
            memcpy(npc_stat_buf, hero.npc_stat_buf, 32);
            break;
        }

        /* 等效 create_game:捲軸 → new_game → input_new → set_attr
         * → clear_apply → save_file;取消 → exit_game(=回標題) */
        theme_scroll();
        new_game();
        if (!input_new())
            continue;
        set_attr();
        clear_apply();
        if (yobdc_mode()) {             /* 官方秘技(task.h 說明) */
            hero.man_exp = 999999UL;
            hero.man_pot = 0xFFFF;      /* 潛能欄原版佈局 16 位,拉滿 */
        }
        memcpy(npc_stat_buf, hero.npc_stat_buf, 32);
        task_bind_new();        /* 任務工作區清零並綁定本 slot */
        save_store();
        break;
    }

    /* 等效 enter_gmud */
    save_time = hero.mud_age + SAVE_INTERVAL;
    setup_attr();
    setup_maxhp_cmd();
    check_player();
    ghost_task_restore();        /* 通緝犯位置按 slot 恢復/舊檔留待補發 */
    busy_flag = 0;
    fb_clear();
    fb_flush();
    heart_beat_reset();         /* 遊戲時鐘錨定(等效 timetick 初始化) */
}

/* ================= DMG 保護畫面 ================= */
/* 引擎依賴 CGB 硬體(GDMA/雙 VRAM bank/雙倍速),DMG/MGB 無法運行。
 * 卡帶頭維持 CGB 專用,但 DMG 實機仍會執行 ROM(硬體不看 0x143),
 * 故照業界慣例顯示提示畫面後停機。全程 LCD 關閉直寫 VRAM,
 * 不經 fb_flush(GDMA 為 CGB 專有)。 */
static const uint8_t s_dmg1[] =                 /* 请使用彩色机型运行 */
    "\xC7\xEB\xCA\xB9\xD3\xC3\xB2\xCA\xC9\xAB"
    "\xBB\xFA\xD0\xCD\xD4\xCB\xD0\xD0";
static const uint8_t s_dmg2[] = "GAME BOY COLOR ONLY";

void dmg_guard(void) BANKED
{
    uint8_t *v = (uint8_t *)0x8000;
    uint8_t *m = (uint8_t *)0x9800;
    uint16_t i;
    uint8_t ty, tx, r, b;

    DISPLAY_OFF;
    LCDC_REG |= LCDCF_BG8000;
    BGP_REG = 0xE4;

    fb_clear();
    font_set_height(12);
    font_draw_text(26, 28, s_dmg1);             /* 9 字 ×12px 置中 */
    font_draw_text(23, 46, s_dmg2);

    /* fb 上 10 個 tile 行 → 1bpp 直转 2bpp 順序灌入 tile 0-199 */
    for (ty = 0; ty < 10; ty++)
        for (tx = 0; tx < 20; tx++)
            for (r = 0; r < 8; r++) {
                b = fb[((uint16_t)ty * 8 + r) * FB_STRIDE + tx];
                *v++ = b;
                *v++ = b;
            }
    for (i = 200 * 16; i < 256 * 16; i++)       /* tile 200-255 清空 */
        *v++ = 0;

    for (ty = 0; ty < 18; ty++)                 /* map:上 10 行順序 id */
        for (tx = 0; tx < 32; tx++)
            m[(uint16_t)ty * 32 + tx] =
                (ty < 10 && tx < 20) ? (uint8_t)(ty * 20 + tx) : 255;

    SHOW_BKG;
    DISPLAY_ON;
    for (;;)
        vsync();
}
