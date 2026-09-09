/* goods.c - 物品清單 UI 的 HOME 層(實作見 goods_impl.c,bank 31)
 *
 * 名字/屬性表在 ROM bank 24(gamedata.c),存取時臨時切 bank。
 * 原版 find_name 回傳表內指針;GBC 端表在 banked ROM,改拷入
 * 3 格輪替緩衝(npc_look 帶著三件裝備需同時持有 3 個名字)。
 *
 * 2026-07-29 拆檔:bank0 只剩 168 字節,把「以名字直接呼叫」的實作
 * 遷到 bank 31。留在這裡的只有三類必須常駐 HOME 的東西——
 *   ① 選單表(cmenu_t / menu_fn / show_fn):menu.c 在 bank 26 讀它們,
 *      跨 bank 指標會讀到垃圾;
 *   ② 表裡的處理器本體:menu.c 透過函式指標呼叫,目標必須恆常可達;
 *   ③ show_study/study_cmd:引用 mt_*(menutext.c 在 bank 26),那批
 *      字串只在選單會話期間 bank 26 掛著時讀得到,不能跟去 bank 31。
 * 詳見 goods_impl.h。
 */
#include <gb/gb.h>
#include <string.h>
#include "gamedata.h"
#include "goods.h"
#include "goods_impl.h"
#include "save.h"
#include "text.h"
#include "skill.h"
#include "menu.h"
#include "menutext.h"
#include "ui.h"
#include "font.h"
#include "fb.h"
#include "blit.h"
#include "res.h"
#include "textbank_res.h"
#include "game.h"

uint8_t goods_attr[8];
uint8_t goods_type, goods_id;           /* 原版同名(goods_id 含 bit7) */

/* 名字輪替池:UI 期間 gfx_scratch 複用(配置見 text.h) */
#define name_pool (gfx_scratch + 100)
static uint8_t name_rot;

/* 等效 aux.s find_name:0xFF(或 0,pai 表)結尾條目線性走訪至第 id 條
 *
 * 必須留 HOME:它自己切 bank 讀 gamedata,而 banked 代碼一切 0x4000 窗
 * 就把自身切走(fight.c:121)。2026-07-29 曾誤搬進 goods_impl.c,症狀是
 * 查看「描述」頁起整個 UI 全白——game.c show_desc 第一件事就是呼叫它。 */
const uint8_t *gd_find_name(const uint8_t *tbl, uint8_t id)
{
    uint8_t bank_save = _current_bank;
    uint8_t *dst;
    uint8_t i, c;

    if (name_rot >= 3)
        name_rot = 0;
    dst = name_pool + (uint8_t)(name_rot++ * 12);

    SWITCH_ROM(GAMEDATA_BANK);
    while (id--) {
        while ((c = *tbl++) != 0xFF && c != 0)
            ;
    }
    for (i = 0; i < 11; i++) {
        c = *tbl++;
        if (c == 0)
            c = 0xFF;
        dst[i] = c;
        if (c == 0xFF)
            break;
    }
    dst[11] = 0xFF;
    SWITCH_ROM(bank_save);
    return dst;
}

/* 等效 goods.s get_goods_name(input Areg=id, output a1=名字址) */
const uint8_t *get_goods_name(uint8_t id)
{
    return gd_find_name(goods_name_tbl, id);
}

/* 等效 goods.s get_goods_attr(a1 = goods_attr_tbl + id*8,拷出 8B) */
void get_goods_attr(uint8_t id)
{
    uint8_t bank_save = _current_bank;

    SWITCH_ROM(GAMEDATA_BANK);
    memcpy(goods_attr, goods_attr_tbl + ((uint16_t)id << 3), 8);
    SWITCH_ROM(bank_save);
}

/* 等效 goods.s find_goods:man_goods 內找 id(整字節比較,含 bit7),
 * 回傳字節偏移 x;0xFF=未找到(原版 carry) */
uint8_t find_goods(uint8_t id)
{
    uint8_t x;

    for (x = 0; x < MAX_GOODS * 2; x += 2) {
        if (hero.man_goods[x + 1] == 0)
            continue;
        if (hero.man_goods[x] == id)
            return x;
    }
    return 0xFF;
}

/* ================= 以下:goods.s 物品清單 UI 段 ================= */

static void show_desc1(void)
{
    gi_show_desc(1);
}

static void show_desc2(void)
{
    gi_show_desc(2);
}

/* 等效 to_show_study:底部訊息 + 等鍵(A=y B=n;原機字母鍵 GBC 無) */
static uint8_t show_study(const uint8_t *msg)
{
    clear_nline2(FB_ROWS - 14, 14);
    format_string(msg);
    ss_ptr = OutBuf;
    show_one_line(0, FB_ROWS - 13);
    fb_flush();
    return wait_key();
}

/* 等效 skill.s study_cmd(讀秘笈)。成功路徑的 master1 學習介面
 * 隨拜師/學習模組接入(TODO ⑥)。回傳=選單解退層數 */
static uint8_t study_cmd(void)
{
    uint8_t i, off;

    if ((goods_id & 0x7F) == BAODIAN_GOODS && hero.man_gender != 2) {
        gi_clear_goods_desc();               /* hide item description */
        if (!confirm_box(1, FB_ROWS - 39, 159, FB_ROWS - 1,
                         mt_book_bao_msg))
            return 0;
        if (!confirm_box(1, FB_ROWS - 39, 159, FB_ROWS - 1,
                         mt_book_bao_msg2))
            return 0;
        hero.man_gender = 2;                /* 原版:自宮後本次即返回 */
        return 0;
    }
    if (find_kf(LITERATE_KF) == 0xFF) {
        show_study(mt_book_fail1_msg);
        return 0;
    }
    if (hero.man_pai != 0 && hero.man_pai != 7) {   /* NONE/XIAOYAO 之外 */
        show_study(mt_book_fail2_msg);
        return 0;
    }
    hero.man_pai = 7;                       /* XIAOYAO_PAI */

    /* 書表 → npc_kfnum/npc_kf(等效原版 5 字節拷貝),交給 master1 */
    {
        uint8_t bank_save = _current_bank;
        SWITCH_ROM(GAMEDATA_BANK);
        off = book_skill_off[goods_attr[2]];
        npc.npc_kfnum = book_skill_blob[off];
        for (i = 0; i < 4; i++)
            npc_kf[i] = book_skill_blob[off + 1 + i];
        SWITCH_ROM(bank_save);
    }

    scroll_to_lcd();
    fb_flush();
    master1();                              /* TODO ⑥:學習選單 */
    scroll_to_lcd();
    fb_flush();
    return 2;                               /* 等效 PullMenu 1 */
}

/* 等效 use_goods(CR 處理器;goods_id 由顯示程序設定) */
static uint8_t use_goods(void)
{
    uint8_t x = find_goods(goods_id);

    if (x == 0xFF)
        return 0;
    get_goods_attr(goods_id & 0x7F);
    switch (goods_attr[0]) {
    case FOOD_WU:
        gi_use_food(x);
        return 1;
    case DRUG_WU:
        gi_use_drug(x);
        return 1;
    case WEAPON_WU:
        gi_use_weapon(x);
        return 1;
    case EQUIP_WU:
        gi_use_equip(x);
        return 1;
    case BOOK_WU:
        return study_cmd();
    default:
        return 1;                           /* OTHER:僅退出 */
    }
}

/* 等效 diu_goods:未裝備才減一件 */
static uint8_t diu_goods(void)
{
    uint8_t x = find_goods(goods_id);

    if (x != 0xFF && !(hero.man_goods[x] & 0x80))
        hero.man_goods[x + 1]--;
    return 1;
}

/* ---- 右側選單三式(佈局字節照原版) ---- */
static const menu_fn use_h[1] = { use_goods };
static const menu_fn diu_h[1] = { diu_goods };
static const show_fn d1_s[1] = { show_desc1 };
static const show_fn d2_s[1] = { show_desc2 };

/* right_menu(11111010b/80h/01010001b/BOX):2B 帶數量 */
static const cmenu_t right_menu =
    { 0, 1, 5, 0, MSTYLE_BOX, 10, MF_DIGITS, use_h, d2_s, 0, goods_name_tbl };
/* right_menu1(11011000b/80h/01010001b/CHECK1):1B 裝備勾 */
static const cmenu_t right_menu1 =
    { 0, 1, 5, 0, MSTYLE_CHECK1, 8, 0, use_h, d1_s, 0, goods_name_tbl };
/* right_menu3(丟棄) */
static const cmenu_t right_menu3 =
    { 0, 1, 5, 0, MSTYLE_BOX, 10, MF_DIGITS, diu_h, d2_s, 0, goods_name_tbl };

static void gd_clear_right(void)
{
    clean_list_right();
    gi_clear_goods_desc();
}

static void show_food(void)
{
    gd_clear_right();
    goods_type = menu_set;                  /* 食物0 藥品1 武器2 裝備3 */
    gi_set_goods();
    show_menu_txt(list_x0 + 5, list_y0, &right_menu);
    fb_flush();
}

static uint8_t deal_food(void)
{
    goods_type = menu_set;
    gi_set_goods();
    return (pop_menu(list_x0 + 5, list_y0, &right_menu) == 0xFE) ? 2 : 0;
}

static void show_equip(void)
{
    gd_clear_right();
    goods_type = menu_set;
    gi_set_goods();
    show_menu_txt(list_x0 + 5, list_y0, &right_menu1);
    fb_flush();
}

static uint8_t deal_equip(void)
{
    goods_type = menu_set;
    gi_set_goods();
    return (pop_menu(list_x0 + 5, list_y0, &right_menu1) == 0xFE) ? 2 : 0;
}

static void show_other(void)
{
    gd_clear_right();
    gi_set_other_goods();
    show_menu_txt(list_x0 + 5, list_y0, &right_menu);
    fb_flush();
}

static uint8_t deal_other(void)
{
    gi_set_other_goods();
    return (pop_menu(list_x0 + 5, list_y0, &right_menu) == 0xFE) ? 2 : 0;
}

static void show_all(void)
{
    gd_clear_right();
    set_all_goods();
    show_menu_txt(list_x0 + 5, list_y0, &right_menu3);
    fb_flush();
}

static uint8_t deal_diu(void)
{
    set_all_goods();
    pop_menu(list_x0 + 5, list_y0, &right_menu3);
    return 0;
}

static const menu_fn goods_h[6] = {
    deal_food, deal_food, deal_equip, deal_equip, deal_other, deal_diu,
};
static const show_fn goods_s[6] = {
    show_food, show_food, show_equip, show_equip, show_other, show_all,
};
/* goods_menu(01000000b/6/01010001b/NORMAL) */
static const cmenu_t goods_left_menu =
    { 6, 1, 5, 0, MSTYLE_NORMAL, 0, 0, goods_h, goods_s,
      mt_goods_menu_items, 0 };

/* 等效 goods.s show_goods(主選單「物品」) */
uint8_t show_goods(void)
{
    list_menu(48, 4, &goods_left_menu);     /* FRAME_X0, FRAME_Y0 */
    scroll_to_lcd();
    fb_flush();
    return 0;
}
