/* battle_loot.c - 戰後滿包時的即時、多選、原子替換流程
 *
 * 這不是事後背包整理入口。npc_fight() 在返回地圖前同步呼叫本模組；
 * 玩家必須選足要犧牲的格數，確認後才一次寫入背包與戰利品。
 */
#pragma bank 31

#include <gb/gb.h>
#include <string.h>
#include "battle_loot.h"
#include "fb.h"
#include "font.h"
#include "gamedata.h"
#include "goods.h"
#include "menu.h"
#include "save.h"
#include "text.h"
#include "ui.h"

#define LOOT_MAX        4
#define SIM_SLOTS       (MAX_GOODS + LOOT_MAX)
#define PICK_ROWS       5
#define LIST_Y          44
#define ROW_H           13

/* UI 期間的 gfx_scratch 分時區。npc.c 必須在 resolver 返回後才於
 * +192/+256 組戰利品模板與名字。 */
#define loot_slot_map   (gfx_scratch + 178)  /* candidate -> man_goods byte off */
#define loot_sim        (gfx_scratch + 200)  /* 24 slots = 48B */
#define loot_final      (gfx_scratch + 248)  /* 20 slots = 40B */

static const uint8_t txt_full[] = {
    0xB1,0xB3,0xB0,0xFC,0xD2,0xD1,0xC2,0xFA,0       /* 背包已满 */
};
static const uint8_t txt_need[] = {
    0xD0,0xE8,0xB6,0xAA,0xC6,0xFA,0                 /* 需丢弃 */
};
static const uint8_t txt_cell[] = { 0xB8,0xF1,0 };   /* 格 */
static const uint8_t txt_gain[] = {
    0xBB,0xF1,0xB5,0xC3,0x3A,0                       /* 获得: */
};
static const uint8_t txt_chosen[] = {
    0xD2,0xD1,0xD1,0xA1,0xD4,0xF1,0                 /* 已选择 */
};
static const uint8_t txt_hint[] = {
    'A',0xD1,0xA1,0xD4,0xF1,'/',                    /* A选择/确定 */
    0xC8,0xB7,0xB6,0xA8,' ',
    'B',0xB3,0xB7,0xCF,0xFA,0                       /* B撤销 */
};

static const uint8_t shiban_tbl[7] = { 0, 1, 2, 4, 8, 16, 32 };
static const uint8_t loot_head[] = {            /* 大获全胜!战斗获得\0金钱: */
    0xB4,0xF3,0xBB,0xF1,0xC8,0xAB,0xCA,0xA4,0x21,
    0xD5,0xBD,0xB6,0xB7,0xBB,0xF1,0xB5,0xC3,0x00,
    0xBD,0xF0,0xC7,0xAE,0x3A,
};
static const uint8_t loot_goods[] = {           /* 物品: */
    0xCE,0xEF,0xC6,0xB7,0x3A,
};

static uint8_t picker_used;

static void putw(uint8_t *p, const void *v)
{
    uint16_t w = (uint16_t)v;
    p[0] = (uint8_t)w;
    p[1] = (uint8_t)(w >> 8);
}

static uint8_t is_stackable(uint8_t id)
{
    get_goods_attr(id);
    return goods_attr[0] != WEAPON_WU && goods_attr[0] != EQUIP_WU;
}

/* add_goods 的無副作用陣列版；slots 可為 20 或 24。 */
static uint8_t sim_add(uint8_t *bag, uint8_t slots, uint8_t id)
{
    uint8_t x;

    for (x = 0; x < (uint8_t)(slots << 1); x += 2) {
        if (bag[x + 1] && bag[x] == id) {
            if (!is_stackable(id))
                break;
            if (bag[x + 1] == 0xFF)
                return 0;
            bag[x + 1]++;
            return 1;
        }
    }
    for (x = 0; x < (uint8_t)(slots << 1); x += 2) {
        if (!bag[x + 1]) {
            bag[x] = id;
            bag[x + 1] = 1;
            return 1;
        }
    }
    return 0;
}

static uint8_t extra_slots_used(void)
{
    uint8_t x, n = 0;

    for (x = MAX_GOODS << 1; x < SIM_SLOTS << 1; x += 2) {
        if (loot_sim[x + 1])
            n++;
    }
    return n;
}

static uint8_t incoming_recreates(uint8_t id, const uint8_t *ids,
                                  uint8_t count, uint8_t got_mask)
{
    uint8_t i;

    if (!is_stackable(id))
        return 0;
    for (i = 0; i < count; i++) {
        if ((got_mask & (uint8_t)(1u << i)) && ids[i] == id)
            return 1;
    }
    return 0;
}

static uint8_t build_candidates(const uint8_t *ids, uint8_t count,
                                uint8_t got_mask)
{
    uint8_t x, n = 0;

    for (x = 0; x < MAX_GOODS << 1; x += 2) {
        uint8_t id = hero.man_goods[x];

        if (!hero.man_goods[x + 1] || (id & 0x80))
            continue;
        if (id == SANJIAO_GOODS)
            continue;
        /* 丟掉一疊即將由本批戰利品重新建立的物品，淨空格數仍為零；
         * 不把這種假選項列給玩家。武器/防具不堆疊，仍可替換。 */
        if (incoming_recreates(id, ids, count, got_mask))
            continue;
        loot_slot_map[n] = x;
        dmenu_buf[1 + (n << 1)] = id;
        dmenu_buf[2 + (n << 1)] = hero.man_goods[x + 1];
        n++;
    }
    dmenu_buf[0] = n;
    return n;
}

static void draw_digit(uint8_t x, uint8_t y, uint8_t v)
{
    uint8_t hundreds = v / 100;
    uint8_t tens = (v / 10) % 10;

    if (hundreds)
        x = font_draw_ascii(x, y, (uint8_t)('0' + hundreds));
    if (hundreds || tens)
        x = font_draw_ascii(x, y, (uint8_t)('0' + tens));
    font_draw_ascii(x, y, (uint8_t)('0' + v % 10));
}

static uint8_t candidate_selected(uint8_t idx)
{
    return dmenu_buf[1 + (idx << 1)] & 0x80;
}

/* 游標只佔 x=1..20。不要走 ui_invert 的逐 pixel 除法/位址迴圈；
 * 直接翻三個 byte，兩列移動由 4108 次 pixel 操作降為 78 次 XOR。 */
static void invert_cursor(uint8_t y)
{
    uint8_t r;

    for (r = 0; r < ROW_H; r++) {
        uint8_t *row = fb + (uint16_t)(y + r) * FB_STRIDE;
        row[0] ^= 0x7F;                   /* pixel 1..7 */
        row[1] ^= 0xFF;                   /* pixel 8..15 */
        row[2] ^= 0xF8;                   /* pixel 16..20 */
    }
    fb_mark_dirty(y, ROW_H);
}

static void draw_marker(uint8_t idx, uint8_t y)
{
    font_draw_ascii(2, y, '[');
    font_draw_ascii(8, y, candidate_selected(idx) ? 'x' : ' ');
    font_draw_ascii(14, y, ']');
}

static void draw_goods_name(uint8_t x, uint8_t y, uint8_t id)
{
    const uint8_t *src = get_goods_name(id);
    uint8_t buf[12];
    uint8_t i = 0;

    while (i < sizeof buf - 1 && src[i] && src[i] != 0xFF) {
        buf[i] = src[i];
        i++;
    }
    buf[i] = 0;
    font_draw_text(x, y, buf);
}

static void draw_candidate(uint8_t idx, uint8_t y)
{
    uint8_t id = dmenu_buf[1 + (idx << 1)] & 0x7F;
    uint8_t qty = dmenu_buf[2 + (idx << 1)];

    fb_fill_rect(1, y, 158, ROW_H, 0);
    draw_marker(idx, y);
    draw_goods_name(24, y, id);
    font_draw_ascii(124, y, 'x');
    draw_digit(132, y, qty);
}

static void draw_status(uint8_t selected, uint8_t need)
{
    fb_fill_rect(0, 110, 160, 17, 0);
    font_draw_text(35, 112, txt_chosen);
    fb_fill_rect(75, 110, 30, 17, 0);
    draw_digit(77, 112, selected);
    font_draw_ascii(85, 112, '/');
    draw_digit(93, 112, need);
}

static void redraw_status_digits(uint8_t selected, uint8_t need)
{
    fb_fill_rect(75, 110, 30, 17, 0);
    draw_digit(77, 112, selected);
    font_draw_ascii(85, 112, '/');
    draw_digit(93, 112, need);
}

static void draw_page(uint8_t cur, uint8_t candidate_count,
                      uint8_t selected, uint8_t need)
{
    uint8_t start = (uint8_t)(cur / PICK_ROWS) * PICK_ROWS;
    uint8_t row, idx;

    fb_fill_rect(0, LIST_Y, 160, PICK_ROWS * ROW_H, 0);
    for (row = 0; row < PICK_ROWS; row++) {
        idx = start + row;
        if (idx >= candidate_count)
            break;
        draw_candidate(idx, LIST_Y + row * ROW_H);
    }
    draw_status(selected, need);
    /* 游標只反白 20px 的 [ ] 標記。整行反白在 CGB 實測需約 18 幀，
     * 會令重畫期間的操作顯得沒有回應。 */
    invert_cursor(LIST_Y + (cur - start) * ROW_H);
    fb_flush();
}

static void draw_incoming(const uint8_t *ids, uint8_t count,
                          uint8_t got_mask)
{
    uint8_t i, shown = 0;

    font_draw_text(2, 17, txt_gain);
    for (i = 0; i < count && shown < LOOT_MAX; i++) {
        uint8_t x, y;

        if (!(got_mask & (uint8_t)(1u << i)))
            continue;
        if (shown < 2) {
            x = shown ? 100 : 34;
            y = 17;
        } else {
            x = (shown == 2) ? 34 : 100;
            y = 30;
        }
        draw_goods_name(x, y, ids[i]);
        shown++;
    }
}

static void draw_picker(const uint8_t *ids, uint8_t count,
                        uint8_t got_mask, uint8_t need, uint8_t cur,
                        uint8_t candidate_count, uint8_t selected)
{
    fb_clear();
    font_set_height(12);
    font_draw_text(2, 2, txt_full);
    font_draw_text(62, 2, txt_need);
    draw_digit(104, 2, need);
    font_draw_text(112, 2, txt_cell);
    draw_incoming(ids, count, got_mask);
    font_draw_text(32, 129, txt_hint);
    draw_page(cur, candidate_count, selected, need);
}

static void redraw_current_marker(uint8_t cur, uint8_t selected,
                                  uint8_t need)
{
    uint8_t start = (uint8_t)(cur / PICK_ROWS) * PICK_ROWS;
    uint8_t y = LIST_Y + (cur - start) * ROW_H;

    invert_cursor(y);                    /* 暫時還原未反白像素 */
    fb_fill_rect(8, y, 6, ROW_H, 0);
    font_draw_ascii(8, y, candidate_selected(cur) ? 'x' : ' ');
    invert_cursor(y);
    redraw_status_digits(selected, need);
    fb_flush();
}

static void redraw_deselected(uint8_t old_cur, uint8_t cur,
                              uint8_t selected, uint8_t need)
{
    uint8_t page = cur / PICK_ROWS;

    if (old_cur == cur) {
        redraw_current_marker(cur, selected, need);
    } else if (old_cur / PICK_ROWS != page) {
        draw_page(cur, dmenu_buf[0], selected, need);
    } else {
        uint8_t start = page * PICK_ROWS;
        uint8_t old_y = LIST_Y + (old_cur - start) * ROW_H;
        uint8_t y = LIST_Y + (cur - start) * ROW_H;

        invert_cursor(old_y);
        fb_fill_rect(8, y, 6, ROW_H, 0);
        font_draw_ascii(8, y, ' ');
        invert_cursor(y);
        redraw_status_digits(selected, need);
        fb_flush();
    }
}

static uint8_t choose_slots(const uint8_t *ids, uint8_t count,
                            uint8_t got_mask, uint8_t need,
                            uint8_t candidate_count)
{
    uint8_t cur = 0, selected = 0;
    uint8_t prev, repeat_key = 0, repeat_ticks = 0;

    /* 先清戰鬥結束訊息殘留的 A，再畫清單。如此玩家在繪製期間按下
     * 方向鍵且仍按住時，首輪輪詢仍會把它視為新輸入，不會吞鍵。 */
    waitpadup();
    draw_picker(ids, count, got_mask, need, cur, candidate_count, selected);
    prev = 0;

    for (;;) {
        uint8_t j, pressed, move = 0, old, old_page;

        vsync();
        heart_beat();
        j = joypad();
        pressed = j & (uint8_t)~prev;

        if (j & (J_UP | J_DOWN | J_LEFT | J_RIGHT)) {
            uint8_t held = j & (J_UP | J_DOWN | J_LEFT | J_RIGHT);
            if (held != repeat_key) {
                repeat_key = held;
                repeat_ticks = 0;
            } else if (repeat_ticks < 0xFF) {
                repeat_ticks++;
            }
            if (pressed & held)
                move = pressed & held;
            else if (repeat_ticks >= 12 && !(repeat_ticks & 3))
                move = held;
        } else {
            repeat_key = 0;
            repeat_ticks = 0;
        }
        prev = j;

        if (pressed & J_A) {
            uint8_t *entry = &dmenu_buf[1 + (cur << 1)];

            if (selected == need)
                return selected;            /* 同頁第二次 A 原子提交 */
            if (*entry & 0x80) {
                *entry &= 0x7F;
                selected--;
            } else if (selected < need) {
                *entry |= 0x80;
                selected++;
            }
            redraw_current_marker(cur, selected, need);
            continue;
        }

        if (pressed & J_B) {
            uint8_t old_cur = cur;
            uint8_t idx = cur;
            uint8_t found = 0;

            if (!candidate_selected(idx)) {
                for (idx = candidate_count; idx; idx--) {
                    if (candidate_selected(idx - 1)) {
                        idx--;
                        found = 1;
                        break;
                    }
                }
                if (!found)
                    continue;           /* 強制現場處理，B 不能退出 */
                cur = idx;
            }
            dmenu_buf[1 + (cur << 1)] &= 0x7F;
            selected--;
            redraw_deselected(old_cur, cur, selected, need);
            continue;
        }

        if (!move)
            continue;
        old = cur;
        old_page = old / PICK_ROWS;
        if (move & J_UP)
            cur = cur ? cur - 1 : candidate_count - 1;
        else if (move & J_DOWN)
            cur = (uint8_t)(cur + 1) < candidate_count ? cur + 1 : 0;
        else if (move & J_LEFT) {
            cur = (cur >= PICK_ROWS) ? cur - PICK_ROWS : 0;
        } else if (move & J_RIGHT) {
            uint8_t next = cur + PICK_ROWS;
            cur = (next < candidate_count) ? next : candidate_count - 1;
        }
        if (cur == old)
            continue;
        if (old_page != cur / PICK_ROWS) {
            draw_page(cur, candidate_count, selected, need);
        } else {
            uint8_t start = (uint8_t)(cur / PICK_ROWS) * PICK_ROWS;
            invert_cursor(LIST_Y + (old - start) * ROW_H);
            invert_cursor(LIST_Y + (cur - start) * ROW_H);
            fb_flush();
        }
    }
}

uint8_t battle_loot_resolve(uint8_t *ids, uint8_t count) BANKED
{
    uint8_t i, got_mask = 0, need, candidate_count;

    picker_used = 0;
    if (count > LOOT_MAX)
        count = LOOT_MAX;
    if (!count)
        return 0;

    memcpy(loot_sim, hero.man_goods, MAX_GOODS << 1);
    memset(loot_sim + (MAX_GOODS << 1), 0, LOOT_MAX << 1);
    for (i = 0; i < count; i++) {
        ids[i] &= 0x7F;
        if (sim_add(loot_sim, SIM_SLOTS, ids[i]))
            got_mask |= (uint8_t)(1u << i);
    }
    need = extra_slots_used();
    if (!got_mask)
        return 0;                           /* 全為 255 疊滿等不可加入項 */
    if (!need) {
        memcpy(hero.man_goods, loot_sim, MAX_GOODS << 1);
        return got_mask;
    }

    candidate_count = build_candidates(ids, count, got_mask);
    if (candidate_count < need)
        return 0;                           /* 合法存檔不會發生 */
    picker_used = 1;
    choose_slots(ids, count, got_mask, need, candidate_count);

    memcpy(loot_final, hero.man_goods, MAX_GOODS << 1);
    for (i = 0; i < candidate_count; i++) {
        if (candidate_selected(i)) {
            uint8_t x = loot_slot_map[i];
            loot_final[x] = 0;
            loot_final[x + 1] = 0;
        }
    }
    for (i = 0; i < count; i++) {
        if ((got_mask & (uint8_t)(1u << i))
            && !sim_add(loot_final, MAX_GOODS, ids[i]))
            return 0;                       /* 原子守門：絕不提交半套結果 */
    }
    memcpy(hero.man_goods, loot_final, MAX_GOODS << 1);
    return got_mask;
}

void battle_loot_collect_npc(void) BANKED
{
    uint8_t i, g, n = 0, got_mask;
    uint8_t loot_ids[LOOT_MAX], loot_slots[LOOT_MAX], loot_bits[LOOT_MAX];
    uint8_t *p;
    uint8_t *names;

    hero.man_money += npc.npc_money;

    /* resolver 會分時複用 gfx_scratch；此處先只保留四件小型描述。 */
    for (i = 0; i < LOOT_MAX; i++) {
        uint8_t bit = 0;

        g = npc.npc_goods[i];
        if (!g)
            continue;
        if (g == SANJIAO_GOODS) {
            bit = shiban_tbl[(npc.npc_pai < 7) ? npc.npc_pai : 0];
            if (hero.shiban & bit)
                continue;
        }
        loot_ids[n] = g & 0x7F;
        loot_slots[n] = i;
        loot_bits[n] = bit;
        n++;
    }

    got_mask = battle_loot_resolve(loot_ids, n);
    if (picker_used)
        fb_clear();                         /* 清掉強制選擇器再畫結算框 */

    p = gfx_scratch + 192;
    names = gfx_scratch + 256;
    for (i = 0; i < LOOT_MAX; i++) {
        varbuf[i << 1] = 0;
        varbuf[(i << 1) + 1] = 0;
    }
    for (i = 0; i < n; i++) {
        uint8_t slot;

        if (!(got_mask & (uint8_t)(1u << i)))
            continue;
        if (loot_bits[i])
            hero.shiban |= loot_bits[i];
        slot = loot_slots[i];
        memcpy(names + (slot * 12), get_goods_name(loot_ids[i]), 12);
        putw(varbuf + (slot << 1), names + (slot * 12));
    }

    memcpy(p, loot_head, sizeof loot_head);
    p += sizeof loot_head;
    *p++ = 2;                              /* type2: npc_money */
    putw(p, &npc.npc_money);
    p += 2;
    *p++ = 0;
    *p++ = 0;
    memcpy(p, loot_goods, sizeof loot_goods);
    p += sizeof loot_goods;
    for (i = 0; i < LOOT_MAX; i++) {
        if (i)
            *p++ = ' ';
        *p++ = 8;                          /* type8: 名字指針槽 */
        putw(p, varbuf + (i << 1));
        p += 2;
        if (i < LOOT_MAX - 1) {
            *p++ = 10;
            *p++ = 0;
        } else {
            *p++ = 0;
            *p++ = 0;
        }
    }
    *p = 0;

    format_string(gfx_scratch + 192);
    message_box_more(6, 3, 150, 75);
}
