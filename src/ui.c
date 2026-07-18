/* ui.c - UI 基礎例程(對照見 ui.h)。代碼在 bank 25。 */
#pragma bank 25
#include <gb/gb.h>
#include <string.h>
#include "ui.h"
#include "fb.h"
#include "font.h"
#include "text.h"
#include "menutext.h"
#include "game.h"
#include "save.h"
#include "skill.h"

/* ---- 按鍵:等效 system.s wait_key + key 回推 ---- */
static uint8_t pushed;

void push_key(uint8_t k) BANKED
{
    pushed = k;
}

static uint8_t map_pad(uint8_t j)
{
    if (j & J_A)      return K_CR;
    if (j & J_B)      return K_ESC;
    if (j & J_START)  return K_F1;
    if (j & J_UP)     return K_UP;
    if (j & J_DOWN)   return K_DOWN;
    if (j & J_LEFT)   return K_LEFT;
    if (j & J_RIGHT)  return K_RIGHT;
    return K_NONE;
}

uint8_t wait_key(void) BANKED
{
    uint8_t k;

    if (pushed) {
        k = pushed;
        pushed = 0;
        return k;
    }
    waitpadup();
    for (;;) {
        vsync();
        heart_beat();               /* 等效原版 wkey 迴圈跑 sys_refresh */
        k = map_pad(joypad());
        if (k)
            return k;
    }
}

/* ---- 等效 system.s sys_refresh(遊戲時鐘 + 回復) ---- */
#define TICK_TIME 15                /* 原版 gmud.h:15 秒一跳 */

static uint16_t hb_anchor;
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

    while ((uint16_t)(sys_time - hb_anchor) >= 60) {
        hb_anchor += 60;

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

/* ---- 線/反白 ---- */
void ui_hline(uint8_t x0, uint8_t x1, uint8_t y) BANKED
{
    uint8_t x;
    for (x = x0; x <= x1; x++)
        fb[(uint16_t)y * FB_STRIDE + (x >> 3)] |= 0x80 >> (x & 7);
    fb_mark_dirty(y, 1);
}

void ui_vline(uint8_t x, uint8_t y0, uint8_t y1) BANKED
{
    uint8_t y;
    for (y = y0; y <= y1; y++)
        fb[(uint16_t)y * FB_STRIDE + (x >> 3)] |= 0x80 >> (x & 7);
    fb_mark_dirty(y0, y1 - y0 + 1);
}

void ui_invert(uint8_t x, uint8_t y, uint8_t w, uint8_t h) BANKED
{
    uint8_t r, i;
    uint8_t *row;

    for (r = 0; r < h; r++) {
        row = fb + (uint16_t)(y + r) * FB_STRIDE;
        for (i = 0; i < w; i++) {
            uint8_t px = x + i;
            row[px >> 3] ^= 0x80 >> (px & 7);
        }
    }
    fb_mark_dirty(y, h);
}

void clear_nline2(uint8_t y, uint8_t n) BANKED
{
    fb_fill_rect(0, y, 160, n, 0);
}

/* ---- 逐行輸出:等效 stringx.s show_one_line/show_string ----
 * 行寬 26 半形(原版 CPR26);內容到 0/0xFF 止,超寬留給下一行續 */
const uint8_t *ss_ptr;

static uint8_t show_one_line_to(uint8_t xh, uint8_t xh1, uint8_t y)
{
    uint8_t n = 0;
    uint8_t c;

    for (;;) {
        c = ss_ptr[n];
        if (c == 0 || c == 0xFF) {          /* 行終:跳過終結符 */
            ss_ptr += n + 1;
            return n;
        }
        if (c & 0x80) {                     /* 全形 */
            if (xh + 2 > xh1)
                break;
            font_draw_cjk(xh * 6, y, ((uint16_t)c << 8) | ss_ptr[n + 1]);
            n += 2;
            xh += 2;
        } else {
            if (xh + 1 > xh1)
                break;
            font_draw_ascii(xh * 6, y, c);
            n += 1;
            xh += 1;
        }
    }
    ss_ptr += n;                            /* 超寬:餘下續於下一行 */
    return n;
}

uint8_t show_one_line(uint8_t xh, uint8_t y) BANKED
{
    return show_one_line_to(xh, 26, y);
}

static uint8_t show_string_to(uint8_t nlines, uint8_t xh,
                              uint8_t xh1, uint8_t y)
{
    uint8_t n;

    while (nlines--) {
        n = show_one_line_to(xh, xh1, y);
        (void)n;
        /* 行終後下一字節為 0 = 雙 0 全文終。舊判定(n==0 且 *ss_ptr==0)
         * 差一位:show_one_line 已跳過行終符,空行會再吃掉第二個 0,
         * 導致要「三個 0」才停 → OutBuf 雙 0 之後的殘留字節(存檔
         * 二進制/舊訊息)被當第 3 行渲染(2026-07-10 BGB 實測)。 */
        if (*ss_ptr == 0)
            return 0;
        y += 12;
    }
    return *ss_ptr != 0;
}

uint8_t show_string(uint8_t nlines, uint8_t xh, uint8_t y) BANKED
{
    return show_string_to(nlines, xh, 26, y);
}

/* ---- 訊息框(等效 message_box:框+OutBuf) ---- */
static void draw_box(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    fb_fill_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, 0);
    ui_hline(x0, x1, y0);
    ui_hline(x0, x1, y1);
    ui_vline(x0, y0, y1);
    ui_vline(x1, y0, y1);
}

/* 確認鍵在 GBC 上固定為 A/B。提示放在框外的獨立列，避免長訊息
 * 佔滿原本對話框時覆蓋最後一行；目前所有確認框都在 y=75 以前。 */
static const uint8_t confirm_hint[] = {
    '[','A',' ',0xCA,0xC7,']',' ','[','B',' ',0xB7,0xF1,']',0
};

static uint8_t confirm_has_hint(void)
{
    uint16_t i;

    /* 少數舊訊息（例如自殺確認）已自帶 [A 是] [B 否]；不要重複。 */
    for (i = 0; i + 2 < sizeof OutBuf; i++) {
        if (OutBuf[i] == 0 && OutBuf[i + 1] == 0)
            return 0;
        if (OutBuf[i] == '[' && OutBuf[i + 1] == 'A'
            && OutBuf[i + 2] == ' ')
            return 1;
    }
    return 0;
}

static void draw_confirm_hint(uint8_t x0, uint8_t x1, uint8_t y1)
{
    if (confirm_has_hint())
        return;
    font_draw_text((uint8_t)((x0 + x1 + 1 - 78) >> 1), y1 - 13,
                   confirm_hint);
}

void message_box(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) BANKED
{
    draw_box(x0, y0, x1, y1);
    ss_ptr = OutBuf;
    show_string_to((uint8_t)(y1 - y0) / 12, (x0 + 6) / 6,
                   x1 / 6, y0 + 3);
    fb_flush();
    wait_key();
}

uint8_t confirm_box(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1,
                    const uint8_t *msg) BANKED
{
    uint8_t k, reserve;

    if (msg)
        format_string(msg);
    reserve = confirm_has_hint() ? 0 : 14;
    draw_box(x0, y0, x1, y1);
    ss_ptr = OutBuf;
    show_string_to((uint8_t)(y1 - y0 - reserve) / 12, (x0 + 6) / 6,
                   x1 / 6, y0 + 3);
    draw_confirm_hint(x0, x1, y1);
    fb_flush();
    for (;;) {
        k = wait_key();
        if (k == K_CR)
            return 1;
        if (k == K_ESC)
            return 0;
    }
}

/* ---- 數字輸入(inputx.s;數字鍵缺席 → 上下 ±1 連發) ---- */
static void digit_show(uint8_t x, uint8_t y, uint16_t v)
{
    uint8_t b[6];
    uint8_t i = 5;

    b[5] = 0;
    do {
        b[--i] = '0' + (uint8_t)(v % 10);
        v /= 10;
    } while (v);
    font_draw_text_replace(x + 3, y + 2, b + i);
    fb_flush();
}

uint16_t input_digit(uint8_t x, uint8_t y, uint16_t val, uint16_t max) BANKED
{
    uint16_t cur = val;
    uint8_t j, hold = 0;

    draw_box(x, y, x + 36, y + 16);
    digit_show(x, y, cur);
    waitpadup();
    for (;;) {
        vsync();
        j = joypad();
        if (j & J_A) {
            waitpadup();
            return cur;
        }
        if (j & J_B) {
            waitpadup();
            return val;                 /* ESC:還原 */
        }
        if (j & (J_UP | J_DOWN | J_LEFT | J_RIGHT)) {
            if (hold == 0 || (hold > 15 && (hold & 3) == 0)) {
                if ((j & J_UP) && cur)
                    cur--;              /* 原版 UP=減 DOWN=加 */
                if ((j & J_DOWN) && cur < max)
                    cur++;
                if (j & J_LEFT)         /* ±10(用戶核可,補數字鍵) */
                    cur = (cur >= 10) ? cur - 10 : 0;
                if (j & J_RIGHT)
                    cur = (cur + 10 < max) ? cur + 10 : max;
                digit_show(x, y, cur);
            }
            if (hold < 255)
                hold++;
        } else {
            hold = 0;
        }
    }
}

/* ---- 等效 lee1.s show_text ---- */
/* 訊息起點:FP 條止於 y≈49;留 5px 間距。3 行溢出到擴展帶(y=80+) */
#define MSG_Y0 (FB_ROWS - 3 * 13)

static void show_text_body(void)
{
    clear_nline2(MSG_Y0, 3 * 13);
    ss_ptr = OutBuf;
    show_string(3, 0, MSG_Y0);
    fb_flush();
}

static void text_wait(uint8_t frames)
{
    uint8_t t;

    for (t = 0; t < frames; t++)
        vsync();
}

/* 戰鬥訊息(show_fight_msg 專用):ADV/BSC 等待減半(x2,
 * 2026-07-17 用戶定版;原版 110 幀) */
void show_text_out(void) BANKED
{
    show_text_body();
    text_wait(110 >> fight_shift());
}

/* 打坐/療傷等服務訊息(serve.c 專用):ADV/BSC 等待 x4 */
void show_text(const uint8_t *msg) BANKED
{
    format_string(msg);
    show_text_body();
    text_wait(110 >> disp_shift());
}

/* ---- 等效 aux.s show_process(重建) ---- */
uint16_t binbuf, bcdbuf;

static void put_num(uint8_t *p, uint16_t v)
{
    uint8_t b[5];
    uint8_t i = 5;

    do {
        b[--i] = '0' + (uint8_t)(v % 10);
        v /= 10;
    } while (v);
    while (i < 5)
        *p++ = b[i++];
    *p = 0;
}

void show_process(uint8_t x, uint8_t y, void (*set_line)(void),
                  void (*set_digit)(void), uint8_t interval,
                  uint8_t (*program)(void)) BANKED
{
    uint8_t buf[12];
    uint8_t w, t, j;
    uint8_t prev_len = 0;   /* 上輪數字串長:縮短時補空格蓋殘字 */
    uint8_t *p;

    for (;;) {
        /* 進度條:binbuf/bcdbuf → 64px */
        set_line();
        w = (bcdbuf && binbuf < bcdbuf)
                ? (uint8_t)(((uint32_t)binbuf << 6) / bcdbuf) : 64;
        draw_box(x, y, x + 65, y + 8);
        if (w)
            fb_fill_rect(x + 1, y + 2, w, 5, 1);

        /* 數字 cur/max。replace 模式只蓋自己畫的格:若比上輪短
         * (打坐 200/100 → 4/101),尾端殘留舊字(顯示 4/10100),
         * 故補空格清到上輪長度。 */
        set_digit();
        put_num(buf, binbuf);
        p = buf + strlen((char *)buf);
        *p++ = '/';
        put_num(p, bcdbuf);
        w = (uint8_t)strlen((char *)buf);
        t = w;
        while (t < prev_len && t < sizeof(buf) - 1)
            buf[t++] = ' ';
        buf[t] = 0;
        prev_len = w;
        font_draw_text_replace(x + 68, y, buf);
        fb_flush();

        if (program())
            return;

        for (t = 0; t < interval; t++) {    /* 等間隔,按鍵即停 */
            vsync();
            heart_beat();                   /* 打坐/練功期間時鐘照走 */
            j = joypad();
            if (j & (J_A | J_B | J_START)) {
                waitpadup();
                return;
            }
        }
    }
}

uint8_t percent(uint16_t a, uint16_t b) BANKED
{
    if (!b)
        return 0;
    return (uint8_t)(((uint32_t)a * 100) / b);
}

/* 加力提示模板備製(bank26 的 serve.c 不能直讀 25 端資料) */
void mt_tpl_jiali(uint8_t *dst) BANKED
{
    memcpy(dst, mt_jiali_flow_msg, sizeof mt_jiali_flow_msg);
    fix_mt_jiali_flow_msg(dst);
}

/* 等效 math.s random_it:mud_seed×65531+1 取模;
 * 熵源 getms/random 暫存器 → DIV_REG(硬體替代,同 save.c 先例) */
uint16_t mud_seed;

uint16_t random_it(uint16_t range) BANKED
{
    uint32_t v;

    if (!range)
        return 0;
    mud_seed = (mud_seed & 0xFF00)
             | (uint8_t)((uint8_t)(DIV_REG << 1) + DIV_REG
                         + (uint8_t)mud_seed);
    v = (uint32_t)mud_seed * 65531UL + 1;
    mud_seed = (uint16_t)v;
    return (uint16_t)(v % range);
}

/* 等效 math.s perform_random_it:32 位除數上的均勻隨機(AP+DP 命中判定)。
 * 兩次推進 mud_seed 取 32 位,對 divisor 取模。熵源同 random_it。 */
uint32_t random_it32(uint32_t divisor) BANKED
{
    uint32_t hi, lo;

    if (!divisor)
        return 0;
    mud_seed = (mud_seed & 0xFF00)
             | (uint8_t)((uint8_t)(DIV_REG << 1) + DIV_REG + (uint8_t)mud_seed);
    hi = (uint32_t)mud_seed * 65531UL + 1;
    mud_seed = (uint16_t)hi;
    lo = (uint32_t)mud_seed * 65531UL + 1;
    mud_seed = (uint16_t)lo;
    return (((uint32_t)(uint16_t)hi << 16) | (uint16_t)lo) % divisor;
}
