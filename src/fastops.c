/* fastops.c - 熱路徑的 SM83 匯編例程(行為與 C 版完全一致,純性能)
 *
 * 適用條件:目標 x 字節對齊且整塊在 scroll_buf 內(速度 8 的行走恆滿足);
 * 邊緣/位移情形仍走 blit.c 的通用路徑。
 */
#include <stdint.h>

uint8_t *fo_dst;            /* scroll_buf 內目標(塊左上角所在字節) */
const uint8_t *fo_src;      /* 圖塊資料(每行 4 字節連續) */
uint8_t fo_h;               /* 行數(32 或 48) */

/* print:整塊覆寫 */
void fo_copy(void) __naked
{
__asm
    ld  a, (#_fo_src)
    ld  l, a
    ld  a, (#_fo_src+1)
    ld  h, a
    ld  a, (#_fo_dst)
    ld  e, a
    ld  a, (#_fo_dst+1)
    ld  d, a
    ld  a, (#_fo_h)
    ld  b, a
1$:
    ld  a, (hl+)
    ld  (de), a
    inc de
    ld  a, (hl+)
    ld  (de), a
    inc de
    ld  a, (hl+)
    ld  (de), a
    inc de
    ld  a, (hl+)
    ld  (de), a
    inc de
    ld  a, e            ; de += 16(下一行)
    add a, #16
    ld  e, a
    jr  nc, 2$
    inc d
2$:
    dec b
    jr  nz, 1$
    ret
__endasm;
}

/* or:疊加 */
void fo_or(void) __naked
{
__asm
    ld  a, (#_fo_src)
    ld  l, a
    ld  a, (#_fo_src+1)
    ld  h, a
    ld  a, (#_fo_dst)
    ld  e, a
    ld  a, (#_fo_dst+1)
    ld  d, a
    ld  a, (#_fo_h)
    ld  b, a
1$:
    ld  a, (de)
    or  a, (hl)
    ld  (de), a
    inc hl
    inc de
    ld  a, (de)
    or  a, (hl)
    ld  (de), a
    inc hl
    inc de
    ld  a, (de)
    or  a, (hl)
    ld  (de), a
    inc hl
    inc de
    ld  a, (de)
    or  a, (hl)
    ld  (de), a
    inc hl
    inc de
    ld  a, e
    add a, #16
    ld  e, a
    jr  nc, 2$
    inc d
2$:
    dec b
    jr  nz, 1$
    ret
__endasm;
}

/* and:遮罩 */
void fo_and(void) __naked
{
__asm
    ld  a, (#_fo_src)
    ld  l, a
    ld  a, (#_fo_src+1)
    ld  h, a
    ld  a, (#_fo_dst)
    ld  e, a
    ld  a, (#_fo_dst+1)
    ld  d, a
    ld  a, (#_fo_h)
    ld  b, a
1$:
    ld  a, (de)
    and a, (hl)
    ld  (de), a
    inc hl
    inc de
    ld  a, (de)
    and a, (hl)
    ld  (de), a
    inc hl
    inc de
    ld  a, (de)
    and a, (hl)
    ld  (de), a
    inc hl
    inc de
    ld  a, (de)
    and a, (hl)
    ld  (de), a
    inc hl
    inc de
    ld  a, e
    add a, #16
    ld  e, a
    jr  nc, 2$
    inc d
2$:
    dec b
    jr  nz, 1$
    ret
__endasm;
}

/* 通用整塊拷貝(捲動平移用)。fo_len = 字節數(>0)。
 * fwd:fo_dst/fo_src = 起點,升序(dst<src 的重疊左移安全)
 * bwd:fo_dst/fo_src = 「最後一字節」,降序(dst>src 的重疊右移安全)
 * 32B 展開 ≈6.3 cy/B(舊逐字節版 13 cy/B,捲動子步省 ~77 線,
 * 2026-07-15 行走提速)。展開塊內順序不變,重疊安全性同舊版。 */
uint16_t fo_len;
uint8_t fo_i;               /* 尾巴字節數(內部) */

void fo_copy_fwd(void) __naked
{
__asm
    ld  a, (#_fo_src)
    ld  l, a
    ld  a, (#_fo_src+1)
    ld  h, a
    ld  a, (#_fo_dst)
    ld  e, a
    ld  a, (#_fo_dst+1)
    ld  d, a
    ld  a, (#_fo_len)
    ld  c, a
    ld  a, (#_fo_len+1)
    ld  b, a
    ld  a, c
    and a, #31
    ld  (#_fo_i), a         ; 尾巴 = len % 32
    .rept 5
    srl b
    rr  c
    .endm                   ; bc = len / 32
1$:
    ld  a, b
    or  a, c
    jr  z, 3$
    .rept 32
    ld  a, (hl+)
    ld  (de), a
    inc de
    .endm
    dec bc
    jr  1$
3$:
    ld  a, (#_fo_i)
    or  a, a
    ret z
    ld  b, a
4$:
    ld  a, (hl+)
    ld  (de), a
    inc de
    dec b
    jr  nz, 4$
    ret
__endasm;
}

void fo_copy_bwd(void) __naked
{
__asm
    ld  a, (#_fo_src)
    ld  l, a
    ld  a, (#_fo_src+1)
    ld  h, a
    ld  a, (#_fo_dst)
    ld  e, a
    ld  a, (#_fo_dst+1)
    ld  d, a
    ld  a, (#_fo_len)
    ld  c, a
    ld  a, (#_fo_len+1)
    ld  b, a
    ld  a, c
    and a, #31
    ld  (#_fo_i), a
    .rept 5
    srl b
    rr  c
    .endm
1$:
    ld  a, b
    or  a, c
    jr  z, 3$
    .rept 32
    ld  a, (hl-)
    ld  (de), a
    dec de
    .endm
    dec bc
    jr  1$
3$:
    ld  a, (#_fo_i)
    or  a, a
    ret z
    ld  b, a
4$:
    ld  a, (hl-)
    ld  (de), a
    dec de
    dec b
    jr  nz, 4$
    ret
__endasm;
}

/* 棧彈跳大拷貝:pop 讀(3cy/2B)+ ld (hl+) 寫 ≈4.6 cy/B。
 * fo_h = 320B 塊數(sync 2880B = 9)。SP 挪用期間必須關中斷
 * (ISR 會壓進源緩衝);每 320B(≈6.6 線)開一拍放行 VBL,
 * 翻面 ISR 有出窗保護(fb.c),延遲喚醒無撕裂之虞。 */
uint16_t fo_sp;             /* 真 SP 暫存 */
uint16_t fo_sp2;            /* 源進度暫存 */

void fo_copy_pop(void) __naked
{
__asm
    ld  (#_fo_sp), sp
    di
    ld  a, (#_fo_src)
    ld  l, a
    ld  a, (#_fo_src+1)
    ld  h, a
    ld  sp, hl              ; sp = 源
    ld  a, (#_fo_dst)
    ld  l, a
    ld  a, (#_fo_dst+1)
    ld  h, a                ; hl = 目標
    ld  a, (#_fo_h)
    ld  (#_fo_i), a
2$:                         ; 每塊 10×32B
    ld  b, #10
1$:
    .rept 16
    pop de
    ld  a, e
    ld  (hl+), a
    ld  a, d
    ld  (hl+), a
    .endm
    dec b
    jr  nz, 1$
    ld  d, h                ; --- 塊間隙:換回真 SP,放行中斷一拍 ---
    ld  e, l
    ld  (#_fo_sp2), sp
    ld  a, (#_fo_sp)
    ld  l, a
    ld  a, (#_fo_sp+1)
    ld  h, a
    ld  sp, hl
    ei
    nop
    di
    ld  a, (#_fo_sp2)
    ld  l, a
    ld  a, (#_fo_sp2+1)
    ld  h, a
    ld  sp, hl
    ld  h, d
    ld  l, e
    ld  a, (#_fo_i)
    dec a
    ld  (#_fo_i), a
    jr  nz, 2$
    ld  a, (#_fo_sp)        ; 還原真 SP
    ld  l, a
    ld  a, (#_fo_sp+1)
    ld  h, a
    ld  sp, hl
    ei
    ret
__endasm;
}

/* 主角底圖快照:scroll_buf(行距 20,每行 4 字節)→ 連續緩衝。
 * fo_src = scroll_buf 內左上角,fo_dst = 連續目標,fo_h = 行數。
 * (復原方向 = fo_copy:連續 → 行距 20) */
void fo_save(void) __naked
{
__asm
    ld  a, (#_fo_src)
    ld  l, a
    ld  a, (#_fo_src+1)
    ld  h, a
    ld  a, (#_fo_dst)
    ld  e, a
    ld  a, (#_fo_dst+1)
    ld  d, a
    ld  a, (#_fo_h)
    ld  b, a
1$:
    ld  a, (hl+)
    ld  (de), a
    inc de
    ld  a, (hl+)
    ld  (de), a
    inc de
    ld  a, (hl+)
    ld  (de), a
    inc de
    ld  a, (hl+)
    ld  (de), a
    inc de
    ld  a, l            ; hl += 16(下一行)
    add a, #16
    ld  l, a
    jr  nc, 2$
    inc h
2$:
    dec b
    jr  nz, 1$
    ret
__endasm;
}

/* 大塊直拷(16B 顆粒):fo_src→fo_dst,fo_h = 16B 單位數(≤255)。
 * 內層 16 字節展開,≈6.25 cy/B(memcpy ≈8.3);sync 2880B 省 ~26 線。 */
void fo_copy16(void) __naked
{
__asm
    ld  a, (#_fo_src)
    ld  l, a
    ld  a, (#_fo_src+1)
    ld  h, a
    ld  a, (#_fo_dst)
    ld  e, a
    ld  a, (#_fo_dst+1)
    ld  d, a
    ld  a, (#_fo_h)
    ld  b, a
1$:
    .rept 16
    ld  a, (hl+)
    ld  (de), a
    inc de
    .endm
    dec b
    jr  nz, 1$
    ret
__endasm;
}

/* fb(1bpp)→ stage(2bpp,雙平面同值):5 個 tile 行 = 100 tiles */
const uint8_t *cvt_src;     /* fb 起點(tile 行首) */
uint8_t *cvt_dst;           /* stage */
uint8_t cvt_i;              /* 內部計數 */
uint8_t cvt_j;
uint8_t cvt_n;
uint8_t cvt_off;

void cvt_rows5(void) __naked
{
__asm
    ld  a, (#_cvt_src)
    ld  l, a
    ld  a, (#_cvt_src+1)
    ld  h, a
    ld  a, (#_cvt_dst)
    ld  e, a
    ld  a, (#_cvt_dst+1)
    ld  d, a
    ld  bc, #20             ; 行距
    ld  a, #5
    ld  (#_cvt_i), a
3$:                         ; 每個 tile 行
    ld  a, #20
    ld  (#_cvt_j), a
4$:                         ; 每個 tile:8 行,每行 1 源字節 → 2 目標字節
    ld  a, (hl)
    ld  (de), a
    inc de
    ld  (de), a
    inc de
    add hl, bc
    ld  a, (hl)
    ld  (de), a
    inc de
    ld  (de), a
    inc de
    add hl, bc
    ld  a, (hl)
    ld  (de), a
    inc de
    ld  (de), a
    inc de
    add hl, bc
    ld  a, (hl)
    ld  (de), a
    inc de
    ld  (de), a
    inc de
    add hl, bc
    ld  a, (hl)
    ld  (de), a
    inc de
    ld  (de), a
    inc de
    add hl, bc
    ld  a, (hl)
    ld  (de), a
    inc de
    ld  (de), a
    inc de
    add hl, bc
    ld  a, (hl)
    ld  (de), a
    inc de
    ld  (de), a
    inc de
    add hl, bc
    ld  a, (hl)
    ld  (de), a
    inc de
    ld  (de), a
    inc de
                            ; hl 現在 = 列底(base+7*20);回到下一 tile 列首:-139
    ld  a, l
    add a, #0x75            ; -139 = 0xFF75
    ld  l, a
    ld  a, h
    adc a, #0xFF
    ld  h, a
    ld  a, (#_cvt_j)
    dec a
    ld  (#_cvt_j), a
    jr  nz, 4$
                            ; 20 個 tile 完:hl = 行首+20 → +140 到下一 tile 行
    ld  a, l
    add a, #140
    ld  l, a
    jr  nc, 5$
    inc h
5$:
    ld  a, (#_cvt_i)
    dec a
    ld  (#_cvt_i), a
    jr  nz, 3$
    ret
__endasm;
}

/* Copy n tile rows from a 1bpp framebuffer into byte 0 or byte 1 of
 * interleaved 2bpp tile rows. */
void cvt_plane_rows(void) __naked
{
__asm
    ld  a, (#_cvt_src)
    ld  l, a
    ld  a, (#_cvt_src+1)
    ld  h, a
    ld  a, (#_cvt_dst)
    ld  e, a
    ld  a, (#_cvt_dst+1)
    ld  d, a
    ld  a, (#_cvt_off)
    add a, e
    ld  e, a
    jr  nc, 10$
    inc d
10$:
    ld  bc, #20
    ld  a, (#_cvt_n)
    ld  (#_cvt_i), a
11$:
    ld  a, #20
    ld  (#_cvt_j), a
12$:
    ld  a, (hl)
    ld  (de), a
    inc de
    inc de
    add hl, bc
    ld  a, (hl)
    ld  (de), a
    inc de
    inc de
    add hl, bc
    ld  a, (hl)
    ld  (de), a
    inc de
    inc de
    add hl, bc
    ld  a, (hl)
    ld  (de), a
    inc de
    inc de
    add hl, bc
    ld  a, (hl)
    ld  (de), a
    inc de
    inc de
    add hl, bc
    ld  a, (hl)
    ld  (de), a
    inc de
    inc de
    add hl, bc
    ld  a, (hl)
    ld  (de), a
    inc de
    inc de
    add hl, bc
    ld  a, (hl)
    ld  (de), a
    inc de
    inc de
    ld  a, l
    add a, #0x75
    ld  l, a
    ld  a, h
    adc a, #0xFF
    ld  h, a
    ld  a, (#_cvt_j)
    dec a
    ld  (#_cvt_j), a
    jr  nz, 12$
    ld  a, l
    add a, #140
    ld  l, a
    jr  nc, 13$
    inc h
13$:
    ld  a, (#_cvt_i)
    dec a
    ld  (#_cvt_i), a
    jr  nz, 11$
    ret
__endasm;
}
