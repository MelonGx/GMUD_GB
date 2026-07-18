/* goods.c - 物品查詢,逐例程移植原版 goods.s(+ aux.s find_name)
 *
 * 名字/屬性表在 ROM bank 24(gamedata.c),存取時臨時切 bank。
 * 原版 find_name 回傳表內指針;GBC 端表在 banked ROM,改拷入
 * 3 格輪替緩衝(npc_look 帶著三件裝備需同時持有 3 個名字)。
 */
#include <gb/gb.h>
#include <string.h>
#include "gamedata.h"
#include "goods.h"
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

/* 名字輪替池:UI 期間 gfx_scratch 複用(配置見 text.h) */
#define name_pool (gfx_scratch + 100)
static uint8_t name_rot;

/* 等效 aux.s find_name:0xFF(或 0,pai 表)結尾條目線性走訪至第 id 條 */
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

/* 等效 goods.s add_goods:取得一件 goods_id。1=成功 */
uint8_t add_goods(uint8_t id)
{
    uint8_t x = find_goods(id);

    if (x != 0xFF) {
        get_goods_attr(id);
        if (goods_attr[0] != WEAPON_WU && goods_attr[0] != EQUIP_WU) {
            hero.man_goods[x + 1]++;        /* 可疊加 */
            return 1;
        }
    }
    for (x = 0; x < MAX_GOODS * 2; x += 2) {
        if (hero.man_goods[x + 1] == 0) {
            hero.man_goods[x] = id;
            hero.man_goods[x + 1] = 1;
            return 1;
        }
    }
    return 0;
}

/* ================= 以下:goods.s 物品清單 UI 段 ================= */

static uint8_t goods_type, goods_id;    /* 原版同名(goods_id 含 bit7) */
static uint8_t sg_n, sg_y;              /* set_goods 條數/字節游標 */

/* 等效 set_goods_l:掃 man_goods 收 goods_type 類(武器/裝備 1B,其餘 2B) */
static void set_goods_l(void)
{
    uint8_t x;

    for (x = 0; x < MAX_GOODS * 2; x += 2) {
        if (hero.man_goods[x + 1] == 0)
            continue;
        get_goods_attr(hero.man_goods[x] & 0x7F);
        if (goods_attr[0] != goods_type)
            continue;
        sg_n++;
        dmenu_buf[1 + sg_y++] = hero.man_goods[x];
        if (goods_type != WEAPON_WU && goods_type != EQUIP_WU)
            dmenu_buf[1 + sg_y++] = hero.man_goods[x + 1];
    }
    dmenu_buf[0] = sg_n;
}

static void set_goods(void)
{
    sg_n = 0;
    sg_y = 0;
    set_goods_l();
}

/* 等效 set_other_goods:雜物 = 秘笈 + 其他 */
static void set_other_goods(void)
{
    goods_type = BOOK_WU;
    set_goods();
    goods_type = OTHER_WU;
    set_goods_l();
}

/* 等效 set_all_goods:全部(丟棄頁/賣出頁),擋三角鐵 */
void set_all_goods(void)
{
    uint8_t x, n = 0;

    for (x = 0; x < MAX_GOODS * 2; x += 2) {
        if (hero.man_goods[x + 1] == 0)
            continue;
        if (hero.man_goods[x] == SANJIAO_GOODS)
            continue;
        dmenu_buf[1 + (n << 1)] = hero.man_goods[x];
        dmenu_buf[2 + (n << 1)] = hero.man_goods[x + 1];
        n++;
    }
    dmenu_buf[0] = n;
}

/* 等效 get_goods_desc:GOODS_DESC 文本 → 工作區(雙 0 收尾)
 * 原版逐字節讀到第一個 \0 即停,再補一個 \0(雙零收尾);
 * 這裡批量讀後掃描補零,避免鄰接條目的文本溢入緩衝區 */
#define goods_desc_buf (gfx_scratch + 192)

static void get_goods_desc(uint8_t id)
{
    uint16_t off = text_get_ptr(TC_GOODS_DESC, id);
    uint8_t i;

    res_read(&textbank_res, off, goods_desc_buf, 180);
    for (i = 0; i < 180; i++) {
        if (goods_desc_buf[i] == 0) {
            goods_desc_buf[i + 1] = 0;
            return;
        }
    }
    goods_desc_buf[180] = 0;
    goods_desc_buf[181] = 0;
}

static void clear_goods_desc(void)
{
    uint8_t y = list_y1 + 2;

    clear_nline2(y, FB_ROWS - y);
}

/* 160x144: use the whole lower panel and wrap every description at once. */
static void goods_show_desc(uint8_t stride)
{
    uint8_t line, y;

    goods_id = dmenu_buf[1 + (uint16_t)menu_set * stride];
    get_goods_desc(goods_id & 0x7F);
    ss_ptr = goods_desc_buf;
    clear_goods_desc();
    y = list_y1 + 3;
    for (line = 0; line < 5 && *ss_ptr; line++, y += 13)
        show_one_line(0, y);
    fb_flush();
}

static void show_desc1(void)
{
    goods_show_desc(1);
}

static void show_desc2(void)
{
    goods_show_desc(2);
}

/* ---- 使用(等效 use_goods 家族;attack/defense 負值鉗 0 照原版) ---- */
static void use_food(uint8_t x)
{
    if (hero.man_food >= hero.man_maxfood)
        return;
    hero.man_goods[x + 1]--;
    hero.man_food += goods_attr[2];
    hero.man_water += goods_attr[3];
}

static void use_drug(uint8_t x)
{
    uint16_t a2;

    if (goods_attr[1] == 0) {               /* 恢復精血上限 */
        if (hero.man_effhp >= hero.man_maxhp)
            return;
        hero.man_goods[x + 1]--;
        a2 = hero.man_maxhp >> 1;
        if (goods_attr[2])
            a2 >>= 1;
        hero.man_effhp += a2;
        if (hero.man_effhp > hero.man_maxhp)
            hero.man_effhp = hero.man_maxhp;
    } else if (goods_attr[1] == 1) {        /* 增內力上限 */
        hero.man_goods[x + 1]--;
        hero.man_maxfp += goods_attr[2];
    }
}

static void wield_adjust(int8_t sign)
{
    int16_t v;

    v = (int16_t)hero.man_attack + sign * (int8_t)goods_attr[3];
    hero.man_attack = (v < 0) ? 0 : (uint8_t)v;
    v = (int16_t)hero.man_defense + sign * (int8_t)goods_attr[4];
    hero.man_defense = (v < 0) ? 0 : (uint8_t)v;
}

static void use_weapon(uint8_t x)
{
    uint16_t v;

    if (!(hero.man_weapon & 0x80)) {        /* 佩掛 */
        hero.man_goods[x] = goods_id ^ 0x80;
        hero.man_weapon = goods_id ^ 0x80;
        v = hero.man_damage + goods_attr[2];
        hero.man_damage = (v > 255) ? 255 : (uint8_t)v;
        wield_adjust(1);
    } else if (goods_id & 0x80) {           /* 卸下(選中=裝備中那把) */
        hero.man_goods[x] = goods_id ^ 0x80;
        hero.man_weapon = goods_id ^ 0x80;
        hero.man_damage = (hero.man_damage >= goods_attr[2])
                              ? hero.man_damage - goods_attr[2] : 0;
        wield_adjust(-1);
    }
}

static void use_equip(uint8_t x)
{
    uint8_t slot = goods_attr[1];
    uint16_t v;

    if (!(hero.man_equip[slot] & 0x80)) {
        hero.man_goods[x] = goods_id ^ 0x80;
        hero.man_equip[slot] = goods_id ^ 0x80;
        v = hero.man_armor + goods_attr[2];
        hero.man_armor = (v > 255) ? 255 : (uint8_t)v;
        wield_adjust(1);
    } else if (goods_id & 0x80) {
        hero.man_goods[x] = goods_id ^ 0x80;
        hero.man_equip[slot] = goods_id ^ 0x80;
        hero.man_armor = (hero.man_armor >= goods_attr[2])
                             ? hero.man_armor - goods_attr[2] : 0;
        wield_adjust(-1);
    }
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
        clear_goods_desc();                  /* hide item description */
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
        use_food(x);
        return 1;
    case DRUG_WU:
        use_drug(x);
        return 1;
    case WEAPON_WU:
        use_weapon(x);
        return 1;
    case EQUIP_WU:
        use_equip(x);
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
    clear_goods_desc();
}

static void show_food(void)
{
    gd_clear_right();
    goods_type = menu_set;                  /* 食物0 藥品1 武器2 裝備3 */
    set_goods();
    show_menu_txt(list_x0 + 5, list_y0, &right_menu);
    fb_flush();
}

static uint8_t deal_food(void)
{
    goods_type = menu_set;
    set_goods();
    return (pop_menu(list_x0 + 5, list_y0, &right_menu) == 0xFE) ? 2 : 0;
}

static void show_equip(void)
{
    gd_clear_right();
    goods_type = menu_set;
    set_goods();
    show_menu_txt(list_x0 + 5, list_y0, &right_menu1);
    fb_flush();
}

static uint8_t deal_equip(void)
{
    goods_type = menu_set;
    set_goods();
    return (pop_menu(list_x0 + 5, list_y0, &right_menu1) == 0xFE) ? 2 : 0;
}

static void show_other(void)
{
    gd_clear_right();
    set_other_goods();
    show_menu_txt(list_x0 + 5, list_y0, &right_menu);
    fb_flush();
}

static uint8_t deal_other(void)
{
    set_other_goods();
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
