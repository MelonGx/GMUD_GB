/* guide.c - 遊戲攻略:全螢幕捲動文字瀏覽器
 *
 * 文本在 guide_data.h(gen_guide.py 生成),物品/门派/人物/赚钱攻略。
 * 上下鍵平滑捲動(按住連發),左右鍵翻頁,B 鍵退出。
 * 標題畫面進入(create.c title_screen)。
 *
 * show_one_line 在 bank 25,呼叫後本 bank ROM 不可讀——
 * 必須先拷行資料到 WRAM 棧緩衝(同 cheat.c show_rom_line 手法)。
 *
 * 文本 blob 於 2026-07-29 加入修行/任務攻略後超過 16KB,已移出本 bank
 * (assets/guide.bin → guide_bN.c,GUIDE_FIRST_BANK 起),改用 res_read
 * 取行;索引表 guide_block_offs/guide_lens 仍在本 bank 直接定址。
 */
#pragma bank 23
#include <gb/gb.h>
#include <string.h>
#include "ui.h"
#include "fb.h"
#include "font.h"
#include "guide_res.h"

#include "guide_data.h"

#define VIS_LINES 11
#define LINE_H    13
#define SECTION_MENU_STEP 15
#define MAX_LINE_BYTES 30

#define VIEW_H    (VIS_LINES * LINE_H)   /* 143:可視區像素高 */

static uint16_t view_start;
static uint16_t view_end;
static uint16_t view_max_top;

static uint16_t drawn_top;   /* 目前畫面上實際顯示的 top(供部分捲動算差) */

/* idx 行在 guide.bin 的位元組偏移;連續繪多行時由呼叫端沿用上一行的
 * 結果(off += guide_lens[idx]),省掉每行從區塊起點重走一次。 */
static uint16_t guide_line_off(uint16_t idx)
{
    uint16_t base = idx & ~((1u << GUIDE_BLOCK_SHIFT) - 1u);
    uint16_t off = guide_block_offs[idx >> GUIDE_BLOCK_SHIFT];

    while (base < idx)
        off += guide_lens[base++];
    return off;
}

static void show_guide_line_at(uint16_t idx, uint16_t off, uint8_t xh, uint8_t y)
{
    uint8_t buf[MAX_LINE_BYTES + 1];

    /* guide_lens 含行末 0xFF,故整行一次讀完即自帶終止符;長度上限由
     * gen_guide.py 對 MAX_LINE_BYTES+1 把關。res_read 會還原本 bank。 */
    res_read(&guide_res, off, buf, guide_lens[idx]);
    ss_ptr = buf;
    show_one_line(xh, y);
}

static void show_guide_line(uint16_t idx, uint8_t xh, uint8_t y)
{
    if (idx >= GUIDE_NLINES) return;
    show_guide_line_at(idx, guide_line_off(idx), xh, y);
}

static void draw_page(uint16_t top)
{
    uint16_t idx, off;
    uint8_t i;

    /* 整頁重畫必清全屏。clear_nline2 → fb_fill_rect 是像素精確版本,
     * 全屏走它要逐行掃 20 個字節(實測 ~1.9 幀);fb_clear 是一次
     * memset + 全屏標髒,同樣效果但快一個量級。 */
    fb_clear();
    idx = top;
    off = guide_line_off(top);
    for (i = 0; i < VIS_LINES; i++, idx++) {
        if (idx >= view_end) break;
        show_guide_line_at(idx, off, 0, i * LINE_H);
        off += guide_lens[idx];
    }
    fb_flush();
    drawn_top = top;
}

/* 小步進(< VIS_LINES 行)捲動:位移既有畫面像素列+只補繪新露出的
 * 那 1~2 行,不整頁重畫。實測(guide_flush_profile.py):整頁 11 行
 * CJK 字型重繪(show_one_line)每行耗~2.5幀,11行合計~30幀,是捲動
 * 延遲的主因;flush/cvt_plane_rows 轉置本身僅~1.2幀。位移改成只繪
 * 1~2 新行後,單步耗時可壓到接近 flush 本身的量級。 */
static void scroll_step(uint16_t new_top, uint8_t n, uint8_t down)
{
    uint16_t shift = (uint16_t)n * LINE_H;
    uint8_t i;

    if (down) {
        memmove(fb, fb + shift * FB_STRIDE,
                (uint16_t)(VIEW_H - shift) * FB_STRIDE);
        clear_nline2((uint8_t)(VIEW_H - shift), (uint8_t)shift);
        for (i = 0; i < n; i++)
            show_guide_line(new_top + VIS_LINES - n + i, 0,
                            (uint8_t)(VIEW_H - shift + (uint16_t)i * LINE_H));
    } else {
        memmove(fb + shift * FB_STRIDE, fb,
                (uint16_t)(VIEW_H - shift) * FB_STRIDE);
        clear_nline2(0, (uint8_t)shift);
        for (i = 0; i < n; i++)
            show_guide_line(new_top + i, 0, (uint8_t)((uint16_t)i * LINE_H));
    }
    fb_mark_dirty(0, VIEW_H);
    fb_flush();
    drawn_top = new_top;
}

/* 依 drawn_top 與新 top 的差距分派:差距 < VIS_LINES 用部分捲動,
 * 否則(LEFT/RIGHT 整頁跳)沒有重疊內容可省,走原本整頁重繪。 */
static void scroll_to(uint16_t new_top)
{
    uint16_t d;

    if (new_top == drawn_top) return;
    if (new_top > drawn_top) {
        d = new_top - drawn_top;
        if (d >= VIS_LINES) { draw_page(new_top); return; }
        scroll_step(new_top, (uint8_t)d, 1);
    } else {
        d = drawn_top - new_top;
        if (d >= VIS_LINES) { draw_page(new_top); return; }
        scroll_step(new_top, (uint8_t)d, 0);
    }
}

static uint8_t pick_section(uint8_t cur)
{
    uint8_t i, k, y;

    waitpadup();
    fb_clear();
    for (i = 0; i < GUIDE_NSECTIONS; i++)
        show_guide_line(guide_section_titles[i], 2,
                        (uint8_t)(4 + i * SECTION_MENU_STEP));

    for (;;) {
        y = (uint8_t)(4 + cur * SECTION_MENU_STEP);
        font_draw_ascii(0, y, '>');
        fb_flush();
        k = wait_key_rep();
        fb_fill_rect(0, y, 8, 13, 0);

        if (k == K_UP)
            cur = cur ? (uint8_t)(cur - 1) : (GUIDE_NSECTIONS - 1);
        else if (k == K_DOWN)
            cur = (cur + 1 == GUIDE_NSECTIONS) ? 0 : (uint8_t)(cur + 1);
        else if (k == K_CR)
            return cur;
        else if (k == K_ESC)
            return 0xFF;
    }
}

static void view_section(uint8_t section)
{
    uint16_t top, nlines;
    uint8_t j, hold, moved;

    view_start = guide_section_starts[section];
    view_end = guide_section_starts[section + 1];
    nlines = view_end - view_start;
    view_max_top = (nlines > VIS_LINES) ?
                   (uint16_t)(view_end - VIS_LINES) : view_start;
    top = view_start;

    waitpadup();
    draw_page(top);

    hold = 0;
    for (;;) {
        vsync();
        j = joypad();

        if (j & J_B) break;

        moved = 0;
        if (j & (J_UP | J_DOWN | J_LEFT | J_RIGHT)) {
            if (hold == 0 || hold > 6) {
                if (j & J_UP) {
                    if (top > view_start) {
                        if (hold == 0) {
                            top--;
                        } else {
                            top = (top >= view_start + 2) ?
                                  top - 2 : view_start;
                        }
                        moved = 1;
                    }
                } else if (j & J_DOWN) {
                    if (top < view_max_top) {
                        if (hold == 0) {
                            top++;
                        } else {
                            top += 2;
                            if (top > view_max_top) top = view_max_top;
                        }
                        moved = 1;
                    }
                } else if (j & J_LEFT) {
                    if (top > view_start) {
                        top = (top >= view_start + VIS_LINES) ?
                              top - VIS_LINES : view_start;
                        moved = 1;
                    }
                } else if (j & J_RIGHT) {
                    if (top < view_max_top) {
                        top += VIS_LINES;
                        if (top > view_max_top) top = view_max_top;
                        moved = 1;
                    }
                }
            }
            hold++;
        } else {
            hold = 0;
        }

        if (moved)
            scroll_to(top);
    }
}

void guide_viewer(void) BANKED
{
    uint8_t section = 0;
    uint8_t picked;

    for (;;) {
        picked = pick_section(section);
        if (picked == 0xFF)
            return;
        section = picked;
        view_section(section);
    }
}