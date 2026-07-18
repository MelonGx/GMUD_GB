/* fight.c - 回合制戰鬥引擎(bank 27),逐例程移植原版:
 *   fight.s      主迴圈/攻防管線/傷害/勝負/write_fight
 *   string2.s    get_result_msg / get_py_action(訊息組合)
 *   digit.s      show_line / show_digit(血氣條 + 微數字)
 *   skilly.s+string.s  random_kf_action(KF_TEXT 動作抽取)
 *   math.s       point_adjust / perform_random_it(→ ui.c random_it32)
 *
 * man_state = hero.man_name 起 41B;npc_state = &npc(text.h),兩者同構
 * fight_off(fight.h)。obj = 攻方,you = 守方,以基址指針多態存取。
 *
 * 尚未接入(明確標註):call_out/init_ptemp/recover(Task#3 ptemp/吸氣)、
 * 絕招 show_perform+perform(Task#3)、物品 show_goods(item 模組)、
 * 連線 net_flag(單機恆 0)。
 */
#pragma bank 27
#include <gb/gb.h>
#include <string.h>
#include "fight.h"
#include "fight_msgs.h"
#include "save.h"
#include "text.h"
#include "skill.h"
#include "gamedata.h"
#include "menu.h"
#include "ui.h"
#include "game.h"
#include "blit.h"
#include "fb.h"
#include "res.h"
#include "player_res.h"
#include "player_girl_res.h"
#include "tiles_res.h"
#include "textbank_res.h"
#include "serve.h"          /* recover(吸氣,bank26 已移植 lee1.s 版) */
#include "goods.h"          /* show_goods(戰鬥「物品」,HOME) */
#include "fight_internal.h" /* 跨 bank 介面(perform.c 絕招) */
#include "nf.h"             /* 聯機對戰(bank 30):net_send_data/net_pkt */

#define XI_KF        41
#define MENGHU_KF    40
#define BOSS_PAI     8
#define KILLER_NPC   125

/* 螢幕座標(原版 fight.s equ)。
 * 全屏 160x144 後,戰鬥區整體下移 FIGHT_TOP,不再貼上緣;內部相對佈局
 * (圖 32px→血條→氣條)不變,只加同一位移。 */
#define FIGHT_TOP    28
#define MAN_X0       10
#define MAN_Y0       FIGHT_TOP
#define NPC_X0       107
#define NPC_Y0       FIGHT_TOP
#define MAN_HP_X0    16
#define MAN_HP_Y0    (FIGHT_TOP + 35)
#define MAN_FP_X0    MAN_HP_X0
#define MAN_FP_Y0    (MAN_HP_Y0 + 9)
#define NPC_HP_X0    110
#define NPC_HP_Y0    (FIGHT_TOP + 35)
#define NPC_FP_X0    NPC_HP_X0
#define NPC_FP_Y0    (NPC_HP_Y0 + 9)
/* 選單位置。用戶要直排放下方狀態列,但 fb 僅 0-79(遊戲)與 112-143
 * (狀態列 32px)上屏,80-111 為不刷新盲帶;直排 5 項需 60px 放不進
 * 32px 狀態列。暫置狀態列橫排(y=118,出戰鬥畫面、可見),待用戶決定
 * 最終方案(見交付說明)。 */
#define MAIN_MENU_X0 6
#define MAIN_MENU_Y0 114

/* 戰鬥主角姿勢。原版 get_player_img(0)=幀 3(way1=朝左);用戶要求全屏
 * 版改朝右面對敵人 → way2*3+status0 = 幀 6(frame=role_way*3+status)。 */
#define FIGHT_PLAYER_FRAME 6

/* fight 暫存(戰鬥期間 gfx_scratch 空閒,分時複用) */
#define imgbuf  (gfx_scratch)           /* 對戰圖 192B */
#define msgbuf  (gfx_scratch + 192)     /* 動作/結果訊息組合 96B */

/* ---- combat 全域(原版 gmud.h fight 段同名) ---- */
static uint8_t  escape_factor;
static uint16_t damage_point;
static uint32_t attack_point, dodge_point, parry_point;
static uint8_t  g_kf_ap, g_kf_dp, g_kf_pp, damage_type;
static uint16_t kf_damage, kf_force;
static uint8_t  man_kf_dp, man_kf_pp, npc_kf_dp, npc_kf_pp;
static uint16_t wound_flag;              /* a9:0=無傷 非0=effhp 扣減量 */
uint8_t  combat_over;                    /* perform.c 共用(fight_internal.h) */
static const uint8_t *escape_msg;

uint8_t fight_exit_code;
uint8_t perform_flag;                    /* 絕招連擊固定動作序號(random_kf_action) */

/* ---- 基址/偏移存取(等效 obj_ptr/you_ptr + (ptr),y) ---- */
static uint8_t *man_state(void) { return (uint8_t *)hero.man_name; }
static uint8_t *npc_state(void) { return (uint8_t *)&npc; }

static uint16_t rd16(const uint8_t *b, uint8_t off)
{
    return (uint16_t)b[off] | ((uint16_t)b[off + 1] << 8);
}
static uint32_t rd32(const uint8_t *b, uint8_t off)
{
    return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8)
         | ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24);
}
static void wr16(uint8_t *b, uint8_t off, uint16_t v)
{
    b[off] = (uint8_t)v;
    b[off + 1] = (uint8_t)(v >> 8);
}

/* ---- textbank 直讀(KF_TEXT 記錄) ---- */
static uint8_t tb8(uint16_t off)
{
    uint8_t v;
    res_read(&textbank_res, off, &v, 1);
    return v;
}
static uint16_t tb16(uint16_t off) { return res_read16(&textbank_res, off); }

/* NPC 對戰圖走 map.c 的 HOME tile_fetch(game.h)。
 * !!嚴禁在本檔自帶 SWITCH_ROM 副本:fight.c 在切換 bank 27,
 * 切走 0x4000 窗=切走自身代碼,立即死機(2026-07-09 教訓)。 */

/* 顯示 bank 27 端訊息:先在本 bank 端格式化(format_string 是 HOME
 * 代碼、此刻 bank 27 仍映射,讀資料安全),再叫 bank 25 顯示 OutBuf。
 * 把 bank 27 指針直接傳給 bank 25 的 show_text 會解讀到垃圾。
 *
 * 連線(fight.s:1298):先把「未格式化模板」廣播給對方(對方以其
 * 本地綁定格式化 → 訊息鏡像)。模板可能指本 bank ROM,必須趁 bank 27
 * 映射時拷入 SRAM 封包區,再跨 bank 呼叫 net_send_data(bank 30)。 */
void show_fight_msg(const uint8_t *msg)
{
    if (net_flag & 0x80) {
        memcpy(NET_PKT_MSG, msg, 100);
        if (net_send_data())
            return;                     /* 傳輸失敗:nf 已顯錯+置結束旗 */
    }
    format_string(msg);
    show_text_out();
}

static uint8_t blitbuf[34];     /* 點陣 WRAM 中轉(icon/exp_ball → bank25 lee_block) */

/* ================= random_kf_action(string.s is_kf_data) ================= */
/* i: kf_id, obj_flag(bit7 man);o: damage_type、g_kf_ap/dp/pp、kf_damage、
 * kf_force,動作訊息 → msgbuf(string_ptr)。基本功=stride3、輕功=stride2、
 * 招式(kf>=BASIC_KF_NUM 非輕功)=stride12 依等級隨機。 */
static void read_action_text(uint16_t desc_off)
{
    uint8_t i;

    res_read(&textbank_res, desc_off, msgbuf, 94);
    msgbuf[94] = 0;
    /* 雙 0 收尾(=原版 get_text_rts 補第二個 0)。固定長拷貝會帶進
     * textbank 下一條字串,單 0 後 format_string 不停 → OutBuf 溢寫
     * 相鄰 npc 結構(2026-07-09 教訓:0xDCFF scratch288 | 0xDE28 npc) */
    for (i = 0; i < 94 && msgbuf[i]; i++)
        ;
    msgbuf[i + 1] = 0;
}

static void random_kf_action(uint8_t is_man)
{
    uint16_t rec = text_get_ptr(TC_KF, kf_id);   /* 記錄址(textbank) */
    uint8_t num = tb8(rec);
    uint8_t r;
    uint16_t desc;

    get_kf_attr(kf_id);
    if (kf_attr[0] == DODGE_KF) {                /* is_dodge_msg:stride 2 */
        r = (uint8_t)random_it(num);
        desc = tb16(rec + 2 + (uint16_t)r * 2);
        read_action_text(desc);
        return;
    }
    if (kf_id < BASIC_KF_NUM) {                  /* is_basic_msg:stride 3 */
        uint16_t e;
        r = (uint8_t)random_it(num);
        e = rec + 2 + (uint16_t)r * 3;
        damage_type = tb8(e);
        kf_damage = 0;
        kf_force = 0;
        g_kf_ap = g_kf_dp = g_kf_pp = 0;         /* 基本功無 ap/dp/pp 加成 */
        desc = tb16(e + 1);
        read_action_text(desc);
        return;
    }
    /* is_action_kf:招式 stride 12。絕招連擊(perform_flag bit7)取固定
     * 動作序號(string.s is_pf_data:實測 bank_data_ptr 重導後仍讀本 kf
     * 動作表,故等效「用 perform_flag&0x7f 當序號、不隨機、不判等級」);
     * 否則 new_random 依等級隨機。 */
    {
        uint16_t e;

        if (perform_flag & 0x80) {
            r = (uint8_t)(perform_flag & 0x7F);
            if (r >= num)
                r = (uint8_t)(num ? num - 1 : 0);
            perform_flag &= 0x7F;                /* 原版 is_pf_data 清 bit7 */
        } else {
            uint8_t y = is_man ? find_kf(kf_id) : find_npc_kf(kf_id);
            uint8_t level = 0;
            uint8_t x = 0;

            if (y != 0xFF)
                level = is_man ? hero.man_kf[y + 1] : npc_kf[y + 1];
            /* new_random:x = 等級門檻 <= level 的動作數(上限 num) */
            for (;;) {
                if (level < tb8(rec + 2 + (uint16_t)x * 12))
                    break;
                if (x >= num)
                    break;
                x++;
            }
            r = (uint8_t)random_it(x ? x : 1);
        }
        e = rec + 2 + (uint16_t)r * 12;
        damage_type = tb8(e + 1);
        g_kf_ap = tb8(e + 3);
        g_kf_dp = tb8(e + 4);
        g_kf_pp = tb8(e + 5);
        kf_damage = tb16(e + 6);
        kf_force = tb16(e + 8);
        desc = tb16(e + 10);
        read_action_text(desc);
    }
}

/* ---- get_kf_action:選攻擊招式 → random_kf_action;damage_point=kf_damage ---- */
static void kf_attack(uint8_t id, uint8_t is_man)
{
    kf_id = id & 0x7F;
    random_kf_action(is_man);
    damage_point = kf_damage;
}

static void get_kf_action(void)                  /* man 攻擊 */
{
    if (hero.man_weapon & 0x80) {                /* weapon_attack */
        if (hero.man_usekf[1] & 0x80) {
            if (check_skill(hero.man_usekf[1] & 0x7F)) {
                kf_attack(hero.man_usekf[1], 1);
                return;
            }
        }
        kf_attack(get_basic_kf(WEAPON_KF), 1);   /* weapon_nokf */
        return;
    }
    if (hero.man_usekf[0] & 0x80)                /* kf_attack(啟用拳腳) */
        kf_attack(hero.man_usekf[0], 1);
    else
        kf_attack(BASIC_BARE_KF, 1);             /* bare_nokf */
}

static void get_kf_action2(void)                 /* npc 攻擊 */
{
    if (npc.npc_goods[0] & 0x80) {               /* weapon_attack2(不 check) */
        if (npc.npc_usekf[1] & 0x80) {
            kf_attack(npc.npc_usekf[1], 0);
            return;
        }
        /* weapon_nokf:原版以 A=npc_weapon 進 get_weapon_kf(skill.s
         * weapon_attack2 一路不動 A),故 npc 側必須帶 bit7 取 npc 武器。
         * 漏 bit7 會讀 man_weapon:主角空手=0 → 0 號物品類型查表 →
         * 垃圾 kf → textbank 亂讀(捕快戰「、、」bug,2026-07-14 修) */
        kf_attack(get_basic_kf(WEAPON_KF | 0x80), 0);
        return;
    }
    if (npc.npc_usekf[0] & 0x80)
        kf_attack(npc.npc_usekf[0], 0);
    else
        kf_attack(BASIC_BARE_KF, 0);
}

static void get_dg_action(uint8_t is_man)        /* 閃避招式 */
{
    uint8_t u = is_man ? hero.man_usekf[2] : npc.npc_usekf[2];

    kf_id = (u & 0x80) ? (u & 0x7F) : BASIC_DODGE_KF;
    random_kf_action(is_man);
}

/* get_py_action:招架訊息(守方兵器決定;等效 string2.s) */
static const uint8_t *get_py_action(uint8_t weapon)
{
    if (weapon & 0x80)                           /* have_weapon */
        return ft_parry_msg_tbl[random_it(4)];
    return ft_unarmed_parry_tbl[random_it(2)];   /* bare_parry */
}

/* ================= 結果訊息(string2.s get_result_msg) ================= */
static const uint8_t *get_damage_msg(uint8_t type, uint16_t dp)
{
    uint8_t idx;

    if (dp == 0)
        return ft_damage0_msg;
    if (type >= 6)
        type = 0;
    if (dp >= 255)
        idx = 7;
    else {
        idx = 0;
        while ((uint8_t)dp >= ft_damage_msg_data[idx])
            idx++;
    }
    return ft_damage_msg_tbl[type][idx];
}

static const uint8_t *get_status_msg(uint8_t ratio, uint16_t wound)
{
    uint8_t idx = 0;

    if (ratio == 0)
        ratio = 1;
    while (ratio <= ft_status_msg_data[idx])
        idx++;
    return wound ? ft_eff_status_tbl[idx] : ft_status_msg_tbl[idx];
}

/* 組合:result_tip + damage_msg + "($n" + status_msg + ")" → msgbuf */
static void get_result_msg(uint8_t ratio, uint16_t wound,
                           uint8_t type, uint16_t dp)
{
    const uint8_t *dmg = get_damage_msg(type, dp);
    const uint8_t *st = get_status_msg(ratio, wound);
    uint8_t *p = msgbuf;

    memcpy(p, ft_result_tip, 4);
    p += 4;
    while (*dmg)
        *p++ = *dmg++;
    *p++ = '(';
    *p++ = '$';
    *p++ = 'n';
    while (*st)
        *p++ = *st++;
    *p++ = ')';
    *p++ = 0;
    *p = 0;
}

/* ================= 招式威力(fight.s skill_power/kf_power) ================= */
static uint32_t kf_power(uint8_t *ob, uint8_t defense, uint8_t micro)
{
    uint16_t lvl = skill_level;
    uint32_t exp = rd32(ob, EXP_OFF);

    lvl += ob[defense ? DEFENSE_OFF : ATTACK_OFF];
    if (micro & 0x80) {                          /* 負向微調 */
        uint8_t m = (uint8_t)(~micro + 1);
        lvl = (lvl > m) ? (uint16_t)(lvl - m) : 0;
    } else {
        lvl += micro;
    }
    if (lvl == 0)
        return exp / 200;
    return (uint32_t)lvl * lvl * lvl / 300 + exp / 100;
}

uint32_t skill_power(uint8_t is_man, uint8_t defense, uint8_t micro)
{
    if (is_man)
        query_skill();
    else
        query_npc_skill();
    return kf_power(is_man ? man_state() : npc_state(), defense, micro);
}

/* point_adjust:依屬性微調威力(fight.s) */
static uint32_t point_adjust(uint32_t power, uint8_t stat)
{
    uint16_t factor;

    if (stat >= 30) {
        uint8_t d = stat - 30;
        factor = 100 + (d >> 1) + (d & 1);
    } else {
        factor = 100 - (uint8_t)((30 - stat) >> 1);
    }
    return power * factor / 100;
}

/* 命中判定:random(AP+DP or AP+PP) < 守值 → 未命中(cal_random_ApDp/ApPp) */
static uint8_t hit_roll(uint32_t atk, uint32_t def)
{
    return random_it32(atk + def) >= def;        /* 1=命中 0=被閃/架 */
}

/* ================= 傷害(fight.s cal_damage) ================= */
static uint16_t cal_damage_bonus(uint8_t *ob, uint8_t *yo, uint16_t a8,
                                 uint16_t a6, uint16_t a7)
{
    if (a6 != 0) {
        uint16_t a1;
        if (ob[WEAPON_OFF] & 0x80)
            a6 /= 6;
        a1 = (a7 >= 3000) ? 3000 : a7;
        a1 /= 20;
        a6 += a1;
        a1 = rd16(yo, FORCE_OFF) / 25;
        if (a6 < a1)
            return a8;                           /* you_little_force */
        a6 -= a1;
    }
    a8 += a6;
    if (kf_force)
        a8 = (uint16_t)(a8 + (uint32_t)kf_force * a8 / 100);
    return a8;
}

static void cal_damage(uint8_t *ob, uint8_t *yo)
{
    uint16_t a8, a9, a6, a7;

    /* cal_basic_damage:a9 = (attack + random(attack))/2 [+kf_damage%] */
    a9 = ob[DAMAGE_OFF];
    a9 = (uint16_t)((a9 + random_it(a9)) >> 1);
    if (kf_damage)
        a9 = (uint16_t)(a9 + (uint32_t)kf_damage * a9 / 100);

    a8 = ob[STR_OFF];                            /* damage_bonus base = 膂力 */
    a6 = rd16(ob, FORCE_OFF);                    /* factor(內力) */
    a7 = rd16(ob, FP_OFF);                       /* fp */
    if (a6 >= a7)
        a6 = a7;
    wr16(ob, FP_OFF, (uint16_t)(a7 - a6));       /* 消耗內力 */
    a8 = cal_damage_bonus(ob, yo, a8, a6, a7);
    a8 = (uint16_t)((a8 + random_it(a8)) >> 1);
    damage_point = (uint16_t)(a8 + a9);
    kf_damage = 0;
    kf_force = 0;

    /* 判傷(effhp 扣減):random(damage) > armor 且(有兵器 或 1/4 機率) */
    wound_flag = 0;
    {
        uint8_t armor = yo[ARMOR_OFF];
        if ((uint8_t)random_it(damage_point) > armor) {
            if ((ob[WEAPON_OFF] & 0x80) || random_it(4) == 0)
                wound_flag = (uint16_t)(damage_point - armor);
        }
    }
}

/* 分身(fenshen):kar==0xFF 且 random(100) < per → 完全閃過 */
static uint8_t fenshen(uint8_t is_man)
{
    uint8_t *s = is_man ? man_state() : npc_state();

    if (s[KAR_OFF] != 0xFF)
        return 1;                                /* 未使用分身 → 正常 */
    if (random_it(100) >= s[PER_OFF])
        return 1;
    show_fight_msg(ft_fenshen_dodge_msg);             /* 分身閃過,攻擊落空 */
    return 0;
}

/* ================= 顯示(digit.s show_line/show_digit + write_fight) === */
static const uint8_t bar_tail[8] =
    { 0, 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE };

static void draw_hbar(uint8_t xbyte, uint8_t y0, uint8_t width_px, uint8_t h)
{
    uint8_t full = width_px >> 3;
    uint8_t rem = width_px & 7;
    uint8_t row, i;

    for (row = 0; row < h; row++) {
        uint16_t base = (uint16_t)(y0 + row) * 20 + xbyte;
        for (i = 0; i < full; i++)
            scroll_buf[base + i] = 0xFF;
        if (rem)
            scroll_buf[base + full] = bar_tail[rem];
    }
}

static void show_line(uint8_t x0px, uint8_t y0, uint16_t val,
                      uint16_t maxv, uint8_t glen)
{
    uint8_t xbyte = x0px >> 3;
    uint16_t vw;

    if (val == 0)
        return;                                  /* is_zero:不畫 */
    if (val >= maxv)
        vw = glen;
    else {
        vw = (uint16_t)((uint32_t)val * glen / maxv);
        if (vw == 0)
            vw = 1;
    }
    draw_hbar(xbyte, y0, (uint8_t)vw, 3);        /* 值條 3px */
    draw_hbar(xbyte, (uint8_t)(y0 + 4), glen, 1);/* 上限條 1px(原版 y0+4) */
}

/* 4×5 微數字(digit.s digit_tbl,低半字節=像素) */
static const uint8_t digit_pat[12][5] = {
    { 0x0E, 0x0A, 0x0A, 0x0A, 0x0E },   /* 0 */
    { 0x04, 0x0C, 0x04, 0x04, 0x04 },   /* 1 */
    { 0x0E, 0x02, 0x0E, 0x08, 0x0E },   /* 2 */
    { 0x0E, 0x02, 0x0E, 0x02, 0x0E },   /* 3 */
    { 0x0A, 0x0A, 0x0E, 0x02, 0x02 },   /* 4 */
    { 0x0E, 0x08, 0x0E, 0x02, 0x0E },   /* 5 */
    { 0x0E, 0x08, 0x0E, 0x0A, 0x0E },   /* 6 */
    { 0x0E, 0x02, 0x02, 0x02, 0x02 },   /* 7 */
    { 0x0E, 0x0A, 0x0E, 0x0A, 0x0E },   /* 8 */
    { 0x0E, 0x0A, 0x0E, 0x02, 0x0E },   /* 9 */
    { 0x00, 0x02, 0x04, 0x08, 0x00 },   /* 10 分隔線 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 11 空 */
};

static uint8_t patbuf[12];

static uint8_t put_num(uint16_t v, uint8_t pi)
{
    uint8_t d[5], n = 0;

    if (v == 0) {
        patbuf[pi++] = 0;
        return pi;
    }
    while (v) {
        d[n++] = (uint8_t)(v % 10);
        v /= 10;
    }
    while (n)
        patbuf[pi++] = d[--n];
    return pi;
}

static void show_digit(uint8_t x0px, uint8_t y0, uint16_t val, uint16_t maxv)
{
    uint8_t x0 = x0px >> 3;
    uint8_t pi = 0, i;

    pi = put_num(val, pi);
    if (maxv != 0) {
        patbuf[pi++] = 10;
        pi = put_num(maxv, pi);
    }
    if (pi & 1)
        patbuf[pi++] = 11;                       /* 補齊偶數(2 位/字節) */
    for (i = 0; i < pi; i += 2) {
        uint8_t col = x0 + (i >> 1);
        const uint8_t *pa = digit_pat[patbuf[i]];
        const uint8_t *pb = digit_pat[patbuf[i + 1]];
        uint8_t r;
        for (r = 0; r < 5; r++)
            scroll_buf[(uint16_t)(y0 + r) * 20 + col] =
                (uint8_t)(pa[r] << 4) | pb[r];
    }
}

static void show_effect(uint8_t x, uint8_t y)    /* 命中火花(exp_ball) */
{
    memcpy(blitbuf, ft_img_exp_ball + 2, 32);    /* bank27→WRAM 再給 bank25 */
    lee_block(x, y, blitbuf, 2, 16, LCMD_OR);
    scroll_to_lcd();
    fb_flush();
}

static void write_fight(void)
{
    uint16_t item_id = G_id_item;
    uint16_t a3;

    /* 清遊戲區(0..79)後全量重畫 */
    memset(scroll_buf, 0, 20 * SCROLL_H);

    /* 主角(幀 3,取上 32 列;女角用用戶素材幀集) */
    res_read((hero.man_gender == 1) ? &player_girl_res : &player_res,
             (uint16_t)FIGHT_PLAYER_FRAME * 192, imgbuf, 192);
    lee_block(MAN_X0, MAN_Y0, imgbuf, 4, 32, LCMD_PRINT);

    /* 主角 血 圖示 + 條 + 數字 */
    memcpy(blitbuf, ft_img_hp + 2, 16);          /* bank27→WRAM 再給 bank25 */
    lee_block(MAN_HP_X0 - 12, MAN_HP_Y0 - 2, blitbuf, 2, 8, LCMD_PRINT);
    a3 = hero.man_maxhp
       ? (uint16_t)((uint32_t)39 * hero.man_effhp / hero.man_maxhp) : 39;
    show_line(MAN_HP_X0, MAN_HP_Y0, hero.man_hp, hero.man_effhp, (uint8_t)a3);
    show_digit(MAN_HP_X0 + 40, MAN_HP_Y0, hero.man_hp, hero.man_effhp);

    /* 主角 氣 圖示 + 條 + 數字 */
    memcpy(blitbuf, ft_img_fp + 2, 16);
    lee_block(MAN_FP_X0 - 12, MAN_FP_Y0 - 2, blitbuf, 2, 8, LCMD_PRINT);
    show_line(MAN_FP_X0, MAN_FP_Y0, hero.man_fp, hero.man_maxfp, 39);
    show_digit(MAN_FP_X0 + 40, MAN_FP_Y0, hero.man_fp, hero.man_maxfp);

    /* NPC 圖 + 血/氣條(無圖示/數字) */
    if (located_id == KILLER_NPC)
        item_id = 474;
    tile_fetch(item_id, imgbuf);
    lee_block(NPC_X0, NPC_Y0, imgbuf, 4, 32, LCMD_PRINT);
    show_line(NPC_HP_X0, NPC_HP_Y0, npc.npc_hp, npc.npc_maxhp, 39);
    show_line(NPC_FP_X0, NPC_FP_Y0, npc.npc_fp, npc.npc_maxfp, 39);
}

void refresh_fight(void)
{
    write_fight();
    scroll_to_lcd();
    fb_flush();
}

/* ================= 攻防管線(fight.s attack_npc/attack_man) ================ */
static void not_attack_busy(uint8_t is_man)
{
    if (is_man) {
        hero.man_busy = hero.man_busy ? hero.man_busy - 1 : 0;
    } else {
        npc.npc_busy = npc.npc_busy ? npc.npc_busy - 1 : 0;
    }
    net_repeat = 1;                     /* fight.s:924(單機無副作用) */
    show_fight_msg(ft_not_move_msg0);                 /* "$N正在...木然!" */
}

uint8_t who_win(void);

/* 主角攻擊 NPC */
void attack_npc(void)
{
    uint32_t ap, dp, pp;
    uint8_t ratio;
    uint8_t y;

    obj_flag = 0x80;
    if (hero.man_busy) {
        not_attack_busy(1);
        return;
    }
    limb_flag = (uint8_t)random_it(16);

    /* ---- 攻擊力 AP ---- */
    get_kf_action();                             /* 設 damage_type、kf、動作訊息 */
    show_fight_msg(msgbuf);
    man_kf_dp = g_kf_dp;
    man_kf_pp = g_kf_pp;
    kf_type = (hero.man_weapon & 0x80) ? WEAPON_KF : HAND_KF;
    ap = skill_power(1, 0, g_kf_ap);
    ap = point_adjust(ap, hero.man_dex);
    attack_point = ap;

    /* ---- 閃避 DP(npc)---- */
    get_dg_action(0);                            /* msgbuf = npc 閃避訊息 */
    kf_type = DODGE_KF;
    dp = skill_power(0, 1, npc_kf_dp);
    npc_kf_dp = 0;
    dp = point_adjust(dp, npc.npc_int);
    if (npc.npc_busy)
        dp /= 3;
    dodge_point = dp;
    if (!hit_roll(attack_point, dodge_point)) {  /* 被閃過 */
        show_fight_msg(msgbuf);
        if (who_win())
            combat_over = 1;
        return;
    }

    /* ---- 招架 PP(npc)---- */
    escape_msg = get_py_action(npc.npc_goods[0]);
    kf_type = PARRY_KF;
    pp = skill_power(0, 1, npc_kf_pp);
    npc_kf_pp = 0;
    pp = point_adjust(pp, npc.npc_str);
    if (npc.npc_busy)
        pp /= 3;
    parry_point = pp;
    if (!hit_roll(attack_point, parry_point)) {  /* 被招架 */
        show_fight_msg(escape_msg);
        if (who_win())
            combat_over = 1;
        return;
    }

    if (!fenshen(0))                             /* npc 分身閃過 */
        return;

    show_effect(NPC_X0 + 8, NPC_Y0 + 4);

    /* ---- 傷害 ---- */
    cal_damage(man_state(), npc_state());
    npc.npc_hp = (npc.npc_hp > damage_point)
               ? (uint16_t)(npc.npc_hp - damage_point) : 0;
    npc.npc_effhp = (npc.npc_effhp > wound_flag)
                  ? (uint16_t)(npc.npc_effhp - wound_flag) : 0;

    ratio = percent(wound_flag ? npc.npc_effhp : npc.npc_hp, npc.npc_maxhp);
    get_result_msg(ratio, wound_flag, damage_type, damage_point);

    /* XI_KF 吸血:等級×傷害/100 → 主角回血(上限 effhp) */
    y = find_kf(XI_KF);
    if (y != 0xFF) {
        uint16_t heal = (uint16_t)((uint32_t)hero.man_kf[y + 1]
                                   * damage_point / 100);
        hero.man_hp += heal;
        if (hero.man_hp > hero.man_effhp)
            hero.man_hp = hero.man_effhp;
    }
    show_fight_msg(msgbuf);
    if (who_win())
        combat_over = 1;
}

/* NPC 攻擊主角 */
void attack_man(void)
{
    uint32_t ap, dp, pp;
    uint8_t ratio;
    uint8_t y;

    obj_flag = 0x00;
    if (npc.npc_busy) {
        not_attack_busy(0);
        return;
    }
    limb_flag = (uint8_t)random_it(16);

    get_kf_action2();
    show_fight_msg(msgbuf);
    npc_kf_dp = g_kf_dp;
    npc_kf_pp = g_kf_pp;
    kf_type = (npc.npc_goods[0] & 0x80) ? WEAPON_KF : HAND_KF;
    ap = skill_power(0, 0, g_kf_ap);
    ap = point_adjust(ap, npc.npc_dex);
    attack_point = ap;

    get_dg_action(1);
    kf_type = DODGE_KF;
    dp = skill_power(1, 1, man_kf_dp);
    man_kf_dp = 0;
    dp = point_adjust(dp, hero.man_con);
    if (hero.man_busy)
        dp /= 3;
    dodge_point = dp;
    if (!hit_roll(attack_point, dodge_point)) {  /* 被閃過 */
        show_fight_msg(msgbuf);
        if (who_win())
            combat_over = 1;
        return;
    }

    escape_msg = get_py_action(hero.man_weapon);
    kf_type = PARRY_KF;
    pp = skill_power(1, 1, man_kf_pp);
    man_kf_pp = 0;
    pp = point_adjust(pp, hero.man_str);
    if (hero.man_busy)
        pp /= 3;
    parry_point = pp;
    if (!hit_roll(attack_point, parry_point)) {  /* 被招架 */
        show_fight_msg(escape_msg);
        if (who_win())
            combat_over = 1;
        return;
    }

    if (!fenshen(1))
        return;

    show_effect(MAN_X0 + 8, MAN_Y0 + 4);

    cal_damage(npc_state(), man_state());
    hero.man_hp = (hero.man_hp > damage_point)
                ? (uint16_t)(hero.man_hp - damage_point) : 0;
    hero.man_effhp = (hero.man_effhp > wound_flag)
                   ? (uint16_t)(hero.man_effhp - wound_flag) : 0;

    ratio = percent(wound_flag ? hero.man_effhp : hero.man_hp, hero.man_maxhp);
    get_result_msg(ratio, wound_flag, damage_type, damage_point);

    y = find_npc_kf(XI_KF);
    if (y != 0xFF) {
        uint16_t heal = (uint16_t)((uint32_t)npc_kf[y + 1]
                                   * damage_point / 100);
        npc.npc_hp += heal;
        if (npc.npc_hp > npc.npc_effhp)
            npc.npc_hp = npc.npc_effhp;
    }
    show_fight_msg(msgbuf);
    if (who_win())
        combat_over = 1;
}

/* ================= 勝負(fight.s who_win) ================= */
static void wait_cr(void)
{
    while (wait_key() != K_CR)
        ;
}

static void killer(void)
{
    if (located_id == KILLER_NPC) {
        if (hero.guan_kill != 0xFF)
            hero.guan_kill++;
    } else {
        hero.npc_kill++;
    }
}

/* 殺敵後道德調整(fight.s daode_1/kill_bad,逐位元運算) */
static void adjust_daode(void)
{
    uint8_t nd = npc.npc_daode;
    uint8_t md = hero.man_daode;

    if (nd == 0)
        return;
    if (!(nd & 0x80)) {                          /* npc_daode > 0 */
        uint16_t a = (md >= nd) ? (uint16_t)(md - nd) : 0;
        if (md & 0x80) {                         /* man_daode < 0 */
            uint8_t a8 = (uint8_t)a;
            a8 = (a8 >= nd) ? (uint8_t)(a8 - nd) : 0;
            a = a8;
        }
        hero.man_daode = (uint8_t)a;
    } else {                                     /* kill_bad:npc_daode < 0 */
        if (md & 0x80) {                         /* man_daode < 0 */
            uint16_t sum = (uint16_t)(uint8_t)(0 - nd) + md;
            if (sum > 0xFF)
                sum = 0xFF;
            hero.man_daode = (uint8_t)sum;
        }
    }
}

uint8_t who_win(void)
{
    if (hero.man_hp == 0) {                       /* npc_win:主角敗 */
        uint8_t nd = npc.npc_daode, md = hero.man_daode;
        /* 連線(fight.s:317):強制走 no_kill 放過路徑,不死亡 */
        if (!(net_flag & 0x80)
            && ((nd & 0x80) || nd == 0 || !(md & 0x80))) {
            format_string(ft_npc_msg2);          /* "$m,去死吧!" */
            show_talk_msg_no_wait();
            fight_exit_code = 0;                 /* 死亡 */
        } else {
            format_string(ft_npc_msg);           /* "承让了."(放過)*/
            show_talk_msg_no_wait();
            fight_exit_code = 2;
        }
        wait_cr();
        return 1;
    }
    if (npc.npc_hp == 0) {                        /* man_win:NPC 敗 */
        if (net_flag & 0x80) {                    /* 連線(fight.s:335):
                                                   * 無殺/放選擇,直接結束 */
            format_string(ft_man_msg7);
            show_talk_msg_no_wait();
            fight_exit_code = 2;
            wait_cr();
            return 1;
        }
        if (npc.npc_pai == BOSS_PAI) {
            fight_exit_code = 3;
            return 1;
        }
        format_string(ft_man_msg);               /* "$m这一刀...(Y/N)?" */
        show_talk_msg_no_wait();
        for (;;) {
            uint8_t k = wait_key();
            if (k == K_CR)
                break;                           /* A=劈(殺) */
            if (k == K_ESC) {                    /* B=不劈(放) */
                format_string(ft_man_msg7);
                show_talk_msg_no_wait();
                fight_exit_code = 1;
                wait_cr();
                return 1;
            }
        }
        /* kill_npc */
        killer();
        {
            uint8_t r = (uint8_t)random_it(5);
            uint8_t idx = (r >= 2) ? r : npc.npc_gender;
            format_string(ft_dead_talk[idx]);
            show_talk_msg_no_wait();
        }
        adjust_daode();
        fight_exit_code = 3;
        wait_cr();
        return 1;
    }
    return 0;                                     /* 雙方存活 */
}

/* 等效 fight.s final_fight:BOSS 開場白 →(邪帝可拒戰)→ 對局。
 * 原版鍵 'b'=拒(exit 2)/'n'=打 → GBC B/A。回 fight_exit_code。 */
uint8_t final_fight(uint8_t ex) BANKED
{
    format_string(ft_boss_tbl[ex]);
    show_talk_msg_no_wait();
    if (ex == 2) {
        for (;;) {
            uint8_t k = wait_key();
            if (k == K_ESC) {
                fight_exit_code = 2;
                return 2;
            }
            if (k == K_CR)
                break;
        }
    } else {
        wait_cr();
    }
    busy_flag |= 0x80;
    fight();
    busy_flag &= 0x7F;
    return fight_exit_code;
}

/* if_escape:逃跑判定(fight.s) */
static uint8_t if_escape(void)
{
    escape_msg = ft_not_move_msg1;               /* 動彈不得 */
    if (hero.man_busy) {
        escape_factor += 10;
        return 0;
    }
    escape_msg = ft_not_move_msg2;               /* 被一把抓住 */
    if ((uint8_t)random_it((uint16_t)hero.man_dex + escape_factor)
            <= npc.npc_dex) {
        escape_factor += 10;
        return 0;
    }
    return 1;                                     /* 逃脫成功 */
}

/* ================= 選單處理器實作(HOME thunk 呼叫) =================
 * call_out/init_ptemp/perform_dispatch 見 perform.c(同 bank 27 直呼) */
uint8_t fight_normal_attack(void) BANKED
{
    combat_over = 0;
    call_out();
    net_repeat = 2;                     /* fight.s:165(攻擊=2 條訊息) */
    attack_npc();
    if (combat_over)
        return 2;
    if (net_flag & 0x80)
        return 2;                       /* 連線:交棒退選單(fight.s:174),
                                         * 對方反擊由對方回合進行 */
    refresh_fight();
    attack_man();
    if (combat_over)
        return 2;
    refresh_fight();
    return 0;
}

/* ---- 供 perform.c(bank 26)跨 bank 呼叫的薄封裝(fight_internal.h)---- */
void pf_attack_npc(void) BANKED { attack_npc(); }
void pf_attack_man(void) BANKED { attack_man(); }
uint32_t pf_skill_power(uint8_t is_man, uint8_t defense, uint8_t micro) BANKED
{
    return skill_power(is_man, defense, micro);
}
void pf_show_fight_msg(const uint8_t *msg) BANKED { show_fight_msg(msg); }

/* ---- 供 nf.c(bank 30,聯機)跨 bank 呼叫的薄封裝 ---- */
uint8_t pf_who_win(void) BANKED { return who_win(); }
void pf_refresh_fight(void) BANKED { refresh_fight(); }
static void init_fight(void);
void pf_init_fight(void) BANKED { init_fight(); }
void pf_fight_menu(void) BANKED
{
    pop_menu(MAIN_MENU_X0, MAIN_MENU_Y0, &fight_menu);
}

/* 等效 fight.s attack_action:show_perform → perform → NPC 反擊 → call_out。
 * show_perform 在 bank 25(與 pop_menu 同 bank);perform_dispatch 在
 * bank 27(本 bank 直呼)。回傳 2=對局結束(全退) 0=重開主選單。 */
uint8_t fight_perform_action(void) BANKED
{
    uint8_t pid;

    combat_over = 0;
    obj_flag = 0x80;                             /* smb7 obj_flag(原版在 show_perform 前) */
    pid = show_perform();                        /* bank 25:絕招清單 */
    if (pid == 0xFF) {                           /* ESC 取消 */
        scroll_to_lcd();
        fb_flush();
        return 0;
    }
    perform_id = pid;
    if (perform_dispatch()) {                    /* 失敗(carry set)→ 重畫重選 */
        if (net_flag & 0x80)
            net_msg_vision &= 0x7F;              /* fight.s:211 net_perform_rts */
        refresh_fight();
        return 0;
    }
    /* 施展中的攻擊已含 who_win(原版死亡即 PullMenu 非局部退出,
     * 等效 combat_over 傳播,不會二次對白) */
    if (combat_over)
        return 2;
    if (net_flag & 0x80) {                       /* fight.s:201 net_perform:
                                                  * 補發「絕招完」包後交棒 */
        net_perform_flag |= 0x80;
        memcpy(NET_PKT_MSG, msgbuf, 100);        /* 原版 string_ptr 殘值,
                                                  * 對方不顯示(perform 旗) */
        net_send_data();
        if (who_win())
            combat_over = 1;
        return 2;
    }
    if (who_win())                               /* 原版 to_perform 成功後顯式
                                                  * 判定:直接傷害類(飛擲/震/
                                                  * 神倒)不經 attack_xxx */
        return 2;
    refresh_fight();
    attack_man();                                /* NPC 反擊一擊 */
    call_out();                                  /* 回合末:遞減 buff 到期還原 */
    if (combat_over)
        return 2;
    refresh_fight();
    return 0;
}

uint8_t fight_xiqi(void) BANKED
{
    recover();                                   /* 吸氣:內力換精血(serve.c) */
    refresh_fight();
    return 0;
}

/* 等效 fight.s goods_action:show_goods(與主選單同一物品清單,HOME)
 * → refresh_fight 重畫戰鬥畫面。戰鬥中吃藥/裝卸皆原版行為。 */
uint8_t fight_goods(void) BANKED
{
    show_goods();
    refresh_fight();
    return 0;
}

uint8_t fight_escape(void) BANKED
{
    combat_over = 0;
    if (net_flag & 0x80) {                       /* fight.s:230:先告知對方
                                                  * 退出(quit 旗包) */
        net_quit_flag |= 0x80;
        memcpy(NET_PKT_MSG, msgbuf, 100);        /* 殘值;對方見 quit 即退 */
        if (net_send_data())
            return 2;                            /* 傳輸失敗:已置結束旗 */
    }
    if (npc.npc_pai == BOSS_PAI)
        return 0;                                /* 不可逃 BOSS */
    obj_flag = 0x80;
    if (if_escape()) {                           /* 逃脫成功 */
        fight_exit_code = 2;
        if (net_flag & 0x80) {                   /* 原版 fail_quit 淨路徑:
                                                  * 無「哪里逃」對白 */
            nf_over = 1;
            return 2;
        }
        format_string(ft_npc_msg3);              /* "哼,哪里逃!" */
        show_talk_msg_no_wait();
        wait_cr();
        return 2;
    }
    show_fight_msg(escape_msg);                        /* 逃跑失敗訊息 */
    attack_man();                                 /* NPC 追打一擊 */
    if (combat_over)
        return 2;
    refresh_fight();
    return 0;
}

/* ================= 對局主體(fight.s fight/init_fight) ================= */
static void init_fight(void)
{
    escape_factor = 20;
    hero.man_busy = 0;
    npc.npc_busy = 0;
    init_ptemp();
}

void fight(void) BANKED
{
    busy_flag |= 0x80;                           /* 戰鬥中不回復(原版) */
    init_fight();

    fb_clear();
    refresh_fight();
    format_string(ft_vs);                        /* "ＶＳ" 置中 */
    ss_ptr = OutBuf;
    show_one_line(13, 8);
    fb_flush();
    {
        uint8_t t;
        for (t = 0; t < 45; t++)
            vsync();
    }

    /* 首回合隨機:半數機率 NPC 先攻 */
    if (random_it(2) == 0) {
        refresh_fight();
        combat_over = 0;
        attack_man();
        if (combat_over) {
            busy_flag &= 0x7F;
            return;
        }
    }

    for (;;) {
        uint8_t r;
        refresh_fight();
        r = pop_menu(MAIN_MENU_X0, MAIN_MENU_Y0, &fight_menu);
        if (r == 0xFE)                           /* 全退:對局結束 */
            break;
        /* 0xFF(ESC)不退出戰鬥,重開選單 */
    }
    clear_nline2(80, FB_ROWS - 80);              /* 清完整擴展帶殘留 */
    fb_flush();
    busy_flag &= 0x7F;
}
