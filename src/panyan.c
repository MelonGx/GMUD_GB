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
#pragma bank 24
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
/* 原版答對姿勢固定等兩次 40/256 秒，共 5/16 秒。
 * GBC 一幀 70224/4194304 秒，精確換算為
 *   (5/16) / (70224/4194304) = 81920/4389
 *   = 18 + 2918/4389 幀。
 * 用餘數累積分配 18/19 幀；任意前綴的累積等待都不超過原版，
 * 191 次（573 分）共 3564 幀，比理論值快約 16.5ms。 */
static uint16_t dance_pose_phase;

static uint8_t dance_pose_frames(void)
{
    uint8_t frames = 18;

    dance_pose_phase += 2918;
    if (dance_pose_phase >= 4389) {
        dance_pose_phase -= 4389;
        frames++;
    }
    return frames;
}
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

/* SCORE/TOP 是畫進 fb 的覆蓋層,而 scroll_to_lcd 每步都用 scroll_buf 整片
 * 蓋掉 fb,所以原本每次 present 都得重畫 22 個字模——每個字模一次
 * SWITCH_ROM + memcpy + blit,實測合計約 1.5 幀,佔滾球一步 9.5 幀的 1/6。
 * 分數只有進球才變,所以改成:畫完把 y 0..PY_SCORE_ROWS-1 抄回 scroll_buf
 * 併進場景,之後 scroll_to_lcd 自動帶著走,動畫步只要 scroll+flush。
 * 這一段只有外框和分數:球在 BALL_Y=30 以下,跳舞毯場地在 DANCE_TOP=32
 * 以下,都不會被凍住。 */
#define PY_SCORE_ROWS 28

/* 分數併進 scroll_buf 之後多出一個副作用:下一次 py_present_score 的
 * scroll_to_lcd 會把「上一次的字」原樣帶回 fb,而 font_draw_* 是 OR 疊加
 * (font.c rep_mode 預設 0),新舊數字疊在同一格就糊成黑塊
 * (2026-07-31 用戶回報跳舞毯 SCORE 欄 garbage;個位 0|3|6|9… 最先爛)。
 * 投篮球每輪開頭 py_basket_box() 先 memset scroll_buf 才畫分數,fb 裡的
 * 字格本來就是空的,所以只有跳舞毯發作。
 *
 * 修法是把「畫字前字格是空的」這個前提補回來——畫之前清掉字格。範圍
 * x 8..95(byte 1..11)、y 2..PY_SCORE_ROWS-1,這塊在兩個遊戲裡都只有
 * 分數文字:跳舞毯場地全在 y>=DANCE_TOP(32);投篮球外框只佔 byte 0/19、
 * 籃框 byte>=15、水平尺與兩顆球 y>=29。
 * 不用 font_draw_text_replace 是因為 SCORE(y=2)與 TOP(y=15)的 16px
 * 字格重疊 3 列,覆寫模式畫 TOP 會啃掉 SCORE 的下緣。 */
#define PY_TXT_B0     1                 /* x=8 → byte 1 */
#define PY_TXT_NB     11                /* 11 個半形格 × 8px = byte 1..11 */

static void py_draw_score(void)         /* 22 個字模,約 1.5 幀 */
{
    uint8_t y;

    for (y = 2; y < PY_SCORE_ROWS; y++)
        memset(&fb[(uint16_t)y * FB_STRIDE + PY_TXT_B0], 0, PY_TXT_NB);

    font_draw_text(8, 2, (const uint8_t *)"SCORE:");
    py_digits(56, 2, py_score);
    if (py_score > py_top)
        py_top = py_score;              /* 等效 show_top 順手刷新 */
    font_draw_text(8, 15, (const uint8_t *)" TOP :");
    py_digits(56, 15, py_top);
}

static void py_present(void)            /* 動畫步:分數已烘進 scroll_buf */
{
    scroll_to_lcd();
    fb_flush();
}

/* 分數烘進 scroll_buf 之後多出一個危險:py_route 有 32 個點落在 y<=27
 * (x 87..117),球是 6×6,而 TOP 的數字排到 x 94、佔 y 15..26——飛行每步
 * 結尾的 6×6 清框會連字一起挖掉,而且挖了就留著,直到下一次重畫。
 * 每步重畫分數要 4.6 幀(22 個字模各一次 SWITCH_ROM),飛行會從 3.3 秒
 * 掉到 6.2 秒。改成:烘完把會被穿過的那幾列(20..27)原樣留一份,清框後
 * 照抄回去——一次 160 位元組 memcpy,成本可忽略。 */
#define PY_GUARD_Y0  20                 /* py_route 的 y 最低 20 */
#define PY_GUARD_H   (PY_SCORE_ROWS - PY_GUARD_Y0)
#define py_guard     (gfx_scratch + 384)    /* 8×20=160B;imgbuf 只到 383 */

static void py_present_score(void)      /* 重建場景或分數變動時:併回場景 */
{
    scroll_to_lcd();
    py_draw_score();
    memcpy(scroll_buf, fb, 20 * PY_SCORE_ROWS);
    memcpy(py_guard, scroll_buf + 20 * PY_GUARD_Y0, 20 * PY_GUARD_H);
    fb_flush();
}

/* ---- 幀延時 + 取窗口內最後一鍵(等效 key 變數語義) ----
 * 陷阱:frames==0 時整個迴圈不執行,連一次 joypad() 都不取樣,回傳恆 0。
 * 純動畫節拍(py_shot_wait)傳 0 沒差,但任何要收玩家按鍵的窗口都必須
 * 傳 >=1(見 py_slide_wait 的 +1)。 */
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

/* 跳舞專用:提示出現後第一個新鍵立即回傳。通用 py_delay_key 必須跑滿
 * 動畫/投籃窗口,不能直接改；舊跳舞共用它時,最壞會把已讀到的方向鍵
 * 壓到整個 46..18 幀反應窗結束才處理。prev=FF 讓 fb_flush 期間已按下
 * 且仍按住的鍵在首幀也能被接住。 */
static uint8_t py_dance_key(uint8_t frames)
{
    uint8_t j, prev = 0xFF;

    while (frames--) {
        vsync();
        j = joypad();
        if (j && j != prev) {
            if (j & J_B)          return K_ESC;
            if (j & J_LEFT)       return K_LEFT;
            if (j & J_RIGHT)      return K_RIGHT;
            if (j & J_UP)         return K_UP;
            if (j & J_DOWN)       return K_DOWN;
        }
        prev = j;
    }
    return 0;
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

/* 回傳 1 = 最高分達 LINGBO_SCORE(凌波微步門檻,發放在 pyh_quest.c)。
 * 判定看 top_dance 而非本局分數,結算在離開遊戲時:達標那一局結束即得;
 * 若最高分是 yobdc 作弊改的,則隨便玩一局、退出時結算(用戶定案)。 */
uint8_t panyan_dance(void) BANKED
{
    uint8_t flag = 0xFF, r, k;
    uint8_t level = 200;                    /* 提示窗口(單位≈4ms) */
    uint8_t cur_slot = 3;                   /* BEGIN1(=flag3 槽)*/

    py_score = 0;
    py_top = hero.top_dance;
    dance_pose_phase = 0;

    py_game_box();
    py_arrow(32, DANCE_TOP + 24, py_up, 0);     /* 左區四向鍵盤 */
    py_arrow(48, DANCE_TOP + 40, py_right, 0);
    py_arrow(16, DANCE_TOP + 40, py_left, 0);
    py_arrow(32, DANCE_TOP + 56, py_down, 0);
    py_player(3, MAN_X, MAN_Y, 48);         /* 原版 get_player_img(0) */
    py_present_score();

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

        /* 反應窗口:原版 level 的單位是 tick = 1/256 秒,一幀 1/59.73 秒,
         * 換算 幀 = level × 59.73/256 = level × 60/257(整數近似,誤差 <0.1%)。
         * 舊寫法 level>>2 等於 ×0.25,比原版慢 7%(level=200:50 幀 vs 46.7)。 */
        k = py_dance_key((uint8_t)((uint16_t)level * 60 / 257));
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
            py_present_score();             /* 上一行剛加了 DODGE_BONUS */
            py_delay_key(dance_pose_frames()); /* 精確平均 2×40/256 秒 */
            py_arrow(ax[r], ay[r], py_arrows[r], 0);
            py_present();
        }
    }
    hero.top_dance = py_top;                /* 等效 proc_quit */
    return (py_top >= LINGBO_SCORE) ? 1 : 0;
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
    py_present_score();                     /* memset 過 scroll_buf,要重畫 */
}

/* 投篮球的節奏。原版 panyan.s 的 delay 單位是 1 tick = 1/256 秒 = 3.906ms:
 *   飛行 shoot_target 每點 10 tick × PY_ROUTE_N(83)點 = 3.24 秒
 *   悶場 little_target        240 tick               = 938ms
 *   滾球 random_delay 5/40/20/40 tick,均勻 → 平均 26.25 tick = 102.5ms/px
 *
 * GBC 的地板是 py_present:fb.c 沒有局部 flush(flush_dirty 只判斷「有沒有
 * 髒」就整屏 flush_all),一次 present 實測 4 幀 = 67ms。所以:
 *   飛行 每點都 present 就是 83×3.8 = 315 幀 = 5.3 秒,比原版慢 63%。改成
 *        軌跡每 py_shot_step()=2 點取樣一次 → 42 步,再把整段排在絕對幀網格
 *        上(見 PY_FLIGHT_FRAMES)。舊寫法是每步固定補 1 幀,總長被繪製成本
 *        牽著走,實測 201 幀 = 3.365 秒,比原版慢 3.8%(2026-08-02 改)。
 *   悶場 純等待沒有繪製成本,照原版 240 tick 給滿(2026-07-29 曾誤 >>3 成
 *        117ms,比原版快 8 倍,用戶基準是「EXT 跟原版一樣」,還原)。
 *   滾球 見 py_slide_wait()。 */
#define py_shot_step()   2
#define py_miss_wait()   56             /* 240 tick × 3.906ms = 938ms */

/* 飛行總長:83 點 × 10 tick = 830 tick,一 tick = 1/256 s、一幀 =
 * 70224/4194304 s → 830 × 1024/4389 = 193.65 幀,取 194(+0.18%)。
 * 逐步用餘數把 194 幀均分到 42 步(每步 4 幀 + 26/42),用絕對截止時間排,
 * 某一步畫慢了下一步會自動追回,總長不隨繪製成本漂移。 */
#define PY_FLIGHT_FRAMES 194
#define PY_FLIGHT_STEPS  ((PY_ROUTE_N + py_shot_step() - 1) / py_shot_step())

/* 尺上球滑動。原版 random_delay 依 random_it(4) 給 5/40/20/40 tick,
 * 即 1.2/9.6/4.8/9.6 幀,平均 26.25 tick = 102.5ms/px = 6.12 幀。
 *
 * GBC 這邊一步的固定成本(scroll_to_lcd + fb_flush)實測 3.9 幀,加上
 * 取鍵窗口至少 1 幀 → 地板 4.9 幀 = 82ms。原版最快那檔(1.2 幀)構不到,
 * 所以做不到逐檔照抄;改成保留「快/慢」的紋理並讓平均值對上原版:
 *     r=0,2 → 等 1 幀(快,4.9 幀 = 82ms)
 *     r=1,3 → 等 3 幀(慢,6.9 幀 = 115ms)
 *     平均 5.9 幀 = 98.8ms,對原版 102.5ms 差 4%
 * 不能給 0:py_delay_key 是 while(frames--),0 幀等於一次 joypad 都不
 * 取樣,玩家按 A 會被整個窗口吞掉,球停不下來(曾實測命中一次要 359 幀)。
 * 要逐檔照抄原版,得先讓 fb.c 支援局部 flush 把那 3.9 幀壓下去。 */
static uint8_t py_slide_wait(void)
{
    return ((uint8_t)random_it(4) & 1) ? 3 : 1;
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
        k = py_delay_key(py_slide_wait());
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
            uint16_t t0 = sys_time, dl = 0;
            uint8_t rem = 0;

            py_ball_at(60, 69, 0);          /* 待投球起飛 */
            for (i = 0; i < PY_ROUTE_N; i += py_shot_step()) {
                uint8_t bx = py_route[(uint16_t)i * 2];
                uint8_t by = py_route[(uint16_t)i * 2 + 1];
                py_ball_at(bx, by, 1);
                py_present();
                dl += PY_FLIGHT_FRAMES / PY_FLIGHT_STEPS;
                rem += PY_FLIGHT_FRAMES % PY_FLIGHT_STEPS;
                if (rem >= PY_FLIGHT_STEPS) {
                    rem -= PY_FLIGHT_STEPS;
                    dl++;
                }
                while ((uint16_t)(sys_time - t0) < dl)
                    vsync();
                py_ball_at(bx, by, 0);
                if (by < PY_SCORE_ROWS)     /* 清框啃到分數帶 → 補回去 */
                    memcpy(scroll_buf + 20 * PY_GUARD_Y0, py_guard,
                           20 * PY_GUARD_H);
            }
            py_score += PARRY_BONUS;
        } else {
            py_delay_key(py_miss_wait());   /* little_target:悶 1 秒 */
            if (--lives == 0)
                goto out;
        }
        py_ball_at(x, BALL_Y, 0);           /* 清尺上停球 */
        py_ball_at(127, 38, 0);             /* 原版怪癖:磨掉一角籃框 */
        /* 這裡不能 py_present():飛行段每步是用 6x6 清框把球擦掉的,
         * 球飛過籃框時連籃框的線一起擦沒了,此刻 scroll_buf 裡的籃框是
         * 破的(桿子沒了、籃圈剩幾個點),尺框也空著。原本在這裡送一次
         * present,等於在「命中 → 分數更新」的那一瞬間把破圖亮出來
         * (2026-07-29 用戶回報「籃筐渲染出錯白屏」,build/bb2 復現)。
         * 迴圈開頭的 py_basket_box() 會 memset scroll_buf 後整屏重畫並
         * present,新分數就在那一幀出現,這裡少送一次沒有任何損失。 */
    }
out:
    hero.top_ball = py_top;
}
