/* cheat.c - 作弊選單(原版 cheat.s,yobdc 專用)
 *
 * 獨立 bank 避免 bank 25 溢出。不用 pop_menu(處理器指針必須在
 * pop_menu 同 bank);改為自行繪製選單+按鍵迴圈,所有 UI 函數
 * (wait_key/show_one_line/clear_nline2/fb_flush/fb_fill_rect)
 * 皆 BANKED 可跨 bank 呼叫。
 *
 * 注意:static const 字串在 bank 29 ROM,show_one_line(bank 25)
 * 經 ss_ptr 讀不到——必須先拷到 WRAM(show_rom_line 助手)。
 * gd_find_name 回傳 WRAM 池拷貝,OutBuf 在 SRAM,均可直用。
 */
#pragma bank 25
#include <gb/gb.h>
#include <string.h>
#include "game.h"
#include "save.h"
#include "text.h"
#include "ui.h"
#include "fb.h"
#include "skill.h"
#include "goods.h"
#include "gamedata.h"

/* ---- GB2312 選單標籤(全為 2 漢字 + 0xFF = 5B)---- */
static const uint8_t ct_money[]  = { 0xBD,0xF0,0xC7,0xAE,0xFF };
static const uint8_t ct_exp[]    = { 0xBE,0xAD,0xD1,0xE9,0xFF };
static const uint8_t ct_pot[]    = { 0xC7,0xB1,0xC4,0xDC,0xFF };
static const uint8_t ct_fp[]     = { 0xC4,0xDA,0xC1,0xA6,0xFF };
static const uint8_t ct_daode[]  = { 0xB5,0xC0,0xB5,0xC2,0xFF };
static const uint8_t ct_sikill[] = { 0xC9,0xB1,0xCB,0xC0,0xFF };
static const uint8_t ct_gkill[]  = { 0xD7,0xB7,0xC9,0xB1,0xFF };
static const uint8_t ct_nkill[]  = { 0xBF,0xB3,0xC9,0xB1,0xFF };
static const uint8_t ct_gender[] = { 0xD0,0xD4,0xB1,0xF0,0xFF };
static const uint8_t ct_per[]    = { 0xC8,0xDD,0xC3,0xB2,0xFF };
static const uint8_t ct_age[]    = { 0xC4,0xEA,0xC1,0xE4,0xFF };
static const uint8_t ct_time[]   = { 0xCA,0xB1,0xBC,0xE4,0xFF };
static const uint8_t ct_dance[]  = { 0xCC,0xF8,0xCE,0xE8,0xFF };
static const uint8_t ct_ball[]   = { 0xCD,0xB6,0xC0,0xBA,0xFF };

static const uint8_t *const ct_labels[14] = {
    ct_money, ct_exp, ct_pot, ct_fp, ct_daode, ct_sikill, ct_gkill,
    ct_nkill, ct_gender, ct_per, ct_age, ct_time, ct_dance, ct_ball,
};

static const uint8_t ct_view[]  = { 0xB2,0xE9,0xBF,0xB4,0xFF };  /* 查看 */
static const uint8_t ct_skill[] = { 0xBC,0xBC,0xC4,0xDC,0xFF };  /* 技能 */

static const uint8_t cheat_sz[14] = {
    4, 4, 2, 2, 1, 1, 1, 1, 1, 1, 4, 2, 2, 2
};

static uint8_t *cheat_field(uint8_t i)
{
    switch (i) {
    case 0:  return (uint8_t *)&hero.man_money;
    case 1:  return (uint8_t *)&hero.man_exp;
    case 2:  return (uint8_t *)&hero.man_pot;
    case 3:  return (uint8_t *)&hero.man_maxfp;
    case 4:  return &hero.man_daode;
    case 5:  return &hero.si_kill;
    case 6:  return &hero.guan_kill;
    case 7:  return &hero.npc_kill;
    case 8:  return &hero.man_gender;
    case 9:  return &hero.man_per;
    case 10: return (uint8_t *)&hero.mud_age;
    case 11: return (uint8_t *)&hero.game_hour;
    case 12: return (uint8_t *)&hero.top_dance;
    default: return (uint8_t *)&hero.top_ball;
    }
}

/* ---- ROM→WRAM 拷貝+顯示(bank 29 ROM 在 show_one_line 切 bank 後不可讀)---- */
static void show_rom_line(const uint8_t *s, uint8_t xh, uint8_t y)
{
    uint8_t b[5];
    b[0]=s[0]; b[1]=s[1]; b[2]=s[2]; b[3]=s[3]; b[4]=s[4];
    ss_ptr = b;
    show_one_line(xh, y);
}

/* ---- 游標(3×5 三角,等效 menu.c arrow_dot) ---- */
#define CUR_X   12
#define CUR_W    3
#define CUR_H    5

static void draw_cursor(uint8_t y, uint8_t on)
{
    fb_fill_rect(CUR_X, y + 4, CUR_W, CUR_H, on);
}

/* ---- 數值編輯迴圈(→+1 ←-1 ↑+10% ↓-10% B=返回)---- */
static void edit_stat(uint8_t idx)
{
    uint8_t *p = cheat_field(idx);
    uint8_t sz = cheat_sz[idx];
    uint8_t msg[8], k;
    uint32_t v, d, mx;

    if (sz == 1)      mx = 0xFFUL;
    else if (sz == 2) mx = 0xFFFFUL;
    else              mx = 0xFFFFFFFFUL;

    for (;;) {
        msg[0] = sz;
        msg[1] = (uint8_t)((uint16_t)p);
        msg[2] = (uint8_t)((uint16_t)p >> 8);
        msg[3] = 0; msg[4] = 0; msg[5] = 0;
        clear_nline2(116, 13);
        format_string(msg);
        ss_ptr = OutBuf;
        show_one_line(1, 116);
        fb_flush();
        k = wait_key_rep();
        if (k == K_ESC) break;

        v = p[0];
        if (sz >= 2) v |= (uint16_t)p[1] << 8;
        if (sz >= 4) v |= (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;

        if (k == K_RIGHT) {
            if (v < mx) v++;
        } else if (k == K_LEFT) {
            if (v > 0) v--;
        } else if (k == K_UP) {
            d = v / 10;
            if (d < 10) d = 10;
            v = (mx - v >= d) ? v + d : mx;
        } else if (k == K_DOWN) {
            d = v / 10;
            if (d < 10) d = 10;
            v = (v >= d) ? v - d : 0;
        }

        p[0] = (uint8_t)v;
        if (sz >= 2) p[1] = (uint8_t)(v >> 8);
        if (sz >= 4) { p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
    }
    clear_nline2(116, 13);
    fb_flush();
}

/* ---- 查看:14 項捲動列表 + 數值編輯 ---- */
#define VROWS  7
#define MENU_X 3           /* xh 位置(半形字=6px 起) */
#define MENU_Y 4           /* 首行像素 y */
#define LINE_H 13

static void draw_stat_list(uint8_t top, uint8_t sel)
{
    uint8_t i;

    clear_nline2(MENU_Y, VROWS * LINE_H);
    for (i = 0; i < VROWS && top + i < 14; i++) {
        if (top + i == sel)
            draw_cursor(MENU_Y + i * LINE_H, 1);
        show_rom_line(ct_labels[top + i], MENU_X, MENU_Y + i * LINE_H);
    }
    fb_flush();
}

static void show_values(void)
{
    uint8_t sel = 0, top = 0, k;

    draw_stat_list(top, sel);
    for (;;) {
        k = wait_key_rep();
        if (k == K_ESC) break;
        if (k == K_CR) {
            edit_stat(sel);
            draw_stat_list(top, sel);
            continue;
        }
        if (k == K_UP && sel > 0) {
            sel--;
            if (sel < top) top = sel;
        }
        if (k == K_DOWN && sel < 13) {
            sel++;
            if (sel >= top + VROWS) top = sel - VROWS + 1;
        }
        draw_stat_list(top, sel);
    }
    clear_nline2(MENU_Y, VROWS * LINE_H);
    fb_flush();
}

/* ---- 技能:已學功夫列表,A 進入等級編輯(與查看同鍵) ---- */
static void cheat_skills(void)
{
    uint8_t n, i, sel, top, k, y, d;
    uint16_t lv;
    uint8_t *p;
    uint8_t ids[MAX_KF];
    uint8_t msg[6];

    n = 0;
    for (i = 0; i < hero.man_kfnum && i < MAX_KF; i++)
        ids[n++] = hero.man_kf[i * 4];
    if (n == 0) return;

    sel = 0; top = 0;
    for (;;) {
        clear_nline2(MENU_Y, VROWS * LINE_H);
        for (i = 0; i < VROWS && top + i < n; i++) {
            if (top + i == sel)
                draw_cursor(MENU_Y + i * LINE_H, 1);
            ss_ptr = gd_find_name(kf_name_tbl, ids[top + i]);
            show_one_line(MENU_X, MENU_Y + i * LINE_H);
        }
        y = find_kf(ids[sel]);
        if (y != 0xFF) {
            p = &hero.man_kf[y + 1];
            msg[0] = 1;
            msg[1] = (uint8_t)((uint16_t)p);
            msg[2] = (uint8_t)((uint16_t)p >> 8);
            msg[3] = 0; msg[4] = 0; msg[5] = 0;
            clear_nline2(116, 13);
            format_string(msg);
            ss_ptr = OutBuf;
            show_one_line(1, 116);
        }
        fb_flush();
        k = wait_key_rep();
        if (k == K_ESC) break;
        if (k == K_CR) {
            y = find_kf(ids[sel]);
            if (y != 0xFF) {
                p = &hero.man_kf[y + 1];
                for (;;) {
                    msg[0] = 1;
                    msg[1] = (uint8_t)((uint16_t)p);
                    msg[2] = (uint8_t)((uint16_t)p >> 8);
                    msg[3] = 0; msg[4] = 0; msg[5] = 0;
                    clear_nline2(116, 13);
                    format_string(msg);
                    ss_ptr = OutBuf;
                    show_one_line(1, 116);
                    fb_flush();
                    k = wait_key_rep();
                    if (k == K_ESC) break;
                    lv = *p;
                    if (k == K_RIGHT) {
                        if (lv < 255) lv++;
                    } else if (k == K_LEFT) {
                        if (lv > 0) lv--;
                    } else if (k == K_UP) {
                        d = (uint8_t)(lv / 10);
                        if (d < 10) d = 10;
                        if (255 - lv >= d) lv += d;
                        else lv = 255;
                    } else if (k == K_DOWN) {
                        d = (uint8_t)(lv / 10);
                        if (d < 10) d = 10;
                        if (lv >= d) lv -= d;
                        else lv = 0;
                    }
                    *p = (uint8_t)lv;
                }
                clear_nline2(116, 13);
            }
        }
        if (k == K_UP && sel > 0) {
            sel--;
            if (sel < top) top = sel;
        }
        if (k == K_DOWN && sel + 1 < n) {
            sel++;
            if (sel >= top + VROWS) top = sel - VROWS + 1;
        }
    }
    clear_nline2(MENU_Y, VROWS * LINE_H);
    clear_nline2(116, 13);
    fb_flush();
}

/* ---- 作弊頂層:查看 / 技能 ---- */
void cheat_main(void) BANKED
{
    uint8_t sel = 0, k;

    for (;;) {
        clear_nline2(MENU_Y, 2 * LINE_H);
        draw_cursor(MENU_Y + sel * LINE_H, 1);
        show_rom_line(ct_view, MENU_X, MENU_Y);
        show_rom_line(ct_skill, MENU_X, MENU_Y + LINE_H);
        fb_flush();

        k = wait_key_rep();
        if (k == K_ESC) break;
        if (k == K_UP && sel > 0) sel--;
        if (k == K_DOWN && sel < 1) sel++;
        if (k == K_CR) {
            if (sel == 0) show_values();
            else          cheat_skills();
        }
    }
    clear_nline2(MENU_Y, 2 * LINE_H);
}
