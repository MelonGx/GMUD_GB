/* pyh_quest.c - 義工/釣魚(原版 pyh_quest.s)+ 物件動作(talk.s item 段)
 *
 * 對照表:saodi/tiaoshui/pichai/fishing/show_bonus/show_loop_msg/
 *   show_text/delay_show_text;talk.s do_drink/sleep/find_rope/baodian/
 *   do_suicide/look_board/show_tell_msg → 同名
 * 工錢變數 varbuf+2..+7 = 原版 game_buf+4..+9(覆蓋 quest_delay,
 * 別名行為經 task_gbuf 忠實保留)。代碼在 bank 28。
 */
#pragma bank 28
#include <gb/gb.h>
#include <string.h>
#include "game.h"
#include "save.h"
#include "text.h"
#include "task.h"
#include "task_msgs.h"
#include "ui.h"
#include "fb.h"
#include "blit.h"
#include "goods.h"
#include "font.h"
#include "skill.h"          /* add_kf/find_kf(HOME) — 凌波微步發放 */
#include "gamedata.h"       /* LINGBO_KF */
#include "fight.h"          /* final_fight(bank 27 BANKED) */
#include "panyan.h"         /* 小遊戲(bank 27 BANKED) */
#include "nf.h"             /* 擂台聯機對戰(bank 30 BANKED) */
#include "ending_data.h"    /* 三結局捲軸文本(bank 28 本地) */

#define DELAY_CONST 2               /* 每句停 2 秒 */

#define DIAOGAN_GOODS 73
#define FISH_GOODS    74
#define YULOU_GOODS   76
#define ROPE_GOODS    19
#define MEIJIU_GOODS  78
#define GLASSES_GOODS 40
#define BAODIAN_GOODS 81
#define KILLER_NPC   125
#define HANDS_ARM      9
#define NONE_PAI       0
#define XIAOYAO_PAI    7
#define DAODE_NPC     12
#define BOSS1_NPC    126
#define GOODMAN      160            /* h/gmud.h 道德閾值 */
#define BADMAN       100

extern __at(0xA370) uint8_t task_gbuf[24];      /* SRAM(task.h 同址) */
#define wk_var (task_gbuf + 4)      /* 原版 varbuf+2(工錢 3×2B) */

#define tplbuf (gfx_scratch + 192)

static void putw(uint8_t *p, const void *v)
{
    uint16_t a = (uint16_t)v;
    p[0] = (uint8_t)a;
    p[1] = (uint8_t)(a >> 8);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* 等效 show_text:格式化 → 頂框對話(等鍵) → 回地圖 */
static void show_fmt(const uint8_t *msg)
{
    format_string(msg);
    show_talk_msg();
    scroll_to_lcd();
    fb_flush();
}

/* 等效 delay_show_text:不等鍵,顯示後停 DELAY_CONST 次 delay_1_sec。
 * 義工號子受速度檔縮短(EXT=x1、ADV/BSC=x4,用戶定版 2026-07-17)
 *
 * 2026-08-02 校準:原版 pyh_quest.s:171-174 是 `jsr delay_1_sec` × 2,而
 * system.s:242 的 delay_1_sec 名字叫一秒、實際是 waittime(240)=240/256 s
 * = 937.5ms。舊值一次算 60 幀(=1.0046s)有兩重誤差:把 240/256 當成 1 秒,
 * 又把 1 秒當成 60 幀。正確是 240 tick × 1024/4389 = 55.99 → 56 幀。
 * 一段 2×56=112 幀=1.8755s,五段合計 9.377s,對原版 9.375s 差 +0.02%
 * (舊值五段 10.046s,慢 7.2%)。 */
#define DELAY_1SEC_FRAMES 56        /* 原版 delay_1_sec = 240 tick */

static void delay_show(const uint8_t *msg, uint8_t len)
{
    uint16_t t;
    uint16_t end = ((uint16_t)DELAY_CONST * DELAY_1SEC_FRAMES)
                   >> disp_shift();

    memcpy(OutBuf, msg, len);
    show_talk_msg0();
    for (t = 0; t < end; t++) {
        vsync();
        heart_beat();               /* 原版 delay_1_sec 內跑 sys_refresh */
    }
}

/* 等效 show_loop_msg:4 句勞動號子(16B 一句) */
static void show_loop_msg(const uint8_t *msgs)
{
    uint8_t i;

    for (i = 0; i < 4; i++)
        delay_show(msgs + ((uint8_t)(i << 4)), 16);
}

/* 等效 pyh_quest.s show_bonus:工錢 20 經驗 10 潛能 50 錢。
 * 婆婆任務獎勵一律不隨速度檔加倍(2026-07-17 用戶定版) */
static void work_bonus(void)
{
    uint16_t e = 20;
    uint16_t p = 10;

    delay_show(tm_over_msg, TM_OVER_MSG_LEN);
    wr16(wk_var, e);
    wr16(wk_var + 2, p);
    wr16(wk_var + 4, 50);
    hero.man_exp += e;
    hero.man_pot += p;
    hero.man_money += 50;
    memcpy(tplbuf, tm_work_bonus_msg, TM_WORK_BONUS_MSG_LEN);
    putw(tplbuf + TM_WORK_BONUS_MSG_S0, wk_var);
    putw(tplbuf + TM_WORK_BONUS_MSG_S1, wk_var + 2);
    putw(tplbuf + TM_WORK_BONUS_MSG_S2, wk_var + 4);
    show_fmt(tplbuf);
}

/* 一份勞動:精血不夠 → 手都酸軟;夠 → 扣血+號子+工錢 */
static uint8_t do_work(uint8_t cost, const uint8_t *loop_msgs)
{
    if (hero.man_hp < cost) {
        show_fmt(tm_fail_msg);
        return 0;
    }
    hero.man_hp -= cost;
    show_loop_msg(loop_msgs);
    work_bonus();
    return 0;
}

static uint8_t do_saodi(void)
{
    return do_work(30, tm_saodi_msg);
}

static uint8_t do_tiaoshui(void)
{
    return do_work(40, tm_tiaoshui_msg);
}

static uint8_t do_pichai(void)
{
    return do_work(50, tm_pichai_msg);
}

/* 等效 fishing:釣竿(手持裝備)+40 精血,50% 上鉤,要魚簍才裝得走 */
static void fishing(void)
{
    uint8_t e = hero.man_equip[HANDS_ARM];

    if (!(e & 0x80) || (e & 0x7F) != DIAOGAN_GOODS) {
        show_fmt(tm_diao_gan_msg);
        return;
    }
    if (hero.man_hp < 40) {
        show_fmt(tm_fish_hp_msg);
        return;
    }
    hero.man_hp -= 40;
    show_fmt(tm_fish_start_msg);
    if (random_it(10) >= 5) {
        show_fmt(tm_no_fish_msg);
        return;
    }
    show_fmt(tm_have_fish_msg);
    if (find_goods(YULOU_GOODS) == 0xFF) {
        show_fmt(tm_fish_escape_msg);
        return;
    }
    add_goods(FISH_GOODS);
    show_fmt(tm_fish_suc_msg);
}

/* ---- 等效 talk.s show_tell_msg:底部一行,等鍵後回地圖 ---- */
static void show_tell(void)
{
    fb_fill_rect(1, 129, 159, 15, 0);
    ui_hline(1, 159, 129);
    ui_hline(1, 159, 143);
    ui_vline(1, 129, 143);
    ui_vline(159, 129, 143);
    ss_ptr = OutBuf;
    show_one_line(1, 131);
    fb_flush();
    wait_key();
    show_player();                  /* 等效 refresh_scroll */
    fb_flush();
}

static void tell_fmt(const uint8_t *msg)
{
    format_string(msg);
    show_tell();
}

/* 等效 do_drink(水井) */
static void do_drink(void)
{
    if (hero.man_water >= hero.man_maxwater) {
        tell_fmt(tm_drink_fail_msg);
        return;
    }
    hero.man_water += 20;
    tell_fmt(tm_drink_succ_msg);
}

/* 等效 sleep(床):要女兒紅,睡一覺過三個月 */
static void do_sleep(void)
{
    uint8_t x = find_goods(MEIJIU_GOODS);

    if (x == 0xFF)
        return;
    hero.man_goods[x + 1]--;
    tell_fmt(tm_sleep_msg);
    hero.mud_age += 3600UL * 3;
}

/* 等效 find_rope(草蓆):身上沒繩索才撿得到 */
static void find_rope(void)
{
    if (find_goods(ROPE_GOODS) != 0xFF)
        return;
    tell_fmt(tm_find_rope_msg);
    add_goods(ROPE_GOODS);
}

static void baodian(void)
{
    if (hero.man_gender == 1)
        return;
    if (hero.man_daode & 0x80)
        return;
    if (hero.man_age >= 18)
        return;
    if (find_goods(GLASSES_GOODS | 0x80) == 0xFF)
        return;
    tell_fmt(tm_find_bao_msg);
    add_goods(BAODIAN_GOODS);
}

/* 等效 do_suicide:有繩索 → 確認後刪檔;1=已刪檔退出 */
static uint8_t do_suicide(void)
{
    if (find_goods(ROPE_GOODS) == 0xFF) {
        format_string(tm_rope_msg);
        message_box(6, 3, 6 + 12 * 12, 3 + 12 * 5);
        scroll_to_lcd();
        fb_flush();
        return 0;
    }
    format_string(tm_suicide_msg);
    if (!confirm_box(6, 3, 6 + 12 * 12, 3 + 12 * 5, NULL)) {
        scroll_to_lcd();
        fb_flush();
        return 0;
    }
    save_wipe();                    /* 等效 delete_file + exit_game */
    return 1;
}

/* 等效 look_board(告示板):好人看通緝令,壞人看到自己 */
static void look_board(void)
{
    uint8_t *slot = quest_temp + QUEST_GHOST * 12;

    if (!(hero.man_daode & 0x80)) {         /* 壞人 */
        memcpy(varbuf, hero.man_name, 9);
        goto wanted;
    }
    task_buf[0] = QUEST_GHOST;              /* 原版 lm quest_type 副作用 */
    if (slot[5] == KILLER_NPC) {            /* 通緝中 */
        memcpy(varbuf, slot + 1, 4);
        varbuf[4] = 0;
        task_buf[1] = 0;                    /* 原版 sta quest_id 副作用 */
wanted:
        memcpy(tplbuf, tm_board2_msg, TM_BOARD2_MSG_LEN);
        putw(tplbuf + TM_BOARD2_MSG_S0, varbuf);
        tell_fmt(tplbuf);
        return;
    }
    tell_fmt(tm_board1_msg);
}

/* ================= 遊戲山莊(panyan.s 入口,遊戲本體 bank 27) ========= */

/* ---- 凌波微步(GBC 原創):跳舞毯 LINGBO_SCORE 分獎勵 ----
 * 原版 panyan.s 的 cal_up_level 是空函式(只有 rts),小遊戲從沒接上武功;
 * 分招名與閃避訊息見 fight.c lingbo_msg*。本 bank(28)訊息傳給 HOME 的
 * format_string 安全(bank 28 此刻映射中)。 */
static const uint8_t pq_lingbo_get_msg[] = {
    /* 你在毯上踏遍六十四卦 */
    0xC4,0xE3,0xD4,0xDA,0xCC,0xBA,0xC9,0xCF,
    0xCC,0xA4,0xB1,0xE9,0xC1,0xF9,0xCA,0xAE,
    0xCB,0xC4,0xD8,0xD4,
    0x00,
    /* 脚下竟自成一套身法 */
    0xBD,0xC5,0xCF,0xC2,0xBE,0xB9,0xD7,0xD4,
    0xB3,0xC9,0xD2,0xBB,0xCC,0xD7,0xC9,0xED,
    0xB7,0xA8,
    0x00,
    /* 你叫它凌波微步! */
    0xC4,0xE3,0xBD,0xD0,0xCB,0xFC,0xC1,0xE8,
    0xB2,0xA8,0xCE,0xA2,0xB2,0xBD,0x21,
    0x00,
    0x00
};
static const uint8_t pq_lingbo_pai_msg[] = {
    /* 你隐约觉出脚下门道 */
    0xC4,0xE3,0xD2,0xFE,0xD4,0xBC,0xBE,0xF5,
    0xB3,0xF6,0xBD,0xC5,0xCF,0xC2,0xC3,0xC5,
    0xB5,0xC0,
    0x00,
    /* 可惜本门武学早已入骨 */
    0xBF,0xC9,0xCF,0xA7,0xB1,0xBE,0xC3,0xC5,
    0xCE,0xE4,0xD1,0xA7,0xD4,0xE7,0xD2,0xD1,
    0xC8,0xEB,0xB9,0xC7,
    0x00,
    /* 再难改弦更张 */
    0xD4,0xD9,0xC4,0xD1,0xB8,0xC4,0xCF,0xD2,
    0xB8,0xFC,0xD5,0xC5,
    0x00,
    0x00
};

static void lingbo_reward(void)
{
    uint8_t y;

    if (find_kf(LINGBO_KF) != 0xFF)
        return;                             /* 已有:不重複給、不出訊息 */
    if (hero.man_pai != NONE_PAI && hero.man_pai != XIAOYAO_PAI) {
        format_string(pq_lingbo_pai_msg);   /* 已投他派:不發放 */
        message_box(6, 3, 6 + 12 * 12, 3 + 12 * 5);
        return;
    }
    kf_id = LINGBO_KF;
    if (!add_kf())
        return;                             /* 武功槽已滿(MAX_KF) */
    y = find_kf(LINGBO_KF);
    if (y != 0xFF)
        hero.man_kf[y + 1] = 1;             /* 給 1 級:不留等級 0 條目
                                             * (見 docs/hidden.md B6) */
    format_string(pq_lingbo_get_msg);
    message_box(6, 3, 6 + 12 * 12, 3 + 12 * 5);
}

/* 等效 do_panyan:hill 確認 → 選路(原版按數字鍵→游標選擇)→ 小遊戲 */
static void do_panyan(void)
{
    uint8_t cur = 0, k, y;

    format_string(tm_hill_msg);
    if (!confirm_box(6, 3, 6 + 12 * 12, 3 + 6 * 12, NULL)) {
        scroll_to_lcd();
        fb_flush();
        return;
    }

    fb_clear();
    format_string(tm_select_road_msg);
    ss_ptr = OutBuf;
    show_string(5, 2, 4);           /* 介紹 2 行 + 三選項(y=30/43/56) */

    for (;;) {
        y = (uint8_t)(30 + cur * 13);
        font_draw_ascii(0, y, '>');
        fb_flush();
        k = wait_key_rep();
        fb_fill_rect(0, y, 8, 13, 0);

        if (k == K_UP)
            cur = cur ? (uint8_t)(cur - 1) : 2;
        else if (k == K_DOWN)
            cur = (cur == 2) ? 0 : (uint8_t)(cur + 1);
        else if (k == K_ESC)
            break;
        else if (k == K_CR) {
            if (cur == 0) {
                if (panyan_dance())
                    lingbo_reward();
            }
            else if (cur == 1)
                panyan_ball();
            break;                  /* 3.回去 */
        }
    }
    /* 小遊戲整場改寫 scroll_buf(fight 同病先例,npc.c)→ 增量
     * 快照/圖名全失效,必須作廢重合成,否則離開時畫面殘骸混合 */
    map_invalidate();
    show_player();                  /* 等效 refresh_scroll */
    fb_flush();
}

/* ================= 最終決戰 + 結局 + 評定(talk.s final_fight/rank.s) ==== */

static const uint16_t boss_img_tbl[3] = { 479, 467, 478 };

/* 等效 rank.s rank:等級評定 + 收場白 */
static void rank_screen(void)
{
    static uint8_t do_kill_v;
    static const uint8_t *rank_desc_v;
    const uint8_t *desc = tm_rank1_msg;
    const uint8_t *tail;
    uint8_t d = hero.man_daode;

    do_kill_v = (d & 0x80) ? hero.guan_kill : hero.si_kill;

    do {
        if (d < BADMAN && hero.man_per >= 32 && hero.si_kill >= 100) {
            desc = tm_rank5_msg;
            break;
        }
        if (d >= GOODMAN && hero.guan_kill >= 60 && hero.man_per >= 22) {
            desc = tm_rank6_msg;
            break;
        }
        if (hero.npc_kill == 0 && hero.guan_kill == 0) {
            desc = tm_rank7_msg;
            break;
        }
        if (!(d & 0x80) && hero.si_kill >= 108) {
            desc = tm_rank2_msg;
            break;
        }
        if ((d & 0x80) && hero.guan_kill >= 64) {
            desc = tm_rank8_msg;
            break;
        }
        if (hero.npc_kill >= 120) {
            desc = tm_rank4_msg;
            break;
        }
        if (hero.man_gender != 0 && hero.man_per >= 36
                && hero.man_age < 30) {
            desc = tm_rank9_msg;
            break;
        }
        if (hero.game_hour < 48) {
            desc = tm_rank3_msg;
            break;
        }
        if (hero.top_dance >= 300) {
            desc = tm_rank10_msg;
            break;
        }
        if (hero.top_ball >= 300) {
            desc = tm_rank11_msg;
            break;
        }
    } while (0);
    rank_desc_v = desc;

    fb_clear();
    fb_flush();

    memcpy(tplbuf, tm_pingding_msg, TM_PINGDING_MSG_LEN);
    putw(tplbuf + TM_PINGDING_MSG_S0, &hero.game_hour);
    putw(tplbuf + TM_PINGDING_MSG_S1, &hero.game_min);
    putw(tplbuf + TM_PINGDING_MSG_S2, &hero.npc_kill);
    putw(tplbuf + TM_PINGDING_MSG_S3, &do_kill_v);
    putw(tplbuf + TM_PINGDING_MSG_S4, &hero.man_daode);
    putw(tplbuf + TM_PINGDING_MSG_S5, &rank_desc_v);
    format_string(tplbuf);
    message_box_more(6, 3, 156, 77);

    /* 收場白:道德兩極 → end2(回到現代),中間 → end1(夢醒) */
    tail = tm_rank_end2_msg;
    if (d < GOODMAN && d >= BADMAN)
        tail = tm_rank_end1_msg;
    format_string(tail);
    message_box_more(6, 3, 156, 77);
}

/* 等效 talk.s final_fight:六派石板集齊 → 依道德/道德和尚存活選 BOSS;
 * 勝 → 結局捲軸(不可跳)+ 評定 → exit_game。回 1=結束遊戲 */
static uint8_t final_fight_item(void)
{
    uint8_t x, ex;

    if ((hero.shiban & 0x3F) != 0x3F)
        return 0;

    x = BOSS1_NPC + 2;                          /* BOSS3 */
    if (npc_alive(DAODE_NPC) || hero.man_daode >= BADMAN) {
        x--;                                    /* BOSS2 */
        if (hero.man_daode < GOODMAN)
            x--;                                /* BOSS1 */
    }
    located_id = x;
    ex = (uint8_t)(x - BOSS1_NPC);              /* endx 0-2 */
    G_id_item = boss_img_tbl[ex];
    init_npc_b();

    x = final_fight(ex);
    if (x == 0)
        return 1;                               /* 戰死 → exit_game */
    if (x == 2) {
        show_player();                          /* 等效 refresh_scroll */
        fb_flush();
        return 0;                               /* 拒戰/逃 → 回地圖 */
    }
    scroll_text_28(ending_tbl[ex], ending_lines_tbl[ex], 0);
    rank_screen();
    return 1;                                   /* 通關 → exit_game */
}

/* ================= 等效 talk.s item_action(單一 BANKED 入口) =================
 * idx = located_id - 200;回 1 = 自殺刪檔(呼叫端置 quit_flag) */
uint8_t item_action_b(uint8_t idx) BANKED
{
    switch (idx) {
    case 0:                             /* 擂台:聯機對戰(talk.s do_netfight;
                                         * 對戰線取代 IrDA,2026-07-17 用戶
                                         * 啟用,推翻「網戰已裁」) */
        net_flag |= 0x80;
        busy_flag |= 0x80;
        netfight();
        busy_flag &= 0x7F;
        net_flag = 0;                   /* 含 bit1 回合旗一併復位 */
        map_invalidate();               /* 戰鬥毀 scroll_buf → 全量重繪 */
        show_player();                  /* 等效 refresh_scroll */
        fb_flush();
        break;
    case 1:                                 /* 遊戲山莊(跳舞毯/投篮球) */
        do_panyan();
        break;
    case 2:                                 /* 掃帚 */
        if (home_buf == 1) {
            home_buf = 0;
            do_saodi();
        }
        break;
    case 3:                                 /* 水桶 */
        if (home_buf == 2) {
            home_buf = 0;
            do_tiaoshui();
        }
        break;
    case 4:                                 /* 木柴 */
        if (home_buf == 3) {
            home_buf = 0;
            do_pichai();
        }
        break;
    case 5:                                 /* 水井 */
        do_drink();
        break;
    case 6:                                 /* 釣魚點(原版 do_fish 包 busy) */
        busy_flag |= 0x80;
        fishing();
        busy_flag &= 0x7F;
        break;
    case 7:                                 /* 上吊自殺 */
        return do_suicide();
    case 8:                                 /* 告示板 */
        look_board();
        break;
    case 9:                                 /* 床 */
        do_sleep();
        break;
    case 10:                                /* 魔宮:最終決戰 */
        return final_fight_item();
    case 11:                                /* 書架(菜花寶典) */
        baodian();
        break;
    case 12:                                /* 草蓆(繩索) */
        find_rope();
        break;
    default:
        break;
    }
    return 0;
}
