/* npc.c - NPC 互動,移植原版 npc.s npc_action/init_menu + talk.s object_say
 *
 * 選單四變體(npc.s npc_menu1-4,BOX_MENU 直列帶框):
 *   特殊表:TEACHER(31)/DAXIA(7)→學習,KILLER(125)→通用
 *   man_master==id→學習;NONE_PAI(0)→通用;TRADE_PAI(255)→交易;其他→拜師
 * 處理器:npc_talk 已接(quest 前的 dunno 路徑);
 *        npc_look/npc_fight/npc_trade/npc_apprentice/npc_learn 隨對應模組接入
 * 代碼在 bank 25(選單處理器與引擎同 bank)。
 */
#pragma bank 25
#include <gb/gb.h>
#include <string.h>
#include "game.h"
#include "text.h"
#include "menu.h"
#include "font.h"
#include "fb.h"
#include "blit.h"
#include "save.h"
#include "gamedata.h"
#include "goods.h"
#include "skill.h"
#include "ui.h"
#include "serve.h"
#include "baishi.h"
#include "menutext.h"
#include "fight.h"
#include "task.h"

#define DAXIA_NPC 7
#define TEACHER_NPC 31
#define KILLER_NPC 125
#define NONE_PAI 0
#define TRADE_PAI 255

extern const uint8_t *const dunno_msgs[5];

/* 等效 npc.s npc_talk:任務鏈(bank28 talk_task)→ dunno 隨機語 */
static uint8_t npc_talk(void)
{
    scroll_to_lcd();    /* 等效 PullMenu 後的 scroll_to_lcd:先清選單 */
    fb_flush();

    if (talk_task()) {                          /* 任務/尋人/quest 有話 */
        scroll_to_lcd();
        fb_flush();
        return 1;
    }
    obj_flag = 0x80;
    format_string(dunno_msgs[random_it(5)]);
    show_talk_msg();
    return 1;                                   /* 等效 PullMenu 退出 */
}

/* 等效 npc.s npc_desc_msg(C 端版:dw=本機指針,運行期填)。
 * wuyi/chushou 記錄 = 原版 wuyi_desc/chushou_desc(type9 二次間接:
 * dw 指向 varbuf 槽,槽內存字串指針)。
 * 模板在 ROM,工作副本+記錄放 gfx_scratch UI 複用區(WRAM 緊張,
 * 配置見 text.h) */
#define look_msg     (gfx_scratch + 0)      /* 工作副本(79B) */
#define wuyi_desc    (gfx_scratch + 92)     /* {8, &varbuf[4]} */
#define chushou_desc (gfx_scratch + 96)     /* {4, &varbuf[6]} */

static const uint8_t look_msg_tpl[] = {
    7, 0, 0, 0x0A, 0x00,                            /* [1]=名字 */
    0xBF, 0xB4, 0xC6, 0xF0, 0xC0, 0xB4, 0xD4, 0xBC, /* 看起来约 */
    1, 0, 0, 0x0A, 0x00,                            /* [14]=varbuf+2 */
    0xB6, 0xE0, 0xCB, 0xEA, 0x00,                   /* 多岁 + 行終 */
    0xCE, 0xE4, 0xD2, 0xD5, 0xBF, 0xB4,             /* 武艺看 */
    0xC6, 0xF0, 0xC0, 0xB4,                         /* 起来 */
    9, 0, 0, 0x00, 0x00,                            /* [34]=wuyi_desc,行終 */
    0xB3, 0xF6, 0xCA, 0xD6, 0xCB, 0xC6, 0xBA, 0xF5, /* 出手似乎 */
    9, 0, 0, 0x00, 0x00,                            /* [47]=chushou_desc */
    0xB4, 0xF8, 0xD7, 0xC5, 0x3A,                   /* 带着: */
    8, 0, 0, 0x0A, 0x00,                            /* [57]=varbuf+10 */
    ' ',
    8, 0, 0, 0x0A, 0x00,                            /* [63]=varbuf+12 */
    ' ',
    8, 0, 0, 0x00, 0x00,                            /* [69]=varbuf+14,行終 */
    7, 0, 0, 0x00, 0x00,                            /* [74]=npc_desc,行終 */
    0x00,                                           /* 全文終 */
};

static void putw(uint8_t *p, const void *v)
{
    uint16_t a = (uint16_t)v;
    p[0] = (uint8_t)a;
    p[1] = (uint8_t)(a >> 8);
}

/* 等效 npc.s npc_look */
static uint8_t npc_look(void)
{
    uint8_t i, g;

    scroll_to_lcd();
    fb_flush();

    memcpy(look_msg, look_msg_tpl, sizeof look_msg_tpl);
    wuyi_desc[0] = 8;
    chushou_desc[0] = 4;

    varbuf[2] = npc.npc_age / 10 * 10;      /* 年齡取整十(原版) */

    set_obj(1);                             /* npc 側 */
    get_skill_desc();
    putw(varbuf + 4, skill_desc_buf);
    putw(varbuf + 6, strong_desc_buf);

    /* 帶著:npc_goods[0..2],bit7=裝備中(原版 l_goods) */
    for (i = 0; i < 3; i++) {
        const uint8_t *nm = 0;
        g = npc.npc_goods[i];
        if (g & 0x80)
            nm = get_goods_name(g & 0x7F);
        putw(varbuf + 10 + (i << 1), nm);
    }

    putw(look_msg + 1, npc.npc_name);
    putw(look_msg + 14, varbuf + 2);
    putw(look_msg + 34, wuyi_desc);
    putw(wuyi_desc + 1, varbuf + 4);
    putw(look_msg + 47, chushou_desc);
    putw(chushou_desc + 1, varbuf + 6);
    putw(look_msg + 57, varbuf + 10);
    putw(look_msg + 63, varbuf + 12);
    putw(look_msg + 69, varbuf + 14);
    putw(look_msg + 74, npc_desc);

    format_string(look_msg);
    message_box_more(6, 3, 156, 77);
    return 1;
}

/* ---- 等效 npc.s get_npc_goods:掠奪金錢 + 4 格物品 → 戰利品框 ----
 * 三角鐵(石板碎片)按門派 shiban 位圖判重,一派只得一塊。 */
static const uint8_t shiban_tbl[7] = { 0, 1, 2, 4, 8, 16, 32 };
static const uint8_t loot_head[] = {            /* 大获全胜!战斗获得\0金钱: */
    0xB4, 0xF3, 0xBB, 0xF1, 0xC8, 0xAB, 0xCA, 0xA4, 0x21,
    0xD5, 0xBD, 0xB6, 0xB7, 0xBB, 0xF1, 0xB5, 0xC3, 0x00,
    0xBD, 0xF0, 0xC7, 0xAE, 0x3A,
};
static const uint8_t loot_goods[] = {           /* 物品: */
    0xCE, 0xEF, 0xC6, 0xB7, 0x3A,
};

static void get_npc_goods(void)
{
    uint8_t i, g;
    uint8_t *p = gfx_scratch + 192;             /* 模板組裝區 */
    uint8_t *names = gfx_scratch + 256;         /* 4×12B(名字池僅 3 格) */

    hero.man_money += npc.npc_money;

    for (i = 0; i < 4; i++) {
        varbuf[i << 1] = 0;
        varbuf[(i << 1) + 1] = 0;
        g = npc.npc_goods[i];
        if (!g)
            continue;
        if (g == SANJIAO_GOODS) {               /* 石板:查派位圖 */
            uint8_t bit = shiban_tbl[(npc.npc_pai < 7) ? npc.npc_pai : 0];
            if (hero.shiban & bit)
                continue;
            hero.shiban |= bit;
        }
        g &= 0x7F;
        add_goods(g);                           /* 包滿仍列名(原版) */
        memcpy(names + (i * 12), get_goods_name(g), 12);
        putw(varbuf + (i << 1), names + (i * 12));
    }

    /* 組裝 get_msg 模板(dw=本機指針,運行期填) */
    memcpy(p, loot_head, sizeof loot_head);
    p += sizeof loot_head;
    *p++ = 2;                                   /* type2: npc_money */
    putw(p, &npc.npc_money);
    p += 2;
    *p++ = 0; *p++ = 0;                         /* dw 0 行終 */
    memcpy(p, loot_goods, sizeof loot_goods);
    p += sizeof loot_goods;
    for (i = 0; i < 4; i++) {
        if (i)
            *p++ = ' ';
        *p++ = 8;                               /* type8: 名字指針槽 */
        putw(p, varbuf + (i << 1));
        p += 2;
        if (i < 3) {
            *p++ = 10; *p++ = 0;                /* dw 10 續行 */
        } else {
            *p++ = 0; *p++ = 0;                 /* dw 0 行終 */
        }
    }
    *p = 0;                                     /* 全文終 */

    format_string(gfx_scratch + 192);
    message_box_more(6, 3, 150, 75);            /* 原版 box(6,3)-(150,75) */
}

/* 等效 npc.s npc_fight:進 fight()(bank 27)+ 戰後處理。
 * exit_code:0 玩家死(原版 exit_game→GBC 軟重啟) 2 逃/被放
 * 1 放過 NPC、3 殺死 NPC → 均掠奪(原版行為);3 另標記死亡。 */
static uint8_t npc_fight(void)
{
    fight();
    /* fight() 清了整屏並改寫 scroll_buf → 增量捲動快照全失效;
     * 一律作廢,回地圖時 show_player 走 make_ground 全量重畫(否則白屏)。 */
    map_invalidate();
    if (fight_exit_code == 0) {
        /* 原版 npc_win: jmp exit_game(dead.s penalty 無呼叫者=死代碼,
         * 2026-07-10 查證)→ GBC 對應 = 回標題,續玩讀上次存檔 */
        quit_flag = 1;
        return 2;                               /* 全退 */
    }
    if (fight_exit_code == 2)
        return 1;
    if (fight_exit_code == 3) {
        /* set_die_stat:殺手不標死(原版 check_stat 對 KILLER 特判
         * 「永遠活著」,其 set_die_stat 寫入走未初始化指針=未定義行為,
         * GBC 取確定等價=跳過;存活由 G_Task_Flag 管) */
        if (located_id != KILLER_NPC)
            npc_stat_buf[located_id >> 3] &=
                (uint8_t)~(0x80 >> (located_id & 7));
        fight_win_task();       /* 查殺 QUEST_KILL 記完成 / 通緝犯發賞 */
    }
    get_npc_goods();
    return 1;
}

#define tplbuf (gfx_scratch + 192)

uint16_t goods_price;
static uint8_t goods_id;    /* 交易中的物品(原版共享全域,此處模組內) */

/* ================= 交易(等效 npc.s dealer 段) ================= */

#define BUY_X0 6
#define BUY_Y0 16
#define PRICE_Y0 (FB_ROWS - 13)

/* 等效 show_price:賺錢/價格行(重畫前清行=原版 replace 字模效果) */
static void show_price(void)
{
    memcpy(tplbuf, mt_goods_price_msg, sizeof mt_goods_price_msg);
    fix_mt_goods_price_msg(tplbuf);
    format_string(tplbuf);
    clear_nline2(PRICE_Y0, 13);
    ss_ptr = OutBuf;
    show_one_line(1, PRICE_Y0);
    fb_flush();
}

static void show_price1(void)               /* 買:原價 */
{
    goods_id = dmenu_buf[1 + menu_set];
    get_goods_attr(goods_id);
    goods_price = (uint16_t)goods_attr[6] | ((uint16_t)goods_attr[7] << 8);
    show_price();
}

static void show_price2(void)               /* 賣:七折 */
{
    goods_id = dmenu_buf[1 + (menu_set << 1)];
    get_goods_attr(goods_id & 0x7F);
    goods_price = (uint16_t)goods_attr[6] | ((uint16_t)goods_attr[7] << 8);
    goods_price = (uint16_t)((uint32_t)goods_price * 7 / 10);
    show_price();
}

static uint8_t buy_it(void)
{
    /* 錢高 16 位非零必然夠(原版 man_money+2/+3 檢查) */
    if ((uint16_t)(hero.man_money >> 16) == 0
        && (uint16_t)hero.man_money < goods_price)
        return 0;
    if (!add_goods(goods_id))
        return 0;                           /* 包滿 */
    hero.man_money -= goods_price;
    return 1;                               /* PullMenu:退出交易 */
}

static uint8_t sell_it(void)
{
    uint8_t x, i;

    if (goods_id & 0x80)
        return 0;                           /* 裝備中不賣 */
    x = find_goods(goods_id);
    if (x == 0xFF)
        return 0;
    hero.man_goods[x + 1]--;
    hero.man_money += goods_price;

    i = 2 + (menu_set << 1);                /* 清單數量就地遞減 */
    if (--dmenu_buf[i] == 0)
        return 1;                           /* 賣光此項:退出交易 */
    return 0;
}

static const menu_fn buy_h[1] = { buy_it };
static const show_fn buy_s[1] = { show_price1 };
static const menu_fn sell_h[1] = { sell_it };
static const show_fn sell_s[1] = { show_price2 };

/* buy_menu(11011000b/80h/10110001b/RADIO):1B 條目 */
static const cmenu_t buy_menu =
    { 0, 1, 3, 1, MSTYLE_RADIO, 8, 0, buy_h, buy_s, 0, goods_name_tbl };
/* sell_menu(11111011b/80h/10110001b/RADIO):2B 帶數量 */
static const cmenu_t sell_menu =
    { 0, 1, 3, 1, MSTYLE_RADIO, 11, MF_DIGITS, sell_h, sell_s,
      0, goods_name_tbl };

/* 等效 npc_trade + dealer:貨單非空=買,空=收購 */
static uint8_t npc_trade(void)
{
    scroll_to_lcd();
    fb_flush();
    if (vendor_goods[0]) {
        uint8_t n = vendor_goods[0];
        format_string(mt_deal_msg);
        show_talk_msg0();
        if (n > 9)
            n = 9;
        memcpy(dmenu_buf, vendor_goods, n + 1);
        dmenu_buf[0] = n;
        pop_menu(BUY_X0, BUY_Y0, &buy_menu);
    } else {
        format_string(mt_deal1_msg);
        show_talk_msg0();
        set_all_goods();
        pop_menu(BUY_X0, BUY_Y0, &sell_menu);
    }
    scroll_to_lcd();
    fb_flush();
    return 1;
}

/* ================= 拜師(等效 npc.s npc_apprentice) ================= */

/* 等效 cat_string:src 首段(含行終 0)接到 dst 首段後,雙 0 收尾 */
static void cat_first(uint8_t *dst, const uint8_t *src)
{
    uint8_t i = 0, j = 0;

    while (dst[i])
        i++;
    i++;                                    /* 保留行終 0 作分行 */
    while ((dst[i] = src[j]) != 0) {
        i++;
        j++;
    }
    dst[i + 1] = 0;
}

static uint8_t npc_apprentice(void)
{
    const uint8_t *msg;

    scroll_to_lcd();
    fb_flush();

    if (hero.man_pai == 7) {                /* XIAOYAO:自創門派 */
        msg = mt_have_kf_msg;
    } else if (hero.man_pai != 0 && hero.man_pai != npc.npc_pai) {
        msg = mt_have_master_msg;           /* 已另投名師 */
    } else if (baishi(located_id)) {
        hero.man_master = located_id;
        hero.man_pai = npc.npc_pai;
        cat_first(tplbuf, mt_apprentice_msg);
        msg = tplbuf;
    } else {
        msg = tplbuf;                       /* 師父的拒絕理由 */
    }
    obj_flag = 0x80;
    format_string(msg);
    show_talk_msg();
    scroll_to_lcd();
    fb_flush();
    return 1;
}

/* ================= 學習(等效 npc.s npc_learn/master/learn_it) ===== */

static void learn_set_line(void)
{
    uint8_t y = find_kf(kf_id);

    bcdbuf = (uint16_t)hero.man_kf[y + 1] * hero.man_kf[y + 1];
    binbuf = (uint16_t)hero.man_kf[y + 2]
           | ((uint16_t)hero.man_kf[y + 3] << 8);
}

static void learn_set_digit(void)
{
    uint8_t y = find_kf(kf_id);

    bcdbuf = hero.man_kf[y + 1];
    binbuf = (uint16_t)hero.man_kf[y + 2]
           | ((uint16_t)hero.man_kf[y + 3] << 8);
}

static uint8_t learn_tick(void)
{
    /* 學習顯示速度:EXT 每輪 1 點(原版),ADV/BSC 每輪 4 點(x4,
     * 2026-07-17 用戶定版;interval 已是 1 幀,只能加每輪點數) */
    uint8_t n = (uint8_t)1 << disp_shift();

    do {
        if (pyh_learn())
            return 1;
    } while (--n);
    return 0;
}

static uint8_t learn_it(void)
{
    uint8_t y;

    kf_id = dmenu_buf[1 + (menu_set << 1)];
    learn_master_lvl = dmenu_buf[2 + (menu_set << 1)];

    if (!add_kf())
        return 0;                           /* 武功欄滿 */

    /* 原版 learn_tbl interval=4ms;重建版給 1 幀留按鍵中斷窗口 */
    busy_flag |= 0x80;                      /* 學習中不回復(npc.s) */
    show_process(32, 1, learn_set_line, learn_set_digit, 1, learn_tick);
    busy_flag &= 0x7F;
    scroll_to_lcd();
    fb_flush();

    /* 一點沒學會 → 移除剛加的空技能(原版 dec man_kfnum) */
    y = find_kf(kf_id);
    if (y != 0xFF && hero.man_kf[y + 1] == 0)
        hero.man_kfnum--;
    return 0;                               /* 留在學習選單 */
}

static const menu_fn learn_h[1] = { learn_it };
/* master_menu(10111110b/80h/10110001b/RADIO):2B[武功,師父等級=數量位] */
static const cmenu_t master_menu =
    { 0, 1, 3, 1, MSTYLE_RADIO, 14, MF_DIGITS, learn_h, 0, 0, kf_name_tbl };

/* 等效 npc.s master1(讀書 study_cmd 也走這裡) */
void master1(void) BANKED
{
    uint8_t n = npc.npc_kfnum;

    if (n > 20)
        n = 20;
    dmenu_buf[0] = n;
    memcpy(dmenu_buf + 1, npc_kf, (uint8_t)(n << 1));
    pop_menu(BUY_X0, BUY_Y0, &master_menu);
}

/* 等效 master:提示語 + 學習選單 */
static void master(void)
{
    format_string(mt_learn_msg);
    show_talk_msg0();
    master1();
}

static uint8_t npc_learn(void)
{
    scroll_to_lcd();
    fb_flush();
    if (located_id == DAXIA_NPC && hero.man_exp < 200000UL) {
        obj_flag = 0x80;
        format_string(mt_daxia_low_msg);    /* 大俠嫌你經驗淺 */
        show_talk_msg();
    } else {
        master();
    }
    scroll_to_lcd();
    fb_flush();
    return 1;
}

static const uint8_t it_talk[] = "\xBD\xBB\xCC\xB8";    /* 交谈 */
static const uint8_t it_look[] = "\xB2\xE9\xBF\xB4";    /* 查看 */
static const uint8_t it_fight[] = "\xD5\xBD\xB6\xB7";   /* 战斗 */
static const uint8_t it_trade[] = "\xBD\xBB\xD2\xD7";   /* 交易 */
static const uint8_t it_appr[] = "\xB0\xDD\xCA\xA6";    /* 拜师 */
static const uint8_t it_learn[] = "\xD1\xA7\xCF\xB0";   /* 学习 */

static const menu_fn h3[] = { npc_talk, npc_look, npc_fight };
static const menu_fn h4_trade[] = { npc_talk, npc_look, npc_fight, npc_trade };
static const menu_fn h4_appr[] =
    { npc_talk, npc_look, npc_fight, npc_apprentice };
static const menu_fn h4_learn[] = { npc_talk, npc_look, npc_fight, npc_learn };

static const uint8_t *const m1_items[] = { it_talk, it_look, it_fight };
static const uint8_t *const m2_items[] = { it_talk, it_look, it_fight, it_trade };
static const uint8_t *const m3_items[] = { it_talk, it_look, it_fight, it_appr };
static const uint8_t *const m4_items[] = { it_talk, it_look, it_fight, it_learn };

static const cmenu_t npc_menu1 =
    { 3, 1, 0, 1, MSTYLE_BOX, 0, 0, h3, 0, m1_items, 0 };
static const cmenu_t npc_menu2 =
    { 4, 1, 0, 1, MSTYLE_BOX, 0, 0, h4_trade, 0, m2_items, 0 };
static const cmenu_t npc_menu3 =
    { 4, 1, 0, 1, MSTYLE_BOX, 0, 0, h4_appr, 0, m3_items, 0 };
static const cmenu_t npc_menu4 =
    { 4, 1, 0, 1, MSTYLE_BOX, 0, 0, h4_learn, 0, m4_items, 0 };

/* 等效 npc.s init_menu */
static const cmenu_t *init_menu(void)
{
    if (located_id == TEACHER_NPC || located_id == DAXIA_NPC)
        return &npc_menu4;
    if (located_id == KILLER_NPC)
        return &npc_menu1;
    if (hero.man_master == located_id)
        return &npc_menu4;
    if (npc.npc_pai == NONE_PAI)
        return &npc_menu1;
    if (npc.npc_pai == TRADE_PAI)
        return &npc_menu2;
    return &npc_menu3;
}

/* 等效 npc.s init_npc:載入 NPC 記錄 + 動態屬性加成。
 * 殺手(init_ghost)由玩家屬性生成,跳過 NPC_DATA 但仍吃武器加成。
 * 8-bit 加法照原版,無鉗位。 */
static void init_npc(void)
{
    uint8_t y;

    if (!init_ghost())
        text_load_npc(located_id);      /* 等效 get_text_data NPC_DATA */

    if (npc.npc_goods[0] & 0x80) {      /* npc_weapon → damage */
        get_goods_attr(npc.npc_goods[0] & 0x7F);
        npc.npc_damage += goods_attr[2];
    }
    if (npc.npc_goods[1] & 0x80) {      /* npc_equip → armor */
        get_goods_attr(npc.npc_goods[1] & 0x7F);
        npc.npc_armor += goods_attr[2];
    }

    y = find_npc_kf(BASIC_BARE_KF);     /* 基本拳腳 → str */
    if (y != 0xFF)
        npc.npc_str += npc_kf[y + 1] / 10;
    y = find_npc_kf(BASIC_DODGE_KF);    /* 基本輕功 → dex */
    if (y != 0xFF)
        npc.npc_dex += npc_kf[y + 1] / 10;
}

/* 跨 bank 入口(最終決戰 pyh_quest.c 需先載 BOSS 記錄) */
void init_npc_b(void) BANKED
{
    init_npc();
}

/* 等效 npc.s npc_action:init_npc + 名字 + 動態選單(SET_TALK_XY: 30,16) */
void npc_action(void)
{
    init_npc();

    scroll_to_lcd();                            /* 乾淨底圖 */
    font_draw_text_replace(6, 2, npc.npc_name); /* show_one_line x=1 列 */
    fb_flush();

    pop_menu(30, 16, init_menu());

    show_player();                              /* 恢復畫面 */
    fb_flush();
}

/* 等效 talk.s object_say(check_stat + NPC/item 分流;
 * item_action 主體在 bank28 pyh_quest.c) */
void object_say(void) BANKED
{
    if (G_Curr_Id < NPC_OFF)
        return;
    if (located_id == 0)
        return;
    /* check_stat:殺手恆活(圖在即人在),其餘查存活位圖 */
    if (located_id != KILLER_NPC && !npc_alive(located_id))
        return;
    if (located_id >= 200) {
        if (item_action_b((uint8_t)(located_id - 200)))
            quit_flag = 1;                  /* 自殺刪檔 → exit_game */
        return;
    }
    npc_action();
}
