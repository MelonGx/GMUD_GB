/* menu.c - 通用選單引擎(對照表見 menu.h;游標格點陣抄自原版 menu.s)
 *
 * 代碼在 bank 25(bank0 已滿)。約束:選單處理器/顯示程序指針只能指向
 * HOME(bank0)或同 bank 25 的函數(引擎裸調,無跨 bank 跳板)。 */
#pragma bank 25
#include <gb/gb.h>
#include <string.h>
#include "menu.h"
#include "font.h"
#include "fb.h"
#include "ui.h"
#include "goods.h"

uint8_t menu_set;
uint8_t list_x0, list_y0, list_x1, list_y1;

/* ---- 12 行游標格點陣(原版 menu.s *_dot,8 位寬) ---- */
static const uint8_t arrow_dot[12] = {
    0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xF8, 0xF0, 0xE0, 0xC0, 0x00,
};
static const uint8_t box_full_dot[12] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0xFC, 0xFC, 0xFC, 0xFC, 0xFC, 0xFC, 0x00,
};
static const uint8_t box_empty_dot[12] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x84, 0x84, 0x84, 0x84, 0xFC, 0x00,
};
static const uint8_t radio_full_dot[12] = {
    0x00, 0x00, 0x00, 0x3C, 0x42, 0x99, 0xBD, 0xBD, 0x99, 0x42, 0x3C, 0x00,
};
static const uint8_t radio_empty_dot[12] = {
    0x00, 0x00, 0x00, 0x3C, 0x42, 0x81, 0x81, 0x81, 0x81, 0x42, 0x3C, 0x00,
};
static const uint8_t icon_full_dot[12] = {
    0x00, 0x00, 0xFC, 0xFC, 0xFC, 0xFC, 0xFC, 0xFC, 0x00, 0x00, 0x00, 0x00,
};
static const uint8_t icon_empty_dot[12] = {
    0x00, 0x00, 0xFC, 0x84, 0x84, 0x84, 0x84, 0xFC, 0x00, 0x00, 0x00, 0x00,
};
static const uint8_t check_full_dot[12] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0xFD, 0xFE, 0xFC, 0xFC, 0xFC, 0xFC, 0x00,
};
static const uint8_t check_empty_dot[12] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0xFD, 0x86, 0xC4, 0xAC, 0x94, 0xFC, 0x00,
};
static const uint8_t empty_dot[12] = { 0 };

/* 樣式 → 滿/空點陣(等效 full_pad_tbl/empty_pad_tbl) */
static const uint8_t *const full_pad_tbl[8] = {
    empty_dot, arrow_dot, box_full_dot, radio_full_dot,
    icon_full_dot, icon_full_dot, check_full_dot, check_full_dot,
};
static const uint8_t *const empty_pad_tbl[8] = {
    empty_dot, empty_dot, box_empty_dot, radio_empty_dot,
    icon_empty_dot, icon_empty_dot, check_empty_dot, check_empty_dot,
};

/* 12px 寬 pad 格(8 位圖形右移 2 置中,覆寫式;等效 put_patbuf) */
static void draw_pad(uint8_t x, uint8_t y, const uint8_t *dot, uint8_t w)
{
    uint8_t r, k, px;

    for (r = 0; r < 12; r++) {
        for (k = 0; k < w; k++) {
            uint8_t on = 0;
            px = x + k;
            if (w == 12) {
                if (k >= 2 && k < 10)
                    on = dot[r] & (0x80 >> (k - 2));
            } else {
                on = dot[r] & (0x80 >> k);
            }
            if (on)
                fb[(uint16_t)(y + r) * FB_STRIDE + (px >> 3)] |=
                    0x80 >> (px & 7);
            else
                fb[(uint16_t)(y + r) * FB_STRIDE + (px >> 3)] &=
                    ~(0x80 >> (px & 7));
        }
    }
    fb_mark_dirty(y, 12);
}

/* ---- 引擎工作狀態 ---- */
static const cmenu_t *cm;
static uint8_t m_count, m_stride, m_rows, m_vis;
static uint8_t m_cur, m_start, m_check;

static uint8_t entry_val(uint8_t idx)
{
    return dmenu_buf[1 + (uint16_t)idx * m_stride];
}

static const uint8_t *item_name(uint8_t idx)
{
    if (cm->n)
        return cm->items[idx];
    return gd_find_name(cm->name_tbl, entry_val(idx) & 0x7F);
}

/* 選出 idx 項的游標點陣(等效 set_current_pad) */
static const uint8_t *pick_dot(uint8_t idx)
{
    uint8_t st = cm->style;

    if (st == MSTYLE_CHECK && idx != m_check)
        st = MSTYLE_BOX;                    /* 未啟用項退化為方格 */
    if (st == MSTYLE_CHECK1 && !(entry_val(idx) & 0x80))
        st = MSTYLE_BOX;                    /* 未裝備項退化為方格 */
    return (idx == m_cur) ? full_pad_tbl[st] : empty_pad_tbl[st];
}

/* 畫一項的文字(+數量+補白),回傳結束 x(等效 write_1_item+digit_pad) */
static uint8_t draw_item_text(uint8_t x, uint8_t y, uint8_t idx)
{
    uint8_t buf[24];
    const uint8_t *nm = item_name(idx);
    uint8_t w = 0, i = 0, c;

    while ((c = nm[i]) != 0xFF && c != 0 && i < 20) {
        buf[i++] = c;
        w++;
    }
    if (cm->flags & MF_DIGITS) {            /* 'x' + ≤3 位數量 */
        uint8_t v = dmenu_buf[2 + (uint16_t)idx * 2];
        uint8_t d[3];
        uint8_t k = 3;
        buf[i++] = 'x';
        w++;
        do {
            d[--k] = '0' + v % 10;
            v /= 10;
        } while (v);
        while (k < 3 && i < 22) {
            buf[i++] = d[k++];
            w++;
        }
    }
    while (w < cm->char_form && i < 23) {   /* 補白到 char_form */
        buf[i++] = ' ';
        w++;
    }
    buf[i] = 0;
    return font_draw_text_replace(x, y, buf);
}

/* ---- 整面重畫(等效 show_menu;cur=0xFF 時純顯示) ---- */
static void draw_menu(uint8_t x, uint8_t y)
{
    uint8_t idx, row, col, ix, iy;
    uint8_t xmax = x;

    if (cm->style == MSTYLE_ICON1) {
        /* 圖示列(8px 格,字節對齊)+ 左側當前項名 */
        uint8_t bx = x & 0xF8;
        for (idx = 0; idx < m_count; idx++)
            draw_pad(bx + (idx << 3), y, pick_dot(idx), 8);
        if (m_cur < m_count)
            draw_item_text((uint8_t)(x / 6 - cm->char_form - 2) * 6, y,
                           m_cur);
        fb_flush();
        return;
    }

    idx = m_start;
    iy = y;
    for (row = 0; row < m_vis && idx < m_count; row++) {
        ix = x;
        for (col = 0; col < cm->line_form && idx < m_count; col++, idx++) {
            if (cm->style != MSTYLE_NORMAL) {
                draw_pad(ix, iy, pick_dot(idx), 12);
                ix += 12;
            }
            {
                uint8_t xe = draw_item_text(ix, iy, idx);
                if (cm->style == MSTYLE_NORMAL && idx == m_cur &&
                    m_cur != 0xFF)
                    ui_invert(ix, iy, xe - ix, 12);
                ix = xe;
            }
            if (ix > xmax)
                xmax = ix;
        }
        iy += 12;
    }
    if (cm->framed) {
        uint8_t x0 = (x >= 3) ? x - 2 : 1;
        ui_hline(x0, xmax + 1, y - 1);
        ui_hline(x0, xmax + 1, iy);
        ui_vline(x0, y - 1, iy);
        ui_vline(xmax + 1, y - 1, iy);
    }
    fb_flush();
}

static void setup(const cmenu_t *m);

/* 顯示程序(等效 proc_show:PushMenuAll→呼叫→PullMenuAll+init_menu。
 * 引擎狀態為共享靜態,顯示程序可能嵌套開選單,必須存還原) */
static void proc_show(void)
{
    const cmenu_t *s_cm = cm;
    uint8_t s_cur = m_cur, s_start = m_start, s_check = m_check;
    show_fn fn;

    if (!cm->shows)
        return;
    menu_set = m_cur;
    fn = cm->shows[m_cur < cm->n ? m_cur : 0];
    if (fn)
        fn();

    setup(s_cm);                            /* 等效 init_menu */
    m_cur = s_cur;
    m_start = s_start;
    m_check = s_check;
}

static void setup(const cmenu_t *m)
{
    cm = m;
    m_stride = (m->flags & MF_DIGITS) ? 2 : 1;
    m_count = m->n ? m->n : dmenu_buf[0];
    m_rows = m->line_form ? (m_count + m->line_form - 1) / m->line_form : 0;
    m_vis = (m->scr_form && m->scr_form < m_rows) ? m->scr_form : m_rows;
}

/* 游標移入視窗(等效 adjust_pos/to_up_menu/to_down_menu) */
static void fix_start(void)
{
    uint8_t crow = m_cur / cm->line_form;
    uint8_t srow = m_start / cm->line_form;

    if (crow < srow)
        m_start = crow * cm->line_form;
    else if (crow >= srow + m_vis)
        m_start = (uint8_t)(crow - m_vis + 1) * cm->line_form;
}

uint8_t pop_menu(uint8_t x, uint8_t y, const cmenu_t *m) BANKED
{
    uint8_t k, r;

    setup(m);
    if (m_count == 0)
        return 0xFF;

    m_check = menu_set;                     /* 等效 init_pos check_item */
    m_cur = 0;
    m_start = 0;
    if ((m->flags & MF_USESET) && menu_set < m_count)
        m_cur = menu_set;
    fix_start();

    draw_menu(x, y);
    proc_show();

    for (;;) {
        k = wait_key();
        if (k == K_ESC)
            return 0xFF;

        if (k == K_CR) {
            menu_fn fn;
            uint8_t s_cur = m_cur, s_start = m_start, s_check = m_check;

            menu_set = m_cur;
            fn = m->handlers ? m->handlers[m->n ? m_cur : 0] : 0;
            if (!fn)
                return m_cur;
            r = fn();                       /* 可能嵌套開選單 */
            if (r == 1)
                return s_cur;
            if (r == 2)
                return 0xFE;
            setup(m);                       /* 等效 PullMenuAll+init_menu */
            m_cur = s_cur;
            m_start = s_start;
            m_check = s_check;
            if (m_count == 0)
                return m_cur;
            if (m_cur >= m_count)
                m_cur = m_count - 1;
            fix_start();
            draw_menu(x, y);
            proc_show();
            continue;
        }

        if (k == K_UP && m_rows > 1) {
            if (m_cur >= cm->line_form)
                m_cur -= cm->line_form;
            else
                m_cur = m_count - 1;
        } else if (k == K_DOWN && m_rows > 1) {
            m_cur += cm->line_form;
            if (m_cur >= m_count)
                m_cur = 0;
        } else if (k == K_LEFT && cm->line_form > 1) {
            m_cur = m_cur ? m_cur - 1 : m_count - 1;
        } else if (k == K_RIGHT && cm->line_form > 1) {
            m_cur++;
            if (m_cur >= m_count)
                m_cur = 0;
        } else {
            continue;
        }
        fix_start();
        draw_menu(x, y);
        proc_show();
    }
}

/* 等效 show_menu_txt:純顯示(游標=0xFF,無鍵迴圈、無顯示程序)。
 * m_check 沿 pop_menu 入口語義取 menu_set(CHECK 樣式的打勾;
 * 寫死 0xFF 會讓「退出技能欄後啟用中武功不打勾」,2026-07-10)。 */
void show_menu_txt(uint8_t x, uint8_t y, const cmenu_t *m) BANKED
{
    setup(m);
    if (m_count == 0)
        return;
    m_check = menu_set;
    m_cur = 0xFF;
    m_start = 0;
    draw_menu(x, y);
}

/* ---- lee3.s list_menu:18×5 字框 + 左 4 字分類欄 ---- */
uint8_t list_menu(uint8_t x, uint8_t y, const cmenu_t *m) BANKED
{
    uint8_t x0 = x - 2, y0 = y - 2;
    uint8_t x1 = x0 + 18 * 6;
    uint8_t y1 = y0 + 5 * 12 + 3;
    uint8_t xd = x0 + 4 * 6 + 3;

    fb_fill_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, 0);
    ui_hline(x0, x1, y0);
    ui_hline(x0, x1, y1);
    ui_vline(x0, y0, y1);
    ui_vline(x1, y0, y1);
    ui_vline(xd, y0, y1);

    list_x0 = xd + 1;
    list_y0 = y;
    list_x1 = x1 - 1;
    list_y1 = y1 - 1;

    return pop_menu(x, y, m);
}

void clean_list_right(void) BANKED
{
    fb_fill_rect(list_x0, list_y0, list_x1 - list_x0 + 1,
                 list_y1 - list_y0 + 1, 0);
}
