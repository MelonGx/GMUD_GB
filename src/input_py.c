/* input_py.c - 拼音輸入法取名(Task#9,用戶核可 2026-07-15)
 *
 * 原版 NC2000 取名走宿主系統拼音輸入法(不在遊戲源碼內),GBC 無宿主
 * IME,依文曲星操作習慣重建:
 *   上半 a-z 字母格拼音節,候選字即時前綴匹配,下方候選列翻頁。
 *   A=選字母/選字  B=退拼音字母→退名字→退出  START=取名完成
 *   候選列:字母格第二行按下/第一行按上進入,左右移動跨頁,上下返回。
 * 資料 pinyin_res(bank 38,gen_pinyin.py):音節字母序、候選字按
 * 音節分組串接 → 前綴命中=連續音節=連續條目區間,翻頁 O(1) 索引;
 * res_read(HOME)跨 bank 取數,本檔代碼 bank 39。
 * 名字上限 4 漢字(man_name 8 字節,與原版存檔佈局一致)。 */
#pragma bank 23
#include <gb/gb.h>
#include <string.h>
#include "save.h"
#include "ui.h"
#include "fb.h"
#include "font.h"
#include "res.h"
#include "pinyin_res.h"
#include "input_py.h"

#define PAGE_N   10
#define GRID_Y0  20            /* 字母兩行 y=20/34 */
#define CAND_Y   62
#define HDR_Y    2
#define SYL_BASE 4             /* pinyin.bin:頭 4B 後音節表 */
#define SYL_SIZE 8             /* 6B 音節 + 2B off */

static const uint8_t s_name[] = "\xD0\xD5\xC3\xFB\x3A";     /* 姓名: */

static uint16_t nsyl, chars_base;
static uint8_t pybuf[6], pylen;         /* 拼音緩衝 */
static uint8_t nmbuf[8], nmlen;         /* 名字 GB 字節(≤4 字) */
static uint16_t cand_lo, cand_hi;       /* 前綴命中條目區間 */
static uint16_t page;                   /* 候選頁(PAGE_N/頁) */

/* 讀第 i 條音節並與 pybuf 前 pylen 字節比較(<0/=0/>0) */
static int syl_cmp(uint16_t i, uint8_t *e)
{
    res_read(&pinyin_res, SYL_BASE + i * SYL_SIZE, e, SYL_SIZE);
    return memcmp(e, pybuf, pylen);
}

/* pybuf 前綴 → 命中音節連續段的條目區間(音節字母序保證連續)。
 * 二分上下界(~2log n 次 res_read;逐條線性掃 405 條 ≈ 15 幀,
 * 會吞下一個按鍵,實機也頓,2026-07-18 首版教訓) */
static void rescan(void)
{
    uint8_t e[SYL_SIZE];
    uint16_t lo, hi, mid;

    cand_lo = cand_hi = 0;
    page = 0;
    if (!pylen)
        return;

    lo = 0;                             /* 下界:首個 syl >= 前綴 */
    hi = nsyl;
    while (lo < hi) {
        mid = (lo + hi) >> 1;
        if (syl_cmp(mid, e) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == nsyl || syl_cmp(lo, e) != 0)
        return;                         /* 無命中 */
    cand_lo = (uint16_t)e[6] | ((uint16_t)e[7] << 8);

    hi = nsyl;                          /* 上界:首個 syl > 前綴 */
    mid = lo + 1;
    for (lo = mid; lo < hi; ) {
        mid = (lo + hi) >> 1;
        if (syl_cmp(mid, e) <= 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    syl_cmp(hi, e);                     /* 讀上界條(可為哨兵)取 off */
    cand_hi = (uint16_t)e[6] | ((uint16_t)e[7] << 8);
}

static uint16_t cand_code(uint16_t idx)
{
    uint8_t g[2];

    res_read(&pinyin_res, chars_base + idx * 2, g, 2);
    return ((uint16_t)g[0] << 8) | g[1];
}

/* 游標反白(XOR):畫上/擦掉同一個呼叫 */
static void cur_invert(uint8_t focus, uint8_t grow, uint8_t gcol,
                       uint8_t csel)
{
    if (focus == 0)
        ui_invert((uint8_t)(7 + gcol * 12),
                  (uint8_t)(GRID_Y0 + grow * 14), 10, 13);
    else
        ui_invert((uint8_t)(15 + csel * 13), CAND_Y, 13, 13);
}

static void draw_all(uint8_t focus, uint8_t grow, uint8_t gcol,
                     uint8_t csel)
{
    uint8_t i, x;
    uint16_t base = cand_lo + page * PAGE_N;

    fb_clear();
    font_draw_text(4, HDR_Y, s_name);
    x = 36;
    for (i = 0; i < nmlen; i += 2)
        x += font_draw_cjk(x, HDR_Y,
                           ((uint16_t)nmbuf[i] << 8) | nmbuf[i + 1]);
    if (nmlen < 8)
        font_draw_ascii(x, HDR_Y, '_');
    for (i = 0; i < pylen; i++)                 /* 拼音緩衝(右上) */
        font_draw_ascii((uint8_t)(112 + i * 8), HDR_Y, pybuf[i]);
    ui_hline(0, 159, 16);

    for (i = 0; i < 26; i++)                    /* 字母格 13×2 */
        font_draw_ascii((uint8_t)(9 + (i % 13) * 12),
                        (uint8_t)(GRID_Y0 + (i / 13) * 14),
                        (uint8_t)('a' + i));

    for (i = 0; i < PAGE_N && base + i < cand_hi; i++)
        font_draw_cjk((uint8_t)(16 + i * 13), CAND_Y, cand_code(base + i));
    if (page)
        font_draw_ascii(4, CAND_Y, '<');
    if (base + PAGE_N < cand_hi)
        font_draw_ascii(150, CAND_Y, '>');

    cur_invert(focus, grow, gcol, csel);
    fb_flush();
}

uint8_t input_pinyin(void) BANKED
{
    uint8_t focus = 0, grow = 0, gcol = 0, csel = 0, k, pcnt;
    uint8_t o_focus = 0, o_grow = 0, o_gcol = 0, o_csel = 0;
    uint8_t dirty = 1;              /* 1=內容變了要整屏重畫 */
    uint16_t base, n;

    nsyl = res_read16(&pinyin_res, 0);
    chars_base = SYL_BASE + (nsyl + 1) * SYL_SIZE;
    pylen = 0;
    nmlen = 0;
    rescan();

    for (;;) {
        base = cand_lo + page * PAGE_N;
        n = cand_hi - base;
        pcnt = (n > PAGE_N) ? PAGE_N : (uint8_t)n;
        if (focus && !pcnt)
            focus = 0;
        if (focus && csel >= pcnt)
            csel = (uint8_t)(pcnt - 1);
        /* 只挪游標時不重畫 26 個字母格與候選列(整屏重畫實測 13 幀,
         * 只搬游標 6 幀)。內容真的變了才走 draw_all。 */
        if (dirty) {
            draw_all(focus, grow, gcol, csel);
        } else {
            cur_invert(o_focus, o_grow, o_gcol, o_csel);    /* 擦舊 */
            cur_invert(focus, grow, gcol, csel);            /* 畫新 */
            fb_flush();
        }
        o_focus = focus;
        o_grow = grow;
        o_gcol = gcol;
        o_csel = csel;
        dirty = 0;

        k = wait_key_rep();
        if (k == K_F1) {                        /* START:取名完成 */
            if (nmlen) {
                memset(hero.man_name, 0, sizeof hero.man_name);
                memcpy(hero.man_name, nmbuf, nmlen);
                return 1;
            }
        } else if (k == K_ESC) {                /* B:逐級回退 */
            dirty = 1;
            if (pylen) {
                pylen--;
                rescan();
            } else if (nmlen) {
                nmlen -= 2;
            } else {
                return 0;
            }
            focus = 0;
        } else if (focus == 0) {                /* 字母格 */
            if (k == K_LEFT) {
                gcol = gcol ? (uint8_t)(gcol - 1) : 12;
            } else if (k == K_RIGHT) {
                gcol = (gcol == 12) ? 0 : (uint8_t)(gcol + 1);
            } else if (k == K_UP || k == K_DOWN) {
                /* 第一行上/第二行下 → 候選列(有候選時),否則換行 */
                if (pcnt && grow == ((k == K_DOWN) ? 1 : 0)) {
                    focus = 1;
                    csel = 0;
                } else {
                    grow ^= 1;
                }
            } else if (k == K_CR) {
                if (pylen < 6) {
                    pybuf[pylen++] = (uint8_t)('a' + grow * 13 + gcol);
                    rescan();
                    dirty = 1;
                }
            }
        } else {                                /* 候選列 */
            if (k == K_UP || k == K_DOWN) {
                focus = 0;
            } else if (k == K_LEFT) {
                if (csel)
                    csel--;
                else if (page) {
                    page--;
                    csel = PAGE_N - 1;
                    dirty = 1;
                }
            } else if (k == K_RIGHT) {
                if ((uint8_t)(csel + 1) < pcnt)
                    csel++;
                else if (base + PAGE_N < cand_hi) {
                    page++;
                    csel = 0;
                    dirty = 1;
                }
            } else if (k == K_CR) {             /* 選字 */
                if (nmlen < 8) {
                    uint16_t code = cand_code(base + csel);
                    nmbuf[nmlen++] = (uint8_t)(code >> 8);
                    nmbuf[nmlen++] = (uint8_t)code;
                    pylen = 0;
                    rescan();
                    focus = 0;
                    dirty = 1;
                }
            }
        }
    }
}
