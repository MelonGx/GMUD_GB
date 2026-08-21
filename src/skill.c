/* skill.c - 武功查詢,逐例程移植原版 skill.s、lee3.s find_kf、
 * npc.s query_npc_skill/find_npc_kf、aux.s set_obj
 *
 * obj 多態:原版 set_obj 填 obj_ptr + func_buf 跳轉表(man/npc 兩套);
 * C 端以 obj_is_npc 旗標分派,obj_read 按 fight_off 偏移讀
 * man_state(hero.man_name 起)或 npc_state。
 */
#include <gb/gb.h>
#include <string.h>
#include "game.h"       /* busy_flag */
#include "gamedata.h"
#include "goods.h"
#include "skill.h"
#include "save.h"
#include "text.h"
#include "menu.h"
#include "menutext.h"
#include "ui.h"
#include "font.h"
#include "fb.h"
#include "blit.h"
#include "serve.h"
#include "menu_input.h"
#include "process_input.h"

/* fight_off 偏移(h/id.h,man_state/npc_state 同構前 41 字節) */
#define DAMAGE_OFF 16
#define FORCE_OFF  22
#define STR_OFF    24
#define WEAPON_OFF 40

uint8_t kf_attr[2];
uint16_t skill_level;
uint8_t kf_type, kf_id;
uint8_t check_err;

static uint8_t obj_is_npc;

/* 等效 skill.s kf_basic_tbl / weapon_basic_tbl(字節照抄) */
static const uint8_t kf_basic_tbl[5] = {
    BASIC_BARE_KF, BASIC_SWORD_KF, BASIC_DODGE_KF,
    BASIC_FORCE_KF, BASIC_PARRY_KF,
};
static const uint8_t weapon_basic_tbl[10] = {
    /* 斧 刀 棍棒 匕 */
    0xFF, BASIC_BLADE_KF, BASIC_CLUB_KF, 0xFF,
    /* 叉 錘 劍 杖竿 */
    0xFF, 0xFF, BASIC_SWORD_KF, BASIC_STAFF_KF,
    /* 暗器 鞭 */
    0xFF, BASIC_WHIP_KF,
};

/* 等效 lee3.s find_kf:man_kf 4B/條(id 等級 潛能2B) */
uint8_t find_kf(uint8_t id)
{
    uint8_t x, y;

    for (x = 0, y = 0; x < hero.man_kfnum; x++, y += 4) {
        if (hero.man_kf[y] == id)
            return y;
    }
    return 0xFF;
}

/* 等效 npc.s find_npc_kf:npc_kf 2B/條(id 等級) */
uint8_t find_npc_kf(uint8_t id)
{
    uint8_t x, y;

    for (x = 0, y = 0; x < npc.npc_kfnum; x++, y += 2) {
        if (npc_kf[y] == id)
            return y;
    }
    return 0xFF;
}

/* 等效 skill.s get_kf_attr(a1 = kf_attr_tbl + id*2) */
void get_kf_attr(uint8_t id)
{
    uint8_t bank_save = _current_bank;

    SWITCH_ROM(GAMEDATA_BANK);
    kf_attr[0] = kf_attr_tbl[(uint16_t)id << 1];
    kf_attr[1] = kf_attr_tbl[((uint16_t)id << 1) + 1];
    SWITCH_ROM(bank_save);
}

const uint8_t *get_kf_name(uint8_t id)
{
    return gd_find_name(kf_name_tbl, id);
}

const uint8_t *get_pf_name(uint8_t id)
{
    return gd_find_name(pf_name_tbl, id);
}

/* 等效 skill.s get_basic_kf:bit7=npc 側(取 npc_weapon) */
uint8_t get_basic_kf(uint8_t t)
{
    uint8_t weapon = hero.man_weapon;

    if (t & 0x80) {
        weapon = npc.npc_goods[0];      /* npc_weapon equ npc_goods */
        t &= 0x7F;
    }
    if (t == WEAPON_KF) {
        get_goods_attr(weapon & 0x7F);
        return weapon_basic_tbl[goods_attr[1]];
    }
    return kf_basic_tbl[t];
}

/* 等效 skill.s check_skill:武功使用條件(man 側) */
uint8_t check_skill(uint8_t id)
{
    uint8_t sub;

    get_kf_attr(id);
    switch (kf_attr[0]) {
    case HAND_KF:
        if (hero.man_weapon & 0x80)
            goto bad_weapon;
        if (find_kf(BASIC_BARE_KF) == 0xFF)
            goto bad_base;
        return 1;
    case WEAPON_KF:
        if (!(hero.man_weapon & 0x80))
            goto bad_weapon;
        sub = kf_attr[1];
        get_goods_attr(hero.man_weapon & 0x7F);
        if (sub != goods_attr[1])
            goto bad_weapon;
        if (find_kf(weapon_basic_tbl[sub]) == 0xFF)
            goto bad_base;
        return 1;
    case DODGE_KF:
        if (find_kf(BASIC_DODGE_KF) == 0xFF)
            goto bad_base;
        return 1;
    case FORCE_KF:
        if (find_kf(BASIC_FORCE_KF) == 0xFF)
            goto bad_base;
        return 1;
    default:
        return 1;
    }
bad_weapon:
    check_err = 1;
    return 0;
bad_base:
    check_err = 0;
    return 0;
}

/* 等效 skill.s query_skill:基本功等級/2 + 通過 check_skill 的招式等級 */
void query_skill(void)
{
    uint8_t y, u;

    skill_level = 0;
    y = find_kf(get_basic_kf(kf_type));
    if (y == 0xFF)
        return;
    skill_level = hero.man_kf[y + 1] >> 1;

    u = hero.man_usekf[kf_type];
    if (!(u & 0x80))
        return;
    if (!check_skill(u & 0x7F))
        return;
    y = find_kf(u & 0x7F);
    if (y == 0xFF)
        return;
    skill_level += hero.man_kf[y + 1];
}

/* 等效 npc.s query_npc_skill(npc 側不做 check_skill,照原版) */
void query_npc_skill(void)
{
    uint8_t y, u;

    skill_level = 0;
    y = find_npc_kf(get_basic_kf(kf_type | 0x80));
    if (y == 0xFF)
        return;
    skill_level = npc_kf[y + 1] >> 1;

    u = npc.npc_usekf[kf_type];
    if (!(u & 0x80))
        return;
    y = find_npc_kf(u & 0x7F);
    if (y == 0xFF)
        return;
    skill_level += npc_kf[y + 1];
}

void set_obj(uint8_t is_npc)
{
    obj_is_npc = is_npc;
}

static void obj_query(void)         /* 等效 jsr func_buf+QUERY_FUNC */
{
    if (obj_is_npc)
        query_npc_skill();
    else
        query_skill();
}

static uint8_t obj_read(uint8_t off)
{
    const uint8_t *p = obj_is_npc ? (const uint8_t *)&npc
                                  : (const uint8_t *)hero.man_name;
    return p[off];
}

static void gd_copy(uint8_t *dst, const uint8_t *src, uint8_t n)
{
    uint8_t bank_save = _current_bank;

    SWITCH_ROM(GAMEDATA_BANK);
    memcpy(dst, src, n);
    SWITCH_ROM(bank_save);
}

/* 等效 skill.s get_skill_desc:
 * 武藝 = (攻擊 + (輕功+招架)/2)/3/5 → skill_level_desc 第 n 條(8B)
 * 出手 = (膂力+傷害+內力低字節/2)/20,上限 5 → strong_lev_desc(4B) */
void get_skill_desc(void)
{
    uint16_t a8, a9, v;

    kf_type = (obj_read(WEAPON_OFF) & 0x80) ? WEAPON_KF : HAND_KF;
    obj_query();
    a9 = skill_level;
    kf_type = DODGE_KF;
    obj_query();
    a8 = skill_level;
    kf_type = PARRY_KF;
    obj_query();
    a8 = (a8 + skill_level) >> 1;

    v = (a9 + a8) / 3 / 5;
    if (v >= SKILL_LEVEL_DESC_COUNT)
        v = SKILL_LEVEL_DESC_COUNT - 1;
    gd_copy(skill_desc_buf,
            skill_level_desc + v * SKILL_LEVEL_DESC_STRIDE,
            SKILL_LEVEL_DESC_STRIDE);

    v = obj_read(STR_OFF);
    v += obj_read(DAMAGE_OFF);
    v += obj_read(FORCE_OFF) >> 1;
    v /= 20;
    if (v >= 5)
        v = 5;
    gd_copy(strong_desc_buf, strong_lev_desc + (v << 2), 4);
}

/* ================= 以下:skill.s 成長/清單 UI 段 ================= */

#define LEARN_X0 80
#define LEARN_Y0 (4 + 8)        /* FRAME_Y0+8 */

/* 等效 skill.s setup_attr:六維 = 底值 + 對應基本功等級/10 */
void setup_attr(void)
{
    uint8_t y;

    y = find_kf(BASIC_BARE_KF);
    if (y != 0xFF)
        hero.man_str = hero.attr_str + hero.man_kf[y + 1] / 10;
    y = find_kf(BASIC_DODGE_KF);
    if (y != 0xFF)
        hero.man_dex = hero.attr_dex + hero.man_kf[y + 1] / 10;
    y = find_kf(LITERATE_KF);
    if (y != 0xFF)
        hero.man_int = hero.attr_int + hero.man_kf[y + 1] / 10;
    y = find_kf(BASIC_FORCE_KF);
    if (y != 0xFF)
        hero.man_con = hero.attr_con + hero.man_kf[y + 1] / 10;
    y = find_kf(LOOKS_KF);
    if (y != 0xFF)
        hero.man_per = hero.attr_per + hero.man_kf[y + 1] / 10;
}

/* 等效 skill.s improve_skill:潛能值+gain;達 (等級+1)² → 升級。
 * 回傳 1=升級(原版 sec) */
uint8_t improve_skill(uint16_t gain)
{
    uint8_t y = find_kf(kf_id);
    uint8_t lv;
    uint16_t pot;

    if (y == 0xFF)
        return 0;
    lv = hero.man_kf[y + 1];
    if (lv == 0xFF)
        return 0;                       /* 滿級:潛能與等級都保持不變 */
    pot = (uint16_t)hero.man_kf[y + 2] | ((uint16_t)hero.man_kf[y + 3] << 8);
    pot += gain;
    hero.man_kf[y + 2] = (uint8_t)pot;
    hero.man_kf[y + 3] = (uint8_t)(pot >> 8);

    if (pot < (uint16_t)(lv + 1) * (lv + 1))
        return 0;
    hero.man_kf[y + 1] = lv + 1;
    hero.man_kf[y + 2] = 0;
    hero.man_kf[y + 3] = 0;
    setup_attr();
    return 1;
}

/* 等效 skill.s add_kf:習得新武功(等級/潛能 0)。1=成功 */
uint8_t add_kf(void)
{
    uint8_t y = find_kf(kf_id);

    if (y != 0xFF)
        return 1;
    if (hero.man_kfnum >= MAX_KF)
        return 0;
    y = hero.man_kfnum * 4;
    hero.man_kf[y] = kf_id;
    hero.man_kf[y + 1] = 0;
    hero.man_kf[y + 2] = 0;
    hero.man_kf[y + 3] = 0;
    hero.man_kfnum++;
    return 1;
}

/* 等效 skill.s set_kf:kf_type 類武功 → dmenu_buf;kf_id 命中項 → menu_set */
static void set_kf(void)
{
    uint8_t i, n = 0, id;

    menu_set = 0xFF;
    for (i = 0; i < hero.man_kfnum; i++) {
        id = hero.man_kf[(uint8_t)(i << 2)];
        get_kf_attr(id);
        if (kf_attr[0] != kf_type)
            continue;
        if (id == kf_id)
            menu_set = n;
        dmenu_buf[1 + n] = id;
        n++;
    }
    dmenu_buf[0] = n;
}

/* 等效 skill.s if_parry:啟用中的拳腳/兵刃可兼作招架,追加進清單 */
static void if_parry(void)
{
    uint8_t n = dmenu_buf[0];

    menu_set = 0xFF;
    if (hero.man_usekf[0] & 0x80) {
        if (hero.man_usekf[0] == hero.man_usekf[4])
            menu_set = n;
        dmenu_buf[1 + n] = hero.man_usekf[0] & 0x7F;
        n++;
    }
    if (hero.man_usekf[1] & 0x80) {
        if (hero.man_usekf[1] == hero.man_usekf[4])
            menu_set = n;
        dmenu_buf[1 + n] = hero.man_usekf[1] & 0x7F;
        n++;
    }
    dmenu_buf[0] = n;
}

/* 一行 GB2312/ASCII 混排佔幾個半形格(全形 2 格,半形 1 格),
 * 語義與 show_one_line_to 的推進一致;遇 0/0xFF 行終止。 */
static uint8_t line_cells(const uint8_t *s)
{
    uint8_t n = 0;

    while (*s && *s != 0xFF) {
        if (*s & 0x80) {
            s += 2;
            n += 2;
        } else {
            s++;
            n += 1;
        }
    }
    return n;
}

/* 等效 skill.s skill_desc + show_desc:底部一行「等級描述 等級/潛能」 */
static void skill_show_desc(void)
{
    uint8_t y, lv, cells;
    uint16_t v;
    uint8_t *tpl = gfx_scratch + 192;

    kf_id = dmenu_buf[1 + menu_set];

    y = find_kf(kf_id);
    lv = (y != 0xFF) ? hero.man_kf[y + 1] : 0;
    v = lv / 5;
    if (v > SKILL_LEVEL_SINGLE_MAX)
        v = SKILL_LEVEL_SINGLE_MAX;
    gd_copy(skill_desc_buf,
            skill_level_desc + v * SKILL_LEVEL_DESC_STRIDE,
            SKILL_LEVEL_DESC_STRIDE);

    varbuf[0] = (uint8_t)(uint16_t)skill_desc_buf;
    varbuf[1] = (uint8_t)((uint16_t)skill_desc_buf >> 8);
    varbuf[2] = lv;
    if (y != 0xFF) {
        varbuf[3] = hero.man_kf[y + 2];
        varbuf[4] = hero.man_kf[y + 3];
    } else {
        varbuf[3] = 0;
        varbuf[4] = 0;
    }

    mt_chardesc_rec[0] = 8;                 /* {8, dw varbuf} */
    mt_chardesc_rec[1] = (uint8_t)(uint16_t)varbuf;
    mt_chardesc_rec[2] = (uint8_t)((uint16_t)varbuf >> 8);

    memcpy(tpl, mt_kfdesc_msg, sizeof mt_kfdesc_msg);
    fix_mt_kfdesc_msg(tpl);
    format_string(tpl);

    clear_nline2(FB_ROWS - 14, 14);
    ss_ptr = OutBuf;
    /* 原版 show_desc 是 `ldx #8` 固定左緣(x=48px),描述字數短時整行明顯
     * 偏右;2026-07-29 用戶要求改居中。行寬 26 格,對齊粒度 6px。 */
    cells = line_cells(OutBuf);
    show_one_line((cells < 26) ? (uint8_t)((26 - cells) >> 1) : 0,
                  FB_ROWS - 13);
    fb_flush();
}

/* 等效 skill.s use_skills/enable_it/disable_it */
static uint8_t use_skills(void)
{
    uint8_t u;

    if (kf_id < BASIC_KF_NUM)
        return 0;                           /* 基本功不可啟用 */
    if (find_kf(kf_id) == 0xFF)
        return 0;
    u = hero.man_usekf[kf_type];
    if ((u & 0x80) && (u & 0x7F) == kf_id)
        hero.man_usekf[kf_type] = 0;        /* 再選一次=停用 */
    else
        hero.man_usekf[kf_type] = kf_id | 0x80;
    return 1;                               /* PullMenu:退回分類欄 */
}

static const menu_fn use_h[1] = { use_skills };
static const show_fn desc_s[1] = { skill_show_desc };

/* 原版 right_menu(11011010b/80h/01011001b/CHECK_MENU) */
static const cmenu_t kf_right_menu =
    { 0, 1, 5, 0, MSTYLE_CHECK, 10, MF_USESET, use_h, desc_s, 0, kf_name_tbl };
/* 原版 right_menu1(dw 0 → CR 即退) */
static const cmenu_t kf_right_menu1 =
    { 0, 1, 5, 0, MSTYLE_BOX, 10, 0, 0, desc_s, 0, kf_name_tbl };

static void sk_clear_right(void)
{
    clean_list_right();
    clear_nline2(FB_ROWS - 14, 14);
}

/* 等效 init_skills_menu(kf_type=分類;kf_id=啟用中項供高亮) */
static void init_skills_menu(void)
{
    kf_type = menu_set;
    kf_id = (hero.man_usekf[kf_type] & 0x80)
                ? (hero.man_usekf[kf_type] & 0x7F) : 0x7F;
    set_kf();
    if (kf_type == PARRY_KF)
        if_parry();
}

static void init_skills_menu1(void)
{
    kf_type = menu_set;
    kf_id = 0xFF;
    set_kf();
}

static void show_kf(void)
{
    sk_clear_right();
    init_skills_menu();
    show_menu_txt(list_x0 + 5, list_y0, &kf_right_menu);
    fb_flush();
}

static uint8_t deal_kf(void)
{
    uint8_t save_type;

    init_skills_menu();
    save_type = kf_type;
    pop_menu(list_x0 + 5, list_y0, &kf_right_menu);
    kf_type = save_type;
    return 0;                               /* 回左欄 */
}

static void show_knowledge(void)
{
    sk_clear_right();
    init_skills_menu1();
    show_menu_txt(list_x0 + 5, list_y0, &kf_right_menu1);
    fb_flush();
}

static uint8_t deal_knowledge(void)
{
    init_skills_menu1();
    pop_menu(list_x0 + 5, list_y0, &kf_right_menu1);
    return 0;
}

static const menu_fn left_h[6] = {
    deal_kf, deal_kf, deal_kf, deal_kf, deal_kf, deal_knowledge,
};
static const show_fn left_s[6] = {
    show_kf, show_kf, show_kf, show_kf, show_kf, show_knowledge,
};
/* 原版 left_menu(01000000b/6/01010001b/NORMAL_MENU) */
static const cmenu_t skills_left_menu =
    { 6, 1, 5, 0, MSTYLE_NORMAL, 0, 0, left_h, left_s,
      mt_left_menu_items, 0 };

/* 等效 skill.s show_skills(主選單「技能」) */
uint8_t show_skills(void)
{
    list_menu(48, 4, &skills_left_menu);    /* FRAME_X0, FRAME_Y0 */
    scroll_to_lcd();
    fb_flush();
    return 0;
}

/* 打坐/練功的 effect tick。原版兩張表都是 dw 400 = 400ms/tick。
 * EXT 用真實 GBC 幀時基排 23/24 幀 deadline；等待逐幀取鍵，畫面重畫
 * 與固定 interval 都算在 400ms 內。ADV/BSC 保留原有 4 tick/輪、約
 * 50ms/tick 的 x8 節奏。effect 邏輯、RNG 與停止條件不變。 */
#define PROC_INTERVAL  6
#define PROC_FAST_BATCH 4
#define PROC_EXT_DEN    21945u
#define PROC_EXT_REM    19553u

static uint16_t proc_deadline, proc_phase;

static uint8_t proc_ext_frames(void)
{
    uint8_t frames = 23;

    proc_phase += PROC_EXT_REM;
    if (proc_phase >= PROC_EXT_DEN) {
        proc_phase -= PROC_EXT_DEN;
        frames++;
    }
    return frames;
}

static void proc_begin(void)
{
    menu_input_begin();                 /* 等選單 A 放開並開 VBlank 鎖存 */
    if (!speed_mode) {
        proc_phase = 0;
        proc_deadline = sys_time + proc_ext_frames();
    }
}

static uint8_t proc_wait(void)
{
    uint8_t j;

    if (process_take_cancel()) {
        waitpadup();
        return 1;
    }
    if (speed_mode)
        return 0;
    while ((int16_t)(sys_time - proc_deadline) < 0) {
        vsync();
        heart_beat();
        j = joypad();
        if (process_take_cancel() || (j & (J_A | J_B | J_START))) {
            waitpadup();
            return 1;
        }
    }
    if (process_take_cancel() || (joypad() & (J_A | J_B | J_START))) {
        waitpadup();
        return 1;
    }
    /* 從現在重錨；阻塞訊息返回後最多立即執行一次，不補跑整段時間。 */
    proc_deadline = sys_time + proc_ext_frames();
    return 0;
}

static uint8_t proc_interval(void)
{
    return PROC_INTERVAL;
}

static uint8_t proc_batch(void)
{
    return speed_mode ? PROC_FAST_BATCH : 1;
}

/* ---- 練功(等效 practice_cmd/practice_it,進度 tick=pyh_practice) ---- */
static void pra_set_line(void)
{
    uint8_t y = find_kf(kf_id);

    bcdbuf = (uint16_t)hero.man_kf[y + 1] * hero.man_kf[y + 1];
    binbuf = (uint16_t)hero.man_kf[y + 2]
           | ((uint16_t)hero.man_kf[y + 3] << 8);
}

static void pra_set_digit(void)
{
    uint8_t y = find_kf(kf_id);

    bcdbuf = hero.man_kf[y + 1];
    binbuf = (uint16_t)hero.man_kf[y + 2]
           | ((uint16_t)hero.man_kf[y + 3] << 8);
}

static uint8_t pra_tick(void)               /* HOME 包裝:跨 bank tick */
{
    uint8_t n;

    if (proc_wait())
        return 1;
    n = proc_batch();
    do {
        if (pyh_practice())
            return 1;
    } while (--n);
    return 0;
}

static uint8_t practice_it(void)
{
    kf_id = dmenu_buf[1 + menu_set] & 0x7F;
    proc_begin();
    busy_flag |= 0x80;                      /* 練功中不回復(skill.s) */
    show_process(32, 1, pra_set_line, pra_set_digit, proc_interval(),
                 pra_tick);
    menu_input_end();
    busy_flag &= 0x7F;
    scroll_to_lcd();
    fb_flush();
    return 0;                               /* 回練功選單 */
}

/* 等效 get_pra_kf:啟用中的拳腳/兵刃/輕功 → 清單 */
static void get_pra_kf(void)
{
    uint8_t i, n = 0;

    for (i = 0; i < 3; i++) {
        if (hero.man_usekf[i] & 0x80)
            dmenu_buf[1 + n++] = hero.man_usekf[i];
    }
    dmenu_buf[0] = n;
}

static const menu_fn pra_h[1] = { practice_it };
/* 原版 practice_menu(10011010b/80h/10110001b/RADIO_MENU) */
static const cmenu_t practice_menu =
    { 0, 1, 3, 1, MSTYLE_RADIO, 10, 0, pra_h, 0, 0, kf_name_tbl };

uint8_t practice_cmd(void)
{
    get_pra_kf();
    pop_menu(LEARN_X0, LEARN_Y0, &practice_menu);
    scroll_to_lcd();
    fb_flush();
    return 0;
}

/* ---- 打坐(等效 skill.s dazuo_cmd:條=內力/2×上限) ---- */
static void dz_set_line(void)
{
    binbuf = hero.man_fp;
    bcdbuf = hero.man_maxfp << 1;
}

static void dz_set_digit(void)
{
    binbuf = hero.man_fp;
    bcdbuf = hero.man_maxfp;
}

static uint8_t dz_tick(void)                /* HOME 包裝:跨 bank tick */
{
    uint8_t n;

    if (proc_wait())
        return 1;
    n = proc_batch();
    do {
        if (dazuo())
            return 1;
    } while (--n);
    return 0;
}

uint8_t dazuo_cmd(void)
{
    proc_begin();
    busy_flag |= 0x80;                      /* 打坐中不回復(skill.s) */
    show_process(32, 1, dz_set_line, dz_set_digit, proc_interval(),
                 dz_tick);
    menu_input_end();
    busy_flag &= 0x7F;
    scroll_to_lcd();
    fb_flush();
    return 0;
}
