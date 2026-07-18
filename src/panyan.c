/* panyan.c - 遊戲山莊小遊戲(逐例程移植原版 panyan.s,bank 27)
 *
 * 對照表:lian_dodge(跳舞毯)/lian_unarmed(投篮球)/game_box/
 *   draw_basket_box/move_basket/shoot_target/little_target/check_score/
 *   show_score/random_delay → 同名或同構
 * 入口 panyan_dance/panyan_ball 由 pyh_quest.c(item 201)呼叫;
 * hill/選路訊息在 bank 28 端顯示(跨 bank 資料不可讀)。
 * 鍵對應:原版 F1/CR→A、ESC→B、方向鍵→十字鍵;原版 delay 單位≈4ms
 * →幀數 = 單位/4(delay_160_ms=40 單位=10 幀)。
 * 繪製:圖塊經 lee_block 進 scroll_buf,線框用本地 sb_* 直寫 scroll_buf;
 * SCORE/TOP 文字是 fb 覆蓋層,每次 present 重畫。
 */
#pragma bank 27
#include <gb/gb.h>
#include <string.h>
#include "game.h"
#include "save.h"
#include "text.h"       /* gfx_scratch(小遊戲期間 UI 複用區全閒置) */
#include "ui.h"
#include "fb.h"
#include "blit.h"
#include "font.h"
#include "res.h"
#include "player_res.h"
#include "player_girl_res.h"
#include "panyan.h"
#include "panyan_data.h"

#define DODGE_BONUS 3
#define PARRY_BONUS 10
#define BALL_Y      30
/* 跳舞遊戲整框下移量:原版 80px 框置中於 144 螢幕 →(144-80)/2=32。
 * 邊框/鍵盤/角色/上方提示箭頭全部 +DANCE_TOP;SCORE/TOP 覆蓋層不動。 */
#define DANCE_TOP   32
#define SLOT_Y      (DANCE_TOP + 7)     /* 上方提示箭頭列 y */
#define MAN_X       104     /* 13*8 */
#define MAN_Y       (DANCE_TOP + 30)

static uint16_t py_score, py_top;
#define imgbuf  (gfx_scratch + 192)     /* 192B(tplbuf 區,期間閒置) */
#define inv_buf (gfx_scratch + 0)       /* 32B(look_msg 區,期間閒置) */

/* ---- scroll_buf 線框(等效 line_draw/squre_draw 的本用例) ---- */
static void sb_hline(uint8_t x0, uint8_t x1, uint8_t y)
{
    uint8_t x;
    uint8_t *row = &scroll_buf[(uint16_t)y * 20];

    for (x = x0; x <= x1; x++)
        row[x >> 3] |= 0x80 >> (x & 7);
}

static void sb_vline(uint8_t x, uint8_t y0, uint8_t y1)
{
    uint8_t y;

    for (y = y0; y <= y1; y++)
        scroll_buf[(uint16_t)y * 20 + (x >> 3)] |= 0x80 >> (x & 7);
}

static void sb_rect(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    sb_hline(x0, x1, y0);
    sb_hline(x0, x1, y1);
    sb_vline(x0, y0, y1);
    sb_vline(x1, y0, y1);
}

/* ---- 主角圖(性別選幀集,等效 get_player_img 序號→幀) ---- */
static void py_player(uint8_t frame, uint8_t x, uint8_t y, uint8_t h)
{
    res_read((hero.man_gender == 1) ? &player_girl_res : &player_res,
             (uint16_t)frame * 192, imgbuf, 192);
    lee_block(x, y, imgbuf, 4, h, LCMD_PRINT);
}

/* ---- SCORE/TOP 覆蓋層 + 呈現 ---- */
static void py_digits(uint8_t x, uint8_t y, uint16_t v)
{
    uint16_t d = 10000;
    uint8_t i;

    for (i = 0; i < 5; i++) {
        font_draw_ascii(x, y, (uint8_t)('0' + (v / d) % 10));
        x += 8;
        d /= 10;
    }
}

static void py_present(void)
{
    scroll_to_lcd();
    font_draw_text(8, 2, (const uint8_t *)"SCORE:");
    py_digits(56, 2, py_score);
    if (py_score > py_top)
        py_top = py_score;              /* 等效 show_top 順手刷新 */
    font_draw_text(8, 15, (const uint8_t *)" TOP :");
    py_digits(56, 15, py_top);
    fb_flush();
}

/* ---- 幀延時 + 取窗口內最後一鍵(等效 key 變數語義) ---- */
static uint8_t py_delay_key(uint8_t frames)
{
    uint8_t k = 0, j, prev = 0xFF;

    while (frames--) {
        vsync();
        j = joypad();
        if (j && j != prev) {
            if (j & J_B)          k = K_ESC;
            else if (j & J_A)     k = K_CR;
            else if (j & J_LEFT)  k = K_LEFT;
            else if (j & J_RIGHT) k = K_RIGHT;
            else if (j & J_UP)    k = K_UP;
            else if (j & J_DOWN)  k = K_DOWN;
        }
        prev = j;
    }
    return k;
}

/* ================= 跳舞毯(lian_dodge) ================= */

static const uint8_t *const py_arrows[4] = {
    py_up, py_down, py_right, py_left,      /* flag 0-3 = 原版記憶體序 */
};
/* flag → 提示槽 x(BEGIN2/3/4/1 = 104/120/136/88,y=7) */
static const uint8_t py_slot_x[4] = { 104, 120, 136, 88 };

/* 鐵律:lee_block 是 bank25 BANKED,執行期以 bank25 窗解引用圖形
 * 指針 → bank27 const 點陣必須先落 WRAM 再傳(反白箭頭因先過
 * inv_buf 倖免,其餘全花;2026-07-12 BGB/PyBoy 實測根治)。 */
static void py_arrow(uint8_t x, uint8_t y, const uint8_t *img, uint8_t inv)
{
    uint8_t i;

    for (i = 0; i < 32; i++)
        inv_buf[i] = inv ? (uint8_t)~img[i] : img[i];
    lee_block(x, y, inv_buf, 2, 16, LCMD_PRINT);
}

static void py_game_box(void)
{
    memset(scroll_buf, 0, 20 * SCROLL_H);
    sb_rect(0, DANCE_TOP + 0, 159, DANCE_TOP + 79);
    sb_vline(78, DANCE_TOP + 1, DANCE_TOP + 79);      /* 左右分界 */
    sb_hline(79, 158, DANCE_TOP + 25);                /* 右上橫線 */
    sb_hline(90, 150, DANCE_TOP + 70);                /* 小人立足線 */
    sb_rect(36, DANCE_TOP + 44, 43, DANCE_TOP + 51);  /* 四箭頭中央小方塊 */
}

void panyan_dance(void) BANKED
{
    uint8_t flag = 0xFF, r, k;
    uint8_t level = 200;                    /* 提示窗口(單位≈4ms) */
    uint8_t cur_slot = 3;                   /* BEGIN1(=flag3 槽)*/

    py_score = 0;
    py_top = hero.top_dance;

    py_game_box();
    py_arrow(32, DANCE_TOP + 24, py_up, 0);     /* 左區四向鍵盤 */
    py_arrow(48, DANCE_TOP + 40, py_right, 0);
    py_arrow(16, DANCE_TOP + 40, py_left, 0);
    py_arrow(32, DANCE_TOP + 56, py_down, 0);
    py_player(3, MAN_X, MAN_Y, 48);         /* 原版 get_player_img(0) */
    py_present();

    for (;;) {
        /* 隨機方向(不與上次相同) */
        do {
            r = (uint8_t)random_it(4);
        } while (r == flag);
        flag = r;

        /* 清舊提示,畫新提示(反白) */
        py_arrow(py_slot_x[cur_slot], SLOT_Y, py_arrows[cur_slot], 0);
        {   /* 以空白覆蓋:直接清 16×16 區 */
            uint8_t yy;
            for (yy = 0; yy < 16; yy++)
                memset(&scroll_buf[(uint16_t)(SLOT_Y + yy) * 20
                                   + (py_slot_x[cur_slot] >> 3)], 0, 2);
        }
        cur_slot = flag;
        py_arrow(py_slot_x[flag], SLOT_Y, py_arrows[flag], 1);
        py_present();

        k = py_delay_key(level >> 2);       /* 單位/4 = 幀 */
        if (k == K_ESC)
            break;
        if (k == K_UP)
            r = 0;
        else if (k == K_DOWN)
            r = 1;
        else if (k == K_RIGHT)
            r = 2;
        else if (k == K_LEFT)
            r = 3;
        else
            continue;                       /* 沒按/非方向鍵:下一題 */
        if (r != flag)
            break;                          /* 按錯=結束(原版 check_score) */

        if (level >= 80)
            level -= 2;                     /* 加速 */
        py_score += DODGE_BONUS;

        /* 對應側箭頭反白 + 主角擺姿。按哪個方向角色就朝哪個方向
         * (frame=way*3+status,way 0-3=下/左/右/上):
         * 上→朝上幀9、下→朝下幀0、右→朝右幀6、左→朝左幀3。
         * (原版 {0,3,10,7} 是照抄 NC2000 幀號,GBC 幀排列不同故錯位) */
        {
            static const uint8_t pose[4] = { 9, 0, 6, 3 };   /* 上下右左 */
            static const uint8_t ax[4] = { 32, 32, 48, 16 };
            static const uint8_t ay[4] = { DANCE_TOP + 24, DANCE_TOP + 56,
                                           DANCE_TOP + 40, DANCE_TOP + 40 };

            py_arrow(ax[r], ay[r], py_arrows[r], 1);
            py_player(pose[r], MAN_X, MAN_Y, 48);
            py_present();
            py_delay_key(20);               /* 2×160ms */
            py_arrow(ax[r], ay[r], py_arrows[r], 0);
            py_present();
        }
    }
    hero.top_dance = py_top;                /* 等效 proc_quit */
}

/* ================= 投篮球(lian_unarmed) ================= */

static void py_ball_at(uint8_t x, uint8_t y, uint8_t draw)
{
    if (draw) {
        memcpy(inv_buf, py_ball, 6);    /* bank27 const → WRAM(見 py_arrow) */
        lee_block(x, y, inv_buf, 1, 6, LCMD_OR);
    }
    else {
        uint8_t yy;
        for (yy = 0; yy < 6; yy++) {        /* 6×6 清除(basket_blank) */
            uint8_t *row = &scroll_buf[(uint16_t)(y + yy) * 20];
            uint8_t xx;
            for (xx = 0; xx < 6; xx++)
                row[(x + xx) >> 3] &= (uint8_t)~(0x80 >> ((x + xx) & 7));
        }
    }
}

static void py_basket_box(void)
{
    memset(scroll_buf, 0, 20 * SCROLL_H);
    sb_rect(0, 0, 159, 79);

    /* 籃框(draw_lankuang) */
    sb_vline(150, 25, 75);
    sb_hline(135, 150, 27);
    sb_hline(135, 150, 40);
    sb_vline(135, 20, 45);
    sb_hline(125, 135, 38);
    sb_vline(127, 38, 44);
    sb_vline(132, 38, 44);
    sb_hline(127, 132, 43);

    py_player(11, 30, 43, 32);              /* 原版 get_player_img(8) 上 32 列 */

    sb_hline(20, 155, 75);                  /* 下方場地線 */
    sb_rect(10, BALL_Y - 1, 50, BALL_Y + 6);/* 水平尺 */
    sb_vline(27, BALL_Y + 6, BALL_Y + 7);   /* 命中刻度 */
    sb_vline(32, BALL_Y + 6, BALL_Y + 7);

    py_ball_at(60, 69, 1);                  /* 待投球 */
    py_ball_at(27, BALL_Y, 1);              /* 尺上球 */
    py_present();
}

/* 等效 random_delay:0→5 1→40 2→20 其他→40 單位(=幀 1/10/5/10) */
static uint8_t py_rand_frames(void)
{
    uint8_t r = (uint8_t)random_it(4);

    if (r == 0)
        return 1;
    if (r == 2)
        return 5;
    return 10;
}

/* 球左右滑動,A 停 → 回停止 x;B 退 → 0xFF */
static uint8_t py_move_ball(void)
{
    uint8_t x = 27, dir = 0, k;             /* dir 0=左 1=右 */

    for (;;) {
        if (dir == 0 && x < 12)
            dir = 1;
        if (dir == 1 && x >= 44)
            dir = 0;
        py_ball_at(x, BALL_Y, 0);
        x = dir ? (uint8_t)(x + 1) : (uint8_t)(x - 1);
        py_ball_at(x, BALL_Y, 1);
        py_present();
        k = py_delay_key(py_rand_frames());
        if (k == K_CR)
            return x;
        if (k == K_ESC)
            return 0xFF;
    }
}

void panyan_ball(void) BANKED
{
    uint8_t x, i, lives = 7;

    py_score = 0;
    py_top = hero.top_ball;

    for (;;) {
        py_basket_box();
        for (;;) {                          /* basket_wait_key */
            x = wait_key();
            if (x == K_ESC)
                goto out;
            if (x == K_CR)
                break;
        }
        x = py_move_ball();
        if (x == 0xFF)
            goto out;

        if (x >= 26 && x < 29) {            /* 命中窗口 */
            py_ball_at(60, 69, 0);          /* 待投球起飛 */
            for (i = 0; i < PY_ROUTE_N; i++) {
                uint8_t bx = py_route[(uint16_t)i * 2];
                uint8_t by = py_route[(uint16_t)i * 2 + 1];
                py_ball_at(bx, by, 1);
                py_present();
                py_delay_key(3);            /* delay 10 單位 */
                py_ball_at(bx, by, 0);
            }
            py_score += PARRY_BONUS;
        } else {
            py_delay_key(60);               /* little_target:悶 1 秒 */
            if (--lives == 0)
                goto out;
        }
        py_ball_at(x, BALL_Y, 0);           /* 清尺上停球 */
        py_ball_at(127, 38, 0);             /* 原版怪癖:磨掉一角籃框 */
        py_present();
    }
out:
    hero.top_ball = py_top;
}
