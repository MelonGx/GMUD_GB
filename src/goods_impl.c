/* goods_impl.c - goods.s 物品實作層(HOME 遷出,見 goods_impl.h)
 *
 * 這裡放的是「直接以名字呼叫」的部分:名字/屬性查詢、背包增減、
 * 分類掃描、描述繪製、各類使用效果。全部不碰 menutext(bank 26)的
 * mt_* 字串,也不被函式指標表引用,所以搬進 banked 區安全。
 */
#pragma bank 31
#include <gb/gb.h>
#include <string.h>
#include "gamedata.h"
#include "goods.h"
#include "goods_impl.h"
#include "save.h"
#include "text.h"
#include "menu.h"
#include "ui.h"
#include "fb.h"
#include "res.h"
#include "textbank_res.h"

/* 本檔嚴禁出現 SWITCH_ROM:程式碼就在 0x4000 窗裡,切走那個窗等於切走
 * 自己(fight.c:121 記的 2026-07-09 教訓)。gd_find_name 因此留在 HOME。
 * 需要讀 gamedata 就呼叫 HOME 的 get_goods_attr/gd_find_name,它們在
 * HOME(0x0000-0x3FFF)執行,切窗不會切掉自身。 */

/* 等效 goods.s add_goods:取得一件 goods_id。1=成功 */
uint8_t add_goods(uint8_t id) BANKED
{
    uint8_t x = find_goods(id);

    if (x != 0xFF) {
        get_goods_attr(id);
        if (goods_attr[0] != WEAPON_WU && goods_attr[0] != EQUIP_WU) {
            if (hero.man_goods[x + 1] == 0xFF)
                return 0;                  /* 疊滿:不可回捲成空格 */
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

/* ---- 分類掃描 ---- */
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

void gi_set_goods(void) BANKED
{
    sg_n = 0;
    sg_y = 0;
    set_goods_l();
}

/* 等效 set_other_goods:雜物 = 秘笈 + 其他 */
void gi_set_other_goods(void) BANKED
{
    goods_type = BOOK_WU;
    gi_set_goods();
    goods_type = OTHER_WU;
    set_goods_l();
}

/* 等效 set_all_goods:全部(丟棄頁/賣出頁),擋三角鐵 */
void set_all_goods(void) BANKED
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

void gi_clear_goods_desc(void) BANKED
{
    uint8_t y = list_y1 + 2;

    clear_nline2(y, FB_ROWS - y);
}

/* 160x144: use the whole lower panel and wrap every description at once. */
void gi_show_desc(uint8_t stride) BANKED
{
    uint8_t line, y;

    goods_id = dmenu_buf[1 + (uint16_t)menu_set * stride];
    get_goods_desc(goods_id & 0x7F);
    ss_ptr = goods_desc_buf;
    gi_clear_goods_desc();
    y = list_y1 + 3;
    for (line = 0; line < 5 && *ss_ptr; line++, y += 13)
        show_one_line(0, y);
    fb_flush();
}

/* ---- 使用(等效 use_goods 家族;attack/defense 負值鉗 0 照原版) ---- */
void gi_use_food(uint8_t x) BANKED
{
    if (hero.man_food >= hero.man_maxfood)
        return;
    hero.man_goods[x + 1]--;
    hero.man_food += goods_attr[2];
    hero.man_water += goods_attr[3];
}

void gi_use_drug(uint8_t x) BANKED
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

/* 絕招擊落或投擲武器：與手動卸下相同地回退傷害、攻擊、防禦，
 * 但武器會少一件，且不再放回背包。 */
void lose_wielded_weapon(void) BANKED
{
    uint8_t weapon = hero.man_weapon;
    uint8_t x;

    if (!(weapon & 0x80))
        return;

    get_goods_attr(weapon & 0x7F);
    hero.man_damage = (hero.man_damage >= goods_attr[2])
                          ? hero.man_damage - goods_attr[2] : 0;
    wield_adjust(-1);

    x = find_goods(weapon);
    if (x != 0xFF && hero.man_goods[x + 1])
        hero.man_goods[x + 1]--;
    hero.man_weapon = 0;
}

void gi_use_weapon(uint8_t x) BANKED
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

void gi_use_equip(uint8_t x) BANKED
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
