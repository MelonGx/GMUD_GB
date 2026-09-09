#pragma bank 25
/* fb.c - 160x144 1bpp framebuffer -> CGB background
 *
 * A full screen needs 360 unique 8x8 tiles, so two ordinary tile copies do
 * not fit in the 512 addressable CGB tile slots.  Each 2bpp tile therefore
 * stores one 1bpp frame in each bitplane.  Palette 0 selects one plane while
 * HBlank DMA prepares the other, then the palette flips in VBlank.
 */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "fb.h"
#include "menu_input.h"
#include "blit.h"

uint8_t fb[FB_ROWS * FB_STRIDE];

/* stage(2bpp 交錯,雙位面跨翻面持久)拆兩段(2026-07-15 行走提速):
 * 頭 12 tile 行(塊 0-239)SRAM bank1 0xA000(3840B,→0xAEFF);
 * 尾 6 tile 行(塊 240-359)SRAM bank0 0xB600(1920B,→0xBD7F,
 * 居 quest_list 0xB340+591=0xB58E 之後,SRAM 0xBFFF 之前,見
 * save.c/task.c 佈局)。
 * 尾段放 bank0 是「尾鏈異步」的前提:flush 把尾 120 塊掛成一條
 * HBlank 鏈(≤128 上限)後立即返回,行走迴圈接跑的合成/sync 全在
 * bank0/WRAM,與鏈讀源同 SRAM bank 相容(HDMA 逐片暫停 CPU,無
 * 匯流排競爭);尾段若在 bank1,鏈會讀到錯 bank 的垃圾。 */
#define HEAD_ROWS   12
#define TAIL_ROWS   6
#define HEAD_BLOCKS (HEAD_ROWS * 20)            /* 240 */
#define BLOCKS_ALL  360
#define TAIL_BLOCKS (BLOCKS_ALL - HEAD_BLOCKS)  /* 120(≤128,一條鏈) */
#define VRAM_BANK0_BLOCKS       180
#define DMA_MAX_BLOCKS          128
#define GDMA_FIRST_LINE         144
#define GDMA_END_LINE           152     /* exclusive; keep line 153 */
#define GDMA_BLOCKS_PER_LINE    12      /* CGB DMA rate is not CPU-speed scaled */
#define HDMA_LAST_VISIBLE_LINE  142     /* leave two lines of start margin */

#define DMA_OWNER_NONE          0
#define DMA_OWNER_GDMA          1
#define DMA_OWNER_SYNC_HBLANK   2
#define DMA_OWNER_ASYNC_TAIL    3

__at(0xA000) static uint8_t stage_head[HEAD_ROWS * 320];
__at(0xB600) static uint8_t stage_tail[TAIL_ROWS * 320];

/* DMA/staging ownership invariant:
 * - DMA_OWNER_ASYNC_TAIL owns SRAM bank0, stage_tail and VBK=1 until reaped.
 * - no SRAM bank switch or stage reuse occurs until stream_finish() has both
 *   reaped that transfer and committed its palette flip in VBlank.
 * - a new frame always writes active_plane^1, never a blind toggled plane.
 * Outside fb.c the program keeps SRAM bank0 selected (main.c is the only other
 * SWITCH_RAM caller), so an async tail may safely run while walk composition
 * continues in WRAM. */
static volatile uint8_t dma_owner;
static volatile uint8_t tail_plane;

static uint8_t row_dirty[18];
static volatile uint8_t active_plane;
static uint8_t prod_plane;
static volatile uint8_t present_pending;
static volatile uint8_t present_plane;
static const uint8_t *frame_src;
static const uint8_t *frame_overlay;
static uint8_t frame_overlay_w;

extern const uint8_t *cvt_src;
extern uint8_t *cvt_dst;
extern uint8_t cvt_n;
extern uint8_t cvt_off;
void cvt_plane_rows(void);

/* Kept in HOME by fb_palette.c: the NONBANKED VBL ISR must be able to read
 * them regardless of which switchable ROM bank was interrupted. */
extern const palette_color_t fb_pal_plane0[4];
extern const palette_color_t fb_pal_plane1[4];

void fb_mark_dirty(uint8_t y, uint8_t h) NONBANKED
{
    uint8_t t0, t1;

    if (!h || y >= FB_ROWS)
        return;
    frame_src = fb;
    frame_overlay = 0;
    t0 = y >> 3;
    t1 = (uint8_t)(y + h - 1) >> 3;
    for (; t0 <= t1 && t0 < 18; t0++)
        row_dirty[t0] = 1;
}

void fb_clear(void) NONBANKED
{
    frame_src = fb;
    frame_overlay = 0;
    memset(fb, 0, sizeof(fb));
    memset(row_dirty, 1, sizeof(row_dirty));
}

/* The public helpers below take 16-byte block counts, not byte lengths.  Zero
 * is rejected centrally: it must never underflow to HDMA5=0xff (128 blocks). */
static void dma_setup(uint16_t dst, const uint8_t *src)
{
    HDMA1_REG = (uint8_t)((uint16_t)src >> 8);
    HDMA2_REG = (uint8_t)src & 0xF0;
    HDMA3_REG = (uint8_t)(dst >> 8) & 0x1F;
    HDMA4_REG = (uint8_t)dst & 0xF0;
}

static uint8_t dma_gdma_start(uint8_t blocks)
{
    if (!blocks || blocks > DMA_MAX_BLOCKS || dma_owner != DMA_OWNER_NONE)
        return 0;

    dma_owner = DMA_OWNER_GDMA;
    HDMA5_REG = blocks - 1;
    dma_owner = DMA_OWNER_NONE;         /* CPU resumes only after GDMA ends */
    return blocks;
}

/* Nintendo requires HBlank DMA to be armed in mode 2 or mode 1, never mode 0.
 * Re-read both STAT and LY after reaching that boundary; 0xff means LCD off. */
static uint8_t dma_start_line(void)
{
    uint8_t mode, ly;

    while (LCDC_REG & LCDCF_ON) {
        mode = STAT_REG & 3;
        if (mode == STATF_OAM || mode == STATF_VBL) {
            ly = LY_REG;
            mode = STAT_REG & 3;
            if ((mode == STATF_OAM && ly < GDMA_FIRST_LINE) ||
                (mode == STATF_VBL && ly >= GDMA_FIRST_LINE))
                return ly;
        }
    }
    return 0xFF;
}

/* Centralized HBlank arming: count validation, STAT synchronization and the
 * actual FF55 write are one operation.  The visible synchronous owner also
 * derives its line budget only after reaching the stable mode2 boundary. */
static uint8_t dma_hblank_start(uint8_t want, uint8_t owner)
{
    uint8_t ly, n, room;

    if (!want || want > DMA_MAX_BLOCKS || dma_owner != DMA_OWNER_NONE ||
        (owner != DMA_OWNER_SYNC_HBLANK && owner != DMA_OWNER_ASYNC_TAIL))
        return 0;
    ly = dma_start_line();
    if (ly == 0xFF)
        return 0;
    n = want;
    disable_interrupts();
    if (dma_owner != DMA_OWNER_NONE || !(LCDC_REG & LCDCF_ON) ||
        (STAT_REG & 3) == STATF_HBL) {
        enable_interrupts();
        return 0;
    }
    if (owner == DMA_OWNER_SYNC_HBLANK) {
        ly = LY_REG;                     /* fresh count at the actual arm point */
        if (ly >= HDMA_LAST_VISIBLE_LINE) {
            enable_interrupts();
            return 0;
        }
        room = HDMA_LAST_VISIBLE_LINE - ly;
        if (n > room)
            n = room;
    }
    dma_owner = owner;
    HDMA5_REG = 0x80 | (n - 1);
    enable_interrupts();
    return n;
}

/* General DMA is safe with LCD off, or in the deliberately shortened VBlank
 * window.  The CGB DMA clock is fixed even when the CPU is in double speed;
 * reserve at most 12 blocks per remaining line and keep line 153 free. */
static uint8_t dma_gdma_window(uint16_t dst, const uint8_t *src, uint8_t want)
{
    uint8_t ly, n;

    if (!want)
        return 0;
    if (!(LCDC_REG & LCDCF_ON)) {
        n = want > DMA_MAX_BLOCKS ? DMA_MAX_BLOCKS : want;
        dma_setup(dst, src);
        return dma_gdma_start(n);
    }

    ly = LY_REG;
    if (ly < GDMA_FIRST_LINE || ly >= GDMA_END_LINE)
        return 0;
    dma_setup(dst, src);
    ly = dma_start_line();               /* stable mode1; budget from fresh LY */
    if (ly < GDMA_FIRST_LINE || ly >= GDMA_END_LINE)
        return 0;
    disable_interrupts();
    ly = LY_REG;                         /* final policy check at FF55 write */
    if (ly < GDMA_FIRST_LINE || ly >= GDMA_END_LINE ||
        dma_owner != DMA_OWNER_NONE) {
        enable_interrupts();
        return 0;
    }
    n = (uint8_t)((GDMA_END_LINE - ly) * GDMA_BLOCKS_PER_LINE);
    if (n > want)
        n = want;
    if (n > DMA_MAX_BLOCKS)
        n = DMA_MAX_BLOCKS;
    dma_owner = DMA_OWNER_GDMA;
    HDMA5_REG = n - 1;
    dma_owner = DMA_OWNER_NONE;
    enable_interrupts();
    return n;
}

/* Visible-period HBlank DMA transfers one block per line.  It is synchronous
 * here, so its source SRAM bank and VBK remain owned until hardware is idle. */
static uint8_t dma_hblank_visible(uint16_t dst, const uint8_t *src,
                                  uint8_t want)
{
    uint8_t n;

    if (!want || !(LCDC_REG & LCDCF_ON))
        return 0;
    dma_setup(dst, src);
    n = dma_hblank_start(want, DMA_OWNER_SYNC_HBLANK);
    if (!n)
        return 0;
    while (!(HDMA5_REG & 0x80))
        ;
    dma_owner = DMA_OWNER_NONE;
    return n;
}

/* At LY 142-143 wait for VBlank; at 152-153 wait through the frame wrap.
 * The next scheduler iteration then chooses visible HBlank or VBlank GDMA. */
static void dma_wait_phase(void)
{
    uint8_t ly = LY_REG;

    if (ly >= GDMA_END_LINE) {
        while (LY_REG >= GDMA_END_LINE)
            ;
    } else if (ly >= HDMA_LAST_VISIBLE_LINE && ly < GDMA_FIRST_LINE) {
        while (LY_REG < GDMA_FIRST_LINE)
            ;
    }
}

/* 產一個 tile 行(20 塊)到 stage 的隱藏位面。呼叫方負責 SRAM bank:
 * 行 <HEAD_ROWS 需 bank1,其餘需 bank0。 */
static void produce_row(uint8_t r)
{
    cvt_src = frame_src + (uint16_t)r * 160;
    cvt_dst = (r < HEAD_ROWS)
                  ? stage_head + (uint16_t)r * 320
                  : stage_tail + (uint16_t)(r - HEAD_ROWS) * 320;
    cvt_off = prod_plane;
    cvt_n = 1;
    cvt_plane_rows();
}

/* GDMA 分段直灌(單段上限 128 塊;LCD 關或 VBlank 窗內使用) */
static uint8_t gdma_blocks(uint16_t dst, const uint8_t *src, uint16_t blocks)
{
    uint8_t n;

    while (blocks) {
        n = (blocks > DMA_MAX_BLOCKS) ? DMA_MAX_BLOCKS : (uint8_t)blocks;
        dma_setup(dst, src);
        if (!dma_gdma_start(n))                  /* caller has LCD off */
            return 0;
        src += (uint16_t)n * 16;
        dst += (uint16_t)n * 16;
        blocks -= n;
    }
    return 1;
}

/* present_plane is immutable while pending.  Keeping it separate from the
 * producer removes the old stale flip_target race. */
static void present_now(void) NONBANKED
{
    active_plane = present_plane;
    set_bkg_palette(0, 1,
                    active_plane ? fb_pal_plane1 : fb_pal_plane0);
    present_pending = 0;
}

static void tail_reap(void) NONBANKED
{
    VBK_REG = 0;
    dma_owner = DMA_OWNER_NONE;
    present_plane = tail_plane;
    present_pending = 1;
}

/* VBL ISR reaps an async tail and commits only inside the same conservative
 * VBlank window used by GDMA.  A delayed ISR after line 151 waits one frame. */
void fb_vbl_isr(void) NONBANKED
{
    uint8_t ly;

    ly = LY_REG;
    if (ly >= GDMA_FIRST_LINE && ly < GDMA_END_LINE) {
        if (present_pending)
            present_now();
        if (dma_owner == DMA_OWNER_ASYNC_TAIL && (HDMA5_REG & 0x80)) {
            tail_reap();
            present_now();
        }
    }

    /* Sample only after the timing-sensitive framebuffer commit. */
    if (menu_joy_active) {
        uint8_t raw = joypad();
        uint8_t pressed = menu_joy_prev ? 0 : raw;

        menu_joy_prev = raw;
        menu_joy_now = raw;
        if (pressed) {
            uint8_t next = (menu_joy_head + 1) &
                           (MENU_JOY_QUEUE_SIZE - 1);
            if (next != menu_joy_tail) {
                menu_joy_queue[menu_joy_head] = pressed;
                menu_joy_head = next;
            }
        }
    }
}

/* Queue a completed synchronous frame. */
static void commit_present(uint8_t plane)
{
    uint8_t ly;

    disable_interrupts();
    present_plane = plane;
    present_pending = 1;
    ly = LY_REG;
    if (!(LCDC_REG & LCDCF_ON) ||
        (ly >= GDMA_FIRST_LINE && ly < GDMA_END_LINE))
        present_now();
    enable_interrupts();
}

/* Wait until the async tail no longer owns stage_tail/SRAM bank0/VBK.  A
 * palette commit may still be pending after return; staging-only CPU work in
 * bank0 is safe during that gap, but SRAM bank switches and VRAM writes are
 * not. */
static void stream_wait_dma(void)
{
    uint8_t done;

    while (1) {
        disable_interrupts();
        if (dma_owner == DMA_OWNER_ASYNC_TAIL) {
            if (!(LCDC_REG & LCDCF_ON)) {
                if (!(HDMA5_REG & 0x80))
                    HDMA5_REG = 0;      /* abort; next full frame overwrites it */
                dma_owner = DMA_OWNER_NONE;
                present_pending = 0;
                VBK_REG = 0;
            } else if (HDMA5_REG & 0x80) {
                tail_reap();
            }
        }
        done = dma_owner == DMA_OWNER_NONE;
        enable_interrupts();
        if (done)
            return;
    }
}

/* Finish the palette half of the frame handoff.  Call before any VRAM write
 * or before choosing a switchable SRAM bank other than the released bank0. */
static void stream_wait_present(void)
{
    uint8_t done, ly;

    while (1) {
        disable_interrupts();
        if (present_pending) {
            ly = LY_REG;
            if (!(LCDC_REG & LCDCF_ON) ||
                (ly >= GDMA_FIRST_LINE && ly < GDMA_END_LINE))
                present_now();
        }
        done = !present_pending;
        enable_interrupts();
        if (done)
            return;
    }
}

/* Full barrier used by synchronous callers and before HALT/vsync. */
static void stream_finish(void)
{
    stream_wait_dma();
    stream_wait_present();
}

/* Arm the only asynchronous transfer.  State is published with interrupts
 * off, so the VBL ISR cannot mistake the idle HDMA5 value for completion. */
static uint8_t tail_start(uint16_t dst, const uint8_t *src, uint8_t blocks,
                          uint8_t plane)
{
    dma_setup(dst, src);
    tail_plane = plane;
    return dma_hblank_start(blocks, DMA_OWNER_ASYNC_TAIL);
}

/* 頭段(bank1)邊轉置邊串流(windowed-chase,2026-07-15 提速):
 * 轉置本身 ~165 線佔滿一幀,期間必掠過 VBlank 窗——舊「全轉置再傳」
 * 白白浪費這些窗。本版把頭 12 行的生產與傳輸交織:
 *   ·可視行 → 轉置一行(CPU,把窗填滿可傳量);
 *   ·VBlank 窗 → 固定硬體速率 GDMA 灌「已產未傳」塊(12 塊/線預算);
 *   ·全產完仍有殘餘 → 可視行滿長 HBlank 鏈補完。
 * 頭大部分在轉置的窗內免費傳掉,關鍵路徑≈轉置+少量 HBlank 尾。
 * VBK 界:塊 180=行 9;GDMA/HBlank 片不跨界。呼叫前 SRAM 須 bank1。 */
static void stream_head(void)
{
    uint16_t consumed = 0, produced, avail;
    uint8_t prow = 0, n;
    const uint8_t *sp;
    uint16_t dp;

    produce_row(0);                     /* 先產 1 行墊底,窗來即可傳 */
    prow = 1;
    while (consumed < HEAD_BLOCKS) {
        produced = (uint16_t)prow * 20;
        if (consumed < produced) {
            avail = produced - consumed;        /* 已產未傳 */
            if (consumed < VRAM_BANK0_BLOCKS) { /* 片不跨 vbank 界 */
                if (avail > VRAM_BANK0_BLOCKS - consumed)
                    avail = VRAM_BANK0_BLOCKS - consumed;
                VBK_REG = 0;
                dp = 0x8000 + consumed * 16;
            } else {
                VBK_REG = 1;
                dp = 0x8000 + (consumed - VRAM_BANK0_BLOCKS) * 16;
            }
            if (avail > DMA_MAX_BLOCKS)
                avail = DMA_MAX_BLOCKS;
            sp = stage_head + consumed * 16;

            /* A safe VBlank window has priority; otherwise keep producing so
             * conversion time overlaps the approach to the next window. */
            n = dma_gdma_window(dp, sp, (uint8_t)avail);
            if (n) {
                consumed += n;
                continue;
            }
        }

        if (prow < HEAD_ROWS) {
            produce_row(prow++);
        } else if (consumed < produced) {       /* 全產完殘餘:HBlank 補 */
            avail = produced - consumed;
            if (consumed < VRAM_BANK0_BLOCKS) {
                if (avail > VRAM_BANK0_BLOCKS - consumed)
                    avail = VRAM_BANK0_BLOCKS - consumed;
                VBK_REG = 0;
                dp = 0x8000 + consumed * 16;
            } else {
                VBK_REG = 1;
                dp = 0x8000 + (consumed - VRAM_BANK0_BLOCKS) * 16;
            }
            if (avail > DMA_MAX_BLOCKS)
                avail = DMA_MAX_BLOCKS;
            sp = stage_head + consumed * 16;

            n = dma_hblank_visible(dp, sp, (uint8_t)avail);
            if (n) {
                consumed += n;
                continue;
            }
            /* LY 142-143 waits to 144; LY 152-153 waits through wrap. */
            dma_wait_phase();
        }
    }
}

/* 全幀 flush(windowed-chase + 異步尾鏈):
 * ① 收完上幀尾鏈,釋放 bank0/stage_tail;② 以上幀待顯示位面的反面為
 *    目標,趁等 VBlank 翻面只轉置下一幀尾段;③ 翻面提交後才切 bank1
 *    或寫 VRAM;④ 頭 12 行邊轉置邊用安全窗傳輸;⑤ 尾 120
 *    塊掛 HBlank 鏈,行走路徑可返回,但下次 SRAM 切 bank/stage 重用或
 *    HALT/vsync 前必須 stream_finish。
 * VRAM 位面 0..8 行=vbank0 id 0..179,9..17 行=vbank1。 */
static void flush_all(uint8_t async)
{
    uint8_t prow, ok;

    stream_wait_dma();
    disable_interrupts();
    prod_plane = (present_pending ? present_plane : active_plane) ^ 1;
    enable_interrupts();

    SWITCH_RAM(0);
    for (prow = HEAD_ROWS; prow < 18; prow++)
        produce_row(prow);

    /* Staging is ready; do not switch SRAM bank or write VRAM until the old
     * active plane has actually yielded to present_plane in VBlank. */
    stream_wait_present();
    SWITCH_RAM(1);

    if (!(LCDC_REG & LCDCF_ON)) {       /* LCD 關:全同步 GDMA */
        for (prow = 0; prow < HEAD_ROWS; prow++)
            produce_row(prow);
        VBK_REG = 0;                     /* stage_head 在 bank1(現行 SRAM) */
        ok = gdma_blocks(0x8000, stage_head, VRAM_BANK0_BLOCKS);
        if (ok) {
            VBK_REG = 1;
            ok = gdma_blocks(0x8000,
                             stage_head + VRAM_BANK0_BLOCKS * 16U,
                             HEAD_BLOCKS - VRAM_BANK0_BLOCKS);
        }
        SWITCH_RAM(0);                   /* stage_tail 在 bank0 */
        if (ok)
            ok = gdma_blocks(0x8000 +
                             (HEAD_BLOCKS - VRAM_BANK0_BLOCKS) * 16,
                             stage_tail, TAIL_BLOCKS);
        VBK_REG = 0;
        if (ok) {
            commit_present(prod_plane);
            memset(row_dirty, 0, sizeof(row_dirty));
        }
        return;
    }

    stream_head();              /* 頭:邊產邊傳(bank1) */

    /* ---- 尾 120 塊掛一條 HBlank 鏈(bank0 源) ---- */
    SWITCH_RAM(0);             /* 鏈讀 stage_tail(bank0);返回後行走亦需 bank0 */
    VBK_REG = 1;                /* 鏈期間必須保持(HDMA 逐片取當前 VBK) */
    ok = tail_start(0x8000 + (HEAD_BLOCKS - VRAM_BANK0_BLOCKS) * 16,
                    stage_tail, TAIL_BLOCKS, prod_plane);
    if (!ok) {                         /* LCD was disabled at the arm boundary */
        if (!(LCDC_REG & LCDCF_ON) &&
            gdma_blocks(0x8000 +
                        (HEAD_BLOCKS - VRAM_BANK0_BLOCKS) * 16,
                        stage_tail, TAIL_BLOCKS)) {
            VBK_REG = 0;
            commit_present(prod_plane);
            memset(row_dirty, 0, sizeof(row_dirty));
        } else
            VBK_REG = 0;
        return;
    }
    memset(row_dirty, 0, sizeof(row_dirty));
    if (!async)
        stream_finish();
}

static void flush_dirty(uint8_t async)
{
    uint8_t ty;

    for (ty = 0; ty < 18; ty++)
        if (row_dirty[ty]) {
            flush_all(async);
            return;
        }
}

static void sync_frame_source(void)
{
    if (frame_src != fb) {
        uint8_t r, nb, m;
        uint8_t *p;
        const uint8_t *q;

        fo_src = frame_src;             /* 2880B=9×320;棧彈跳 ≈4.6 cy/B
                                         * (memcpy 8.3),sync 96→~62 線 */
        fo_dst = fb;
        fo_h = sizeof(fb) / 320;
        fo_copy_pop();
        if (frame_overlay) {
            nb = frame_overlay_w >> 3;
            m = (uint8_t)(0xFF00 >> (frame_overlay_w & 7));
            for (p = fb, q = frame_overlay, r = 0; r < 12;
                 r++, p += FB_STRIDE, q += 12) {
                memcpy(p, q, nb);
                if (m)
                    p[nb] = (uint8_t)((p[nb] & ~m) | (q[nb] & m));
            }
        }
        frame_src = fb;
        frame_overlay = 0;
    }
}

void fb_present(const uint8_t *src) NONBANKED
{
    frame_src = src;
    frame_overlay = 0;
    memset(row_dirty, 1, sizeof(row_dirty));
}

void fb_present_overlay(const uint8_t *src, const uint8_t *overlay,
                        uint8_t width_px) NONBANKED
{
    frame_src = src;
    frame_overlay = overlay;
    frame_overlay_w = width_px;
    memset(row_dirty, 1, sizeof(row_dirty));
}

/* 同步 flush:先收上一輪尾鏈(即使無髒行也要,否則掛起幀永不上屏),
 * 再同步刷到底。所有非行走路徑(選單/戰鬥/文字)語義與舊版一致。 */
void fb_flush(void) BANKED
{
    stream_finish();
    sync_frame_source();
    flush_dirty(0);
}

/* 行走串流(逐子步全刷,8px/更新):sync 先跑——上一輪尾鏈還在
 * 自走,sync 只碰 WRAM/bank0,正好蓋掉鏈的傳輸時間;flush_all(1)
 * 掛新尾鏈即返回,下一子步的合成/sync 繼續蓋。 */
void fb_flush_stream(void) BANKED
{
    sync_frame_source();
    flush_dirty(1);
}

void fb_stream_drain(void) BANKED
{
    stream_finish();
}

void fb_init(void) BANKED
{
    uint8_t tx, ty, id, attr;
    uint8_t ids[20], attrs[20];
    uint8_t *mapA = (uint8_t *)0x9800;
    uint8_t *mapB = (uint8_t *)0x9C00;

    DISPLAY_OFF;
    LCDC_REG |= LCDCF_BG8000;
    LCDC_REG &= ~LCDCF_BG9C00;

    VBK_REG = 1;
    memset((void *)0x8000, 0, 0x1000);
    VBK_REG = 0;
    memset((void *)0x8000, 0, 0x1000);

    for (ty = 0; ty < 18; ty++) {
        attr = (ty < 9) ? 0x00 : 0x08;
        for (tx = 0; tx < 20; tx++) {
            id = (uint8_t)((uint16_t)(ty % 9) * 20 + tx);
            ids[tx] = id;
            attrs[tx] = attr;
        }
        VBK_REG = 0;
        memcpy(mapA + (uint16_t)ty * 32, ids, 20);
        memcpy(mapB + (uint16_t)ty * 32, ids, 20);
        VBK_REG = 1;
        memcpy(mapA + (uint16_t)ty * 32, attrs, 20);
        memcpy(mapB + (uint16_t)ty * 32, attrs, 20);
    }
    VBK_REG = 0;

    if (!(HDMA5_REG & 0x80))    /* 開機/重入:終止可能殘留的鏈 */
        HDMA5_REG = 0;
    dma_owner = DMA_OWNER_NONE;
    active_plane = 0;
    prod_plane = 0;
    tail_plane = 0;
    present_plane = 0;
    present_pending = 0;
    disable_interrupts();
    add_VBL(fb_vbl_isr);
    enable_interrupts();
    frame_src = fb;
    frame_overlay = 0;
    set_bkg_palette(0, 1, fb_pal_plane0);
    SWITCH_RAM(1);
    memset(stage_head, 0, sizeof(stage_head));
    SWITCH_RAM(0);
    memset(stage_tail, 0, sizeof(stage_tail));
    fb_clear();
}
