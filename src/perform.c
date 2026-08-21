/* perform.c - 絕招系統(bank 27),逐招移植原版:
 *   hello.s      perform 分派器(perform_id → 24 招)
 *   perform.s    24 招 + ptemp 引擎(judge_kf/judge_force/set_ptemp/
 *                find_ptemp/init_ptemp/call_out/refresh_data/show_over_msg)
 *
 * 訊息(PERFORM_TEXT=class 7)已隨 textbank 組譯(data/perform.txt),
 * 直接 text_get_ptr(TC_PERFORM, idx) 取,format_string 出 → show_fight_msg。
 *
 * obj=施展方 you=承受方,以 obj_flag bit7 多態(單機恆主角施展 obj=man)。
 * ptemp/npc_ptemp 在原版位於 game_buf(存檔區外,斷電即失);此處合為
 * 一段 160B(ptemp=前120 npc_ptemp=後40),與原版 call_out 連續掃描一致。
 *
 * 攻擊(attack_xxx)、招式威力(skill_power)、勝負(who_win)、訊息顯示
 * (show_fight_msg)直呼同 bank 的 fight.c(fight_internal.h,裸調無跳板)。
 *
 * !! 原版 call_out 依「當下 obj_flag」還原+報訊,而到期幾乎總落在敵方
 *    反擊後(obj_flag=0)→ 到期訊息 $N 錯報敵名、主角 buff 還原寫進敵側
 *    (主角實質永 buff、敵屬性被舊值蓋)。用戶定版 2026-07-18:
 *    訊息主語一律改按 buff 所屬側;屬性還原側 EXT/ADV 修正、BSC 保留原版。
 *
 * bank 27(fight)已近滿,絕招置 bank 26;對 fight.c 的攻防/威力/訊息
 * 呼叫經 fight_internal.h 的 pf_* BANKED 跳板。
 */
#pragma bank 27
#include <gb/gb.h>
#include <string.h>
#include "save.h"
#include "text.h"
#include "skill.h"
#include "goods.h"
#include "gamedata.h"
#include "ui.h"
#include "fight.h"
#include "fight_internal.h"
#include "res.h"
#include "textbank_res.h"

/* ---- fight_off(對照 fight.h)---- */
#define ATTACK_OFF  14
#define DEFENSE_OFF 15
#define DAMAGE_OFF  16
#define ARMOR_OFF   17
#define EXP_OFF     18
#define FORCE_OFF   22
#define STR_OFF     24
#define DEX_OFF     25
#define INT_OFF     26
#define CON_OFF     27
#define PER_OFF     28
#define KAR_OFF     29
#define HP_OFF      30
#define MAXHP_OFF   32
#define FP_OFF      34
#define MAXFP_OFF   36
#define EFFHP_OFF   38
#define WEAPON_OFF  40
#define BUSY_OFF     9

/* ---- kf_type(usekf 槽序,h/id.h)---- */
#define HAND_KF    0
#define WEAPON_KF  1
#define DODGE_KF   2
#define FORCE_KF   3
#define PARRY_KF   4

/* ---- kf_id(h/id.h)---- */
#define BAGUAD_KF   11
#define BAGUAZ_KF   12
#define BAZHEN_KF   13
#define HUNYUAN_KF  14
#define HUATUAN_KF  17
#define LIU_KF      18
#define MEIHUA_KF   19
#define SANHUA_KF   20
#define PIFENG_KF   23
#define TONGJI_KF   25
#define RENSHU_KF   26
#define YIDAO_KF    29
#define TAIJIJ_KF   30
#define TAIJIQ_KF   31
#define TAIJIG_KF   32
#define XUESHANG_KF 36
#define XUESHANJ_KF 38
#define XUEYING_KF  39

/* ---- perform_id(h/id.h pf_id)---- */
#define DAOYING1_PF   0
#define DAOYING2_PF   1
#define ZHANGDAO1_PF  2
#define ZHANGDAO2_PF  3
#define LUOYING_PF    4
#define LIULANG_PF    5
#define SANHUA_PF     6
#define FEIZHI_PF     7
#define HONGLIAN_PF   8
#define LEIDONG_PF    9
#define FENSHEN_PF   10
#define YIANMU_PF    11
#define LIANZHAN_PF  12
#define YIDAOZHAN_PF 13
#define CHAN_PF      14
#define LIAN_PF      15
#define SANHUAN_PF   16     /* taoyue */
#define JI_PF        17
#define LUANHUAN_PF  18
#define YINYANG_PF   19
#define ZHEN_PF      20
#define BINGXIN_PF   21
#define LIUCHU_PF    22
#define QINNA_PF     23     /* shengui */
#define YAKYU_PF     24     /* 野球拳(GBC original, BSC only) */
#define MENGHU_KF    40

/* ---- PERFORM_TEXT 訊息序號(h/perform_id.h)---- */
#define PT_ZHANGDAO_SUCESS  0
#define PT_ZHANGDAO_OVER    1
#define PT_DAOYING_SUCESS   2
#define PT_SANHUA_SUCESS    3
#define PT_SANHUA_OVER      4
#define PT_LIULANG_SUCESS   5
#define PT_LUOYING_SUCESS   6
#define PT_LUOYING_SHOOT    7
#define PT_LUOYING_DODGE    8
#define PT_LUOYING_SHOOT1   9
#define PT_LUOYING_DODGE1  10
#define PT_FEIZHI_SUCESS   11
#define PT_FEIZHI_SHOOT    12
#define PT_FEIZHI_DODGE    13
#define PT_HONGLIAN_SUCESS 14
#define PT_LEIDONG_SUCESS  15
#define PT_FENSHEN_SUCESS  16
#define PT_FENSHEN_OVER    17
#define PT_YIANMU_SUCESS   18
#define PT_YIANMU_OVER     19
#define PT_LIANZHAN_SUCESS 20
#define PT_YIDAO_SUCESS    21
#define PT_CHAN_SUCESS     22
#define PT_CHAN_SHOOT      23
#define PT_CHAN_DODGE      24
#define PT_LIAN_SUCESS     25
#define PT_LIAN_OVER       26
#define PT_TAOYUE_SUCESS   27
#define PT_JI_SUCESS       28
#define PT_JI_SHOOT        29
#define PT_JI_SHOOT1       30
#define PT_JI_DODGE        31
#define PT_LUANHUAN_SUCESS 32
#define PT_LUANHUAN_SHOOT  33
#define PT_LUANHUAN_DODGE  34
#define PT_YINYANG_SUCESS  35
#define PT_YINYANG_ATTACK  36
#define PT_YINYANG_SHOOT   37
#define PT_YINYANG_DODGE   38
#define PT_ZHEN_SUCESS     39
#define PT_ZHEN_DODGE      40
#define PT_ZHEN_SHOOT      41
#define PT_ZHEN_SHOOT1     42
#define PT_ZHEN_DODGE1     43
#define PT_BINGXIN_SUCESS  44
#define PT_BINGXIN_OVER    45
#define PT_LIUCHU_SUCESS   46
#define PT_SHENGUI_SUCESS  47
#define PT_SHENGUI_SHOOT   48
#define PT_SHENGUI_DODGE   49
#define PT_YIANMU_FAIL     50
#define PT_LEVEL           64
#define PT_SUIT            65
#define PT_NEILI           66
#define PT_NOT_NEILI       67
#define PT_USING           68
#define PT_BUSY            69
#define PT_MAN_BUSY        70
#define PT_NPC_BUSY        71
#define PT_NOT_STR         72

/* pf_attr_tbl(skill.dat):perform_id → 所需 kf_id(show_perform 用) */
/* (在 fight_home.c 也需要,故此處僅供 perform 內部一致性參照,不外露) */

/* ================= ptemp 引擎狀態 ================= */
#define PTEMP_SIZE     120
#define NPC_PTEMP_SIZE  40
/* ptemp(0)+npc_ptemp(120)=160B,置 SRAM(main 常開;WRAM 讓給棧,
 * 2026-07-10)。原版在 game_buf 共享區;每場 init_ptemp 重置。 */
__at(0xA3A0) static uint8_t pf_area[PTEMP_SIZE + NPC_PTEMP_SIZE];

/* 施展參數(原版 game_buf+56 起) */
static uint16_t neili_cost;
static uint8_t  set_kf;
static uint16_t set_level;
static uint8_t  temp_data, temp_delay, temp_position, temp_data_2, temp_position_2;

/* ================= 基址/取存(obj=施展方 you=承受方) ================= */
static uint8_t *pf_obj(void)
{
    return (obj_flag & 0x80) ? (uint8_t *)hero.man_name : (uint8_t *)&npc;
}
static uint8_t *pf_you(void)
{
    return (obj_flag & 0x80) ? (uint8_t *)&npc : (uint8_t *)hero.man_name;
}
static uint16_t o16(uint8_t off) { uint8_t *b = pf_obj(); return b[off] | ((uint16_t)b[off + 1] << 8); }
static uint32_t o32(uint8_t off)
{
    uint8_t *b = pf_obj();
    return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8)
         | ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24);
}
static uint8_t  o8(uint8_t off)  { return pf_obj()[off]; }
static void ow16(uint8_t off, uint16_t v) { uint8_t *b = pf_obj(); b[off] = (uint8_t)v; b[off + 1] = (uint8_t)(v >> 8); }
static uint16_t y16(uint8_t off) { uint8_t *b = pf_you(); return b[off] | ((uint16_t)b[off + 1] << 8); }
static uint32_t y32(uint8_t off)
{
    uint8_t *b = pf_you();
    return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8)
         | ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24);
}
static uint8_t  y8(uint8_t off)  { return pf_you()[off]; }
static void yw16(uint8_t off, uint16_t v) { uint8_t *b = pf_you(); b[off] = (uint8_t)v; b[off + 1] = (uint8_t)(v >> 8); }

static uint8_t get_obj_busy(void) { return pf_obj()[BUSY_OFF]; }
static uint8_t get_you_busy(void) { return pf_you()[BUSY_OFF]; }
static void set_obj_busy(uint8_t v) { pf_obj()[BUSY_OFF] = v; }
static void set_you_busy(uint8_t v) { pf_you()[BUSY_OFF] = v; }

/* adc_obj_1byte:8 位飽和加,寫回 obj[off] */
static void adc_obj_1byte(uint8_t a, uint8_t off)
{
    uint8_t *b = pf_obj();
    uint16_t s = (uint16_t)b[off] + a;
    b[off] = (s > 0xFF) ? 0xFF : (uint8_t)s;
}

/* sub_obj_fp:扣 neili_cost(下限 0) */
static void sub_obj_fp(void)
{
    uint16_t fp = o16(FP_OFF);
    ow16(FP_OFF, (fp >= neili_cost) ? (uint16_t)(fp - neili_cost) : 0);
}
/* sub_you_hp/sub_you_fp:扣 amt(下限 0);回 1=未借位(you 值≥amt) */
static uint8_t sub_you_hp(uint16_t amt)
{
    uint16_t hp = y16(HP_OFF);
    uint8_t nb = (hp >= amt);
    yw16(HP_OFF, nb ? (uint16_t)(hp - amt) : 0);
    return nb;
}
static uint8_t sub_you_fp(uint16_t amt)
{
    uint16_t fp = y16(FP_OFF);
    uint8_t nb = (fp >= amt);
    yw16(FP_OFF, nb ? (uint16_t)(fp - amt) : 0);
    return nb;
}

/* get_usekf(A):obj/you 依 obj_flag(此處僅 obj 側 usekf 用) */
static uint8_t get_usekf(uint8_t which)
{
    return (obj_flag & 0x80) ? hero.man_usekf[which] : npc.npc_usekf[which];
}

/* ================= 訊息(PERFORM_TEXT class 7,已在 textbank) ================= */
#define pf_msg (gfx_scratch + 288)      /* 戰鬥期間 gfx_scratch 288+ 空閒 */

static void show_pf_text(uint8_t idx)
{
    uint16_t rec = text_get_ptr(TC_PERFORM, idx);
    uint8_t i;

    if (net_flag & 0x80)
        net_repeat = 2;                 /* perform.s:2359 show_text 淨戰計數 */
    res_read(&textbank_res, rec, pf_msg, 200);
    pf_msg[198] = 0;
    pf_msg[199] = 0;                    /* 保底雙 0(訊息本身已雙 0 收尾) */
    for (i = 0; i < 198 && pf_msg[i]; i++)
        ;
    pf_show_fight_msg(pf_msg);          /* → fight.c(bank 27)跳板 */
}

/* 絕招失敗訊息(等效 perform.s:383 fail_rts):置 net_msg_vision,
 * 失敗訊息只在本機顯示(回合未交棒,對方收包後不上畫面)。回 1。 */
static uint8_t pf_fail(uint8_t idx)
{
    net_msg_vision |= 0x80;
    show_pf_text(idx);
    return 1;
}

/* ================= judge_kf / judge_force ================= */
/* 回 1=失敗(已顯示訊息,原版 carry set),0=通過 */
static uint8_t judge_kf(uint8_t type)
{
    kf_type = type;
    kf_id = get_usekf(type) & 0x7F;
    if (kf_id != set_kf) {              /* not_suit_kf */
        kf_id = set_kf;                 /* suit_msg 的 $k 顯示所需武功 */
        return pf_fail(PT_SUIT);
    }
    if (obj_flag & 0x80)
        query_skill();                  /* → skill_level(用 kf_type) */
    else
        query_npc_skill();
    if (skill_level < set_level)        /* not_level */
        return pf_fail(PT_LEVEL);
    return 0;
}

static uint8_t judge_force(void)
{
    if (o16(MAXFP_OFF) < neili_cost)    /* neili_level:內力上限不足 */
        return pf_fail(PT_NEILI);
    if (o16(FP_OFF) < neili_cost)       /* not_neili:當前內力不夠 */
        return pf_fail(PT_NOT_NEILI);
    return 0;
}

/* ================= ptemp:find/set/find_pspace/init/call_out ================= */
static uint8_t ptemp_off;               /* find_ptemp 命中偏移 */

/* find_ptemp:obj 側 ptemp 找 perform_id;回 1=命中(ptemp_off) 0=無 */
static uint8_t find_ptemp(uint8_t pid)
{
    uint8_t base = (obj_flag & 0x80) ? 0 : PTEMP_SIZE;
    uint8_t lim = (obj_flag & 0x80) ? PTEMP_SIZE : NPC_PTEMP_SIZE;
    uint8_t y;

    for (y = 0; y < lim; y += 6) {
        if (pf_area[base + y] == pid) {
            ptemp_off = (uint8_t)(base + y);
            return 1;
        }
    }
    return 0;
}

/* find_pspace:obj 側找空槽([0]==0xFF);回 1=有(*out=偏移) 0=滿 */
static uint8_t find_pspace(uint8_t *out)
{
    uint8_t base = (obj_flag & 0x80) ? 0 : PTEMP_SIZE;
    uint8_t lim = (obj_flag & 0x80) ? PTEMP_SIZE : NPC_PTEMP_SIZE;
    uint8_t y;

    for (y = 0; y < lim; y += 6) {
        if (pf_area[base + y] == 0xFF) {
            *out = (uint8_t)(base + y);
            return 1;
        }
    }
    return 0;
}

/* set_ptemp:寫入 {perform_id,temp_data,temp_position,temp_data_2,
 * temp_position_2,temp_delay};回 1=成功 0=無空槽 */
static uint8_t set_ptemp(uint8_t pid)
{
    uint8_t o;

    if (!find_pspace(&o))
        return 0;
    pf_area[o + 0] = pid;
    pf_area[o + 1] = temp_data;
    pf_area[o + 2] = temp_position;
    pf_area[o + 3] = temp_data_2;
    pf_area[o + 4] = temp_position_2;
    pf_area[o + 5] = temp_delay;
    return 1;
}

/* clear_xxx_ptemp:找 obj 側 perform_id 槽,清 position/position_2(=不還原) */
static void clear_xxx_ptemp(uint8_t pid)
{
    if (find_ptemp(pid)) {
        pf_area[ptemp_off + 2] = 0;
        pf_area[ptemp_off + 4] = 0;
    }
}

void init_ptemp(void) BANKED
{
    uint8_t y;
    for (y = 0; y < PTEMP_SIZE + NPC_PTEMP_SIZE; y += 6) {
        pf_area[y + 0] = 0xFF;
        pf_area[y + 1] = 0;
        pf_area[y + 2] = 0;
        pf_area[y + 3] = 0;
        pf_area[y + 4] = 0;
        pf_area[y + 5] = 0;
    }
}

/* refresh_data:把 val 還原到「當下 obj_flag」側的 position 欄位。
 * position:0 none 1 attack 2 defense 3 damage 4 armor 5 str 6 dex
 * 7 int 8 con 9 per 10 kar 11 對手 attack(is_npc_attack) */
static void refresh_data(uint8_t val, uint8_t pos)
{
    uint8_t *ob = pf_obj();

    switch (pos) {
    case 1:  ob[ATTACK_OFF]  = val; break;
    case 2:  ob[DEFENSE_OFF] = val; break;
    case 3:  ob[DAMAGE_OFF]  = val; break;
    case 4:  ob[ARMOR_OFF]   = val; break;
    case 5:  ob[STR_OFF]     = val; break;
    case 6:  ob[DEX_OFF]     = val; break;
    case 7:  ob[INT_OFF]     = val; break;
    case 8:  ob[CON_OFF]     = val; break;
    case 9:  ob[PER_OFF]     = val; break;
    case 10: ob[KAR_OFF]     = val; break;
    case 11: pf_you()[ATTACK_OFF] = val; break;
    default: break;
    }
}

#define FENSHEN_TAG0 0x46
#define FENSHEN_TAG1 0x53

/* reserve_buf[1..2] 標記 BSC 影分身殘留。雙字節避免舊存檔的保留區
 * 偶然被誤認；man_kar==0xFF 另兼容標記加入前已產生的舊殘留。 */
static uint8_t fenshen_feature_marked(void)
{
    return hero.reserve_buf[1] == FENSHEN_TAG0
        && hero.reserve_buf[2] == FENSHEN_TAG1;
}

static void fenshen_feature_mark(void)
{
    if (speed_mode == 2) {
        hero.reserve_buf[1] = FENSHEN_TAG0;
        hero.reserve_buf[2] = FENSHEN_TAG1;
    }
}

static void fenshen_feature_unmark(void)
{
    hero.reserve_buf[1] = 0;
    hero.reserve_buf[2] = 0;
}

/* 回到非影分身狀態的合法值。福緣沒有後天加成；容貌要包含目前
 * 駐顏術等級，不能直接退回先天值。 */
static void restore_fenshen_base(void)
{
    uint8_t y = find_kf(LOOKS_KF);

    hero.man_per = hero.attr_per;
    if (y != 0xFF)
        hero.man_per += hero.man_kf[y + 1] / 10;
    hero.man_kar = hero.attr_kar;
}

void fenshen_disable_bsc_feature(void) BANKED
{
    if (!fenshen_feature_marked() && hero.man_kar != 0xFF)
        return;
    restore_fenshen_base();
    fenshen_feature_unmark();
}

/* 對局在 delay 歸零前結束：EXT/ADV 從 ptemp 精確還原；BSC 刻意
 * 留下影分身效果。無論哪檔都清本場槽，下一場不得再拿舊倒數還原。 */
void fenshen_fight_end(void) BANKED
{
    uint8_t saved = obj_flag;
    uint8_t found, y;

    obj_flag = 0x80;
    found = find_ptemp(FENSHEN_PF);
    if (found) {
        if (speed_mode != 2) {
            hero.man_per = pf_area[ptemp_off + 1];
            hero.man_kar = pf_area[ptemp_off + 3];
        }
        pf_area[ptemp_off] = 0xFF;
        for (y = 1; y < 6; y++)
            pf_area[ptemp_off + y] = 0;
    }
    if (speed_mode != 2) {
        if (!found && (fenshen_feature_marked() || hero.man_kar == 0xFF))
            restore_fenshen_base();
        fenshen_feature_unmark();
    }
    obj_flag = saved;
}

/* show_over_msg:buff 到期提示(僅部份招式有) */
static void show_over_msg(uint8_t pid)
{
    if (pid == ZHANGDAO1_PF || pid == ZHANGDAO2_PF)
        show_pf_text(PT_ZHANGDAO_OVER);
    else if (pid == SANHUA_PF)
        show_pf_text(PT_SANHUA_OVER);
    else if (pid == FENSHEN_PF)
        show_pf_text(PT_FENSHEN_OVER);
    else if (pid == YIANMU_PF)
        show_pf_text(PT_YIANMU_OVER);
    else if (pid == LIAN_PF)
        show_pf_text(PT_LIAN_OVER);
    else if (pid == BINGXIN_PF)
        show_pf_text(PT_BINGXIN_OVER);
}

/* call_out:掃 ptemp+npc_ptemp,delay 遞減;歸零 → 還原兩欄 + over 訊息 + 清槽 */
void call_out(void) BANKED
{
    uint8_t o;

    for (o = 0; o < PTEMP_SIZE + NPC_PTEMP_SIZE; o += 6) {
        uint8_t d = pf_area[o + 5];
        uint8_t saved, owner;
        if (d == 0)
            continue;
        d--;
        pf_area[o + 5] = d;
        if (d != 0)
            continue;
        /* call_out_serve:還原 → over 訊息 → 清槽(側別見檔頭 !!) */
        saved = obj_flag;
        owner = (o < PTEMP_SIZE) ? 0x80 : 0x00;
        if (speed_mode != 2)
            obj_flag = owner;           /* EXT/ADV:還原到 buff 所屬側 */
        refresh_data(pf_area[o + 1], pf_area[o + 2]);
        refresh_data(pf_area[o + 3], pf_area[o + 4]);
        if (speed_mode != 2 && o < PTEMP_SIZE
                && pf_area[o] == FENSHEN_PF)
            fenshen_feature_unmark();
        obj_flag = owner;               /* 全模式:$N=buff 所屬側 */
        show_over_msg(pf_area[o + 0]);
        obj_flag = saved;
        pf_area[o + 0] = 0xFF;
        pf_area[o + 1] = 0;
        pf_area[o + 2] = 0;
        pf_area[o + 3] = 0;
        pf_area[o + 4] = 0;
        pf_area[o + 5] = 0;
    }
}

/* ================= 兵器裝卸(daoying/daoying_zhen/liulang 空手一擊用) ==== */
static uint8_t saved_damage;            /* perform_temp4+1:暫存 man_damage */

static void unwield_weapon(void)
{
    if (obj_flag & 0x80) {
        uint8_t x;
        saved_damage = hero.man_damage;
        hero.man_damage = 0;
        x = find_goods(hero.man_weapon);
        hero.man_goods[x] ^= 0x80;
        hero.man_weapon &= 0x7F;
    } else {
        npc.npc_goods[0] &= 0x7F;       /* npc_weapon 清 enable */
    }
}
static void wield_weapon(void)
{
    if (obj_flag & 0x80) {
        uint8_t x = find_goods(hero.man_weapon);
        hero.man_goods[x] ^= 0x80;
        hero.man_damage = saved_damage;
        hero.man_weapon |= 0x80;
    } else {
        npc.npc_goods[0] |= 0x80;
    }
}

/* attack_xxx:依 obj_flag 由施展方攻擊承受方(經 bank 27 跳板)。
 * 淨戰計數 3(perform.s 17 處 lm net_repeat,#3 全部緊貼 attack_xxx,
 * 收斂於此;單機無副作用)。 */
static void attack_xxx(void)
{
    /* 原版 who_win 會非局部退出；C 版需明確中止後續連擊。
     * 只跳過攻擊，仍讓絕招本體跑完武器/臨時數值的收尾。 */
    if (combat_over)
        return;

    net_repeat = 3;
    if (obj_flag & 0x80)
        pf_attack_npc();
    else
        pf_attack_man();
}

static uint8_t remove_wield_bonus(uint8_t value, uint8_t raw)
{
    int16_t v = (int16_t)value - (int8_t)raw;
    return (v < 0) ? 0 : (uint8_t)v;
}

/* NPC 平常只在 init_npc 加入武器傷害；聯機 NPC 是對方玩家的鏡像，
 * 其攻防也已包含武器修正，因此需額外回退。 */
static void lose_npc_weapon(void)
{
    uint8_t weapon = npc.npc_goods[0];

    if (!(weapon & 0x80))
        return;

    get_goods_attr(weapon & 0x7F);
    npc.npc_damage = (npc.npc_damage >= goods_attr[2])
                         ? npc.npc_damage - goods_attr[2] : 0;
    if (net_flag & 0x80) {
        npc.npc_attack = remove_wield_bonus(npc.npc_attack, goods_attr[3]);
        npc.npc_defense = remove_wield_bonus(npc.npc_defense, goods_attr[4]);
    }
    npc.npc_goods[0] = 0;
}

static void lose_obj_weapon(void)
{
    if (obj_flag & 0x80)
        lose_wielded_weapon();
    else
        lose_npc_weapon();
}

static void lose_you_weapon(void)
{
    if (obj_flag & 0x80)
        lose_npc_weapon();
    else
        lose_wielded_weapon();
}

/* skill_xxx_power:依 obj_flag 決定 man/npc 側威力(經跳板) */
static uint32_t skill_xxx_power(uint8_t defense, uint8_t micro)
{
    return pf_skill_power((obj_flag & 0x80) ? 1 : 0, defense, micro);
}

/* ============================================================
 *                     24 招(perform.s)
 * 每招回 1=失敗(carry set) 0=成功(clc)
 * ============================================================ */

/* 八卦門:化掌为刀(掌)ZHANGDAO1 */
static uint8_t zhangdao_gua(void)
{
    uint16_t lv;

    set_kf = HUNYUAN_KF; set_level = 105;
    if (judge_kf(FORCE_KF)) return 1;
    set_kf = BAGUAZ_KF; set_level = 105;
    if (judge_kf(HAND_KF)) return 1;
    neili_cost = 200;
    if (judge_force()) return 1;

    if (find_ptemp(ZHANGDAO2_PF)) { return pf_fail(PT_USING); }
    if (find_ptemp(ZHANGDAO1_PF)) { return pf_fail(PT_USING); }

    sub_obj_fp();
    show_pf_text(PT_ZHANGDAO_SUCESS);

    lv = skill_level;
    temp_delay = (uint8_t)(lv / 25);
    temp_data = (uint8_t)o16(ATTACK_OFF);   /* 存原攻擊 */
    temp_position = 1;
    temp_position_2 = 0;
    set_ptemp(ZHANGDAO1_PF);

    adc_obj_1byte((uint8_t)(lv / 15), ATTACK_OFF);
    return 0;
}

/* 八卦門:化掌为刀(阵)ZHANGDAO2 */
static uint8_t zhangdao_zhen(void)
{
    uint16_t lv;
    uint8_t inc;

    set_kf = HUNYUAN_KF; set_level = 120;
    if (judge_kf(FORCE_KF)) return 1;
    set_kf = BAZHEN_KF; set_level = 120;
    if (judge_kf(HAND_KF)) return 1;
    neili_cost = 400;
    if (judge_force()) return 1;

    if (find_ptemp(ZHANGDAO1_PF)) { return pf_fail(PT_USING); }
    if (find_ptemp(ZHANGDAO2_PF)) { return pf_fail(PT_USING); }

    sub_obj_fp();
    show_pf_text(PT_ZHANGDAO_SUCESS);

    lv = skill_level;
    temp_delay = (uint8_t)(lv / 20);
    temp_data = (uint8_t)o16(STR_OFF);
    temp_position = 5;
    temp_data_2 = (uint8_t)o16(ATTACK_OFF);
    temp_position_2 = 1;
    set_ptemp(ZHANGDAO2_PF);

    inc = (uint8_t)(lv / 15);
    adc_obj_1byte(inc, ATTACK_OFF);
    adc_obj_1byte((uint8_t)(inc << 1), STR_OFF);    /* asl a1 = ×2 */

    set_obj_busy(2);
    return 0;
}

/* 八卦門:刀影掌(掌)DAOYING1 */
static uint8_t daoying_gua(void)
{
    if (get_obj_busy()) { return pf_fail(PT_MAN_BUSY); }

    set_kf = HUNYUAN_KF; set_level = 90;
    if (judge_kf(FORCE_KF)) return 1;
    set_kf = BAGUAD_KF; set_level = 135;
    if (judge_kf(WEAPON_KF)) return 1;
    set_kf = BAGUAZ_KF; set_level = 30;
    if (judge_kf(HAND_KF)) return 1;
    neili_cost = 150;
    if (judge_force()) return 1;

    if (find_ptemp(ZHANGDAO1_PF)) { return pf_fail(PT_USING); }
    if (find_ptemp(ZHANGDAO2_PF)) { return pf_fail(PT_USING); }
    if (find_ptemp(DAOYING2_PF))  { return pf_fail(PT_USING); }
    if (find_ptemp(DAOYING1_PF))  { return pf_fail(PT_BUSY); }

    sub_obj_fp();
    show_pf_text(PT_DAOYING_SUCESS);

    temp_position = 0;
    temp_position_2 = 0;
    temp_delay = 7;
    set_ptemp(DAOYING1_PF);

    unwield_weapon();
    attack_xxx();
    wield_weapon();
    attack_xxx();

    set_obj_busy(3);
    return 0;
}

/* 八卦門:刀影掌(阵)DAOYING2 */
static uint8_t daoying_zhen(void)
{
    uint8_t at, st;

    set_kf = HUNYUAN_KF; set_level = 90;
    if (judge_kf(FORCE_KF)) return 1;
    set_kf = BAGUAD_KF; set_level = 90;
    if (judge_kf(WEAPON_KF)) return 1;
    set_kf = BAZHEN_KF; set_level = 45;
    if (judge_kf(HAND_KF)) return 1;
    neili_cost = 450;
    if (judge_force()) return 1;

    if (find_ptemp(ZHANGDAO1_PF)) { return pf_fail(PT_USING); }
    if (find_ptemp(ZHANGDAO2_PF)) { return pf_fail(PT_USING); }
    if (find_ptemp(DAOYING1_PF))  { return pf_fail(PT_USING); }
    if (find_ptemp(DAOYING2_PF))  { return pf_fail(PT_BUSY); }

    sub_obj_fp();
    show_pf_text(PT_DAOYING_SUCESS);

    at = (uint8_t)o16(ATTACK_OFF);
    st = (uint8_t)o16(STR_OFF);

    temp_data = at;
    temp_position = 1;
    temp_data_2 = st;
    temp_position_2 = 5;
    temp_delay = 7;
    set_ptemp(DAOYING2_PF);

    adc_obj_1byte(10, ATTACK_OFF);
    adc_obj_1byte((uint8_t)(skill_level / 9), STR_OFF);

    unwield_weapon();
    attack_xxx();
    attack_xxx();
    wield_weapon();
    attack_xxx();

    /* 恢復原攻擊/膂力(手動),再清 ptemp 還原位(=純冷卻) */
    pf_obj()[ATTACK_OFF] = at;
    pf_obj()[STR_OFF] = st;
    clear_xxx_ptemp(DAOYING2_PF);

    set_obj_busy(3);
    return 0;
}

/* 花間派:流星飞掷 FEIZHI */
static uint8_t feizhi(void)
{
    uint16_t lv0;
    uint8_t saved_at;
    uint32_t ap, dp;

    if (o16(STR_OFF) < 33) { return pf_fail(PT_NOT_STR); }  /* perform_str */

    set_kf = TONGJI_KF; set_level = 120;
    if (judge_kf(FORCE_KF)) return 1;
    set_kf = PIFENG_KF; set_level = 120;
    if (judge_kf(WEAPON_KF)) return 1;

    lv0 = skill_level;                  /* perform_temp4 = 判定後的 skill_level */
    neili_cost = 550;
    if (skill_level >= 150)
        neili_cost = 850;
    if (judge_force()) return 1;

    if (find_ptemp(FEIZHI_PF)) { return pf_fail(PT_BUSY); }

    sub_obj_fp();
    show_pf_text(PT_FEIZHI_SUCESS);

    temp_position = 0;
    temp_position_2 = 0;
    temp_delay = 9;
    set_ptemp(FEIZHI_PF);

    saved_at = (uint8_t)o16(ATTACK_OFF);        /* perform_temp7 */
    adc_obj_1byte(15, ATTACK_OFF);

    /* AP:主角(kf_type=WEAPON_KF 由前 judge_kf 保留) */
    kf_id = get_usekf(1) & 0x7F;                 /* get_usekf+kf_attr:設 kf_id(未參與威力) */
    ap = skill_xxx_power(0, 0);                  /* skill_power(man,attack,0) */

    /* DP:對手(npc),原版臨時翻 obj_flag → skill_npc_power,kf_type 不變。
     * 此後 skill_level = npc 側武功等級(feizhi_3 的 effhp 扣減用它) */
    kf_id = ((obj_flag & 0x80) ? npc.npc_usekf[2] : hero.man_usekf[2]) & 0x7F;
    dp = pf_skill_power((obj_flag & 0x80) ? 0 : 1, 1, 0);
    if (get_obj_busy())                          /* obj(man)busy → /3(施展時恆 0,死枝) */
        dp /= 3;

    if (random_it32(ap + dp) >= dp) {
        /* shoot:命中,傷 = (lv0 + 膂力)×2 */
        uint16_t dmg = (uint16_t)((lv0 + (uint8_t)o16(STR_OFF)) << 1);
        show_pf_text(PT_FEIZHI_SHOOT);
        if (sub_you_hp(dmg)) {                   /* you 存活 → 扣 effhp(npc 武功/2) */
            uint16_t half = skill_level >> 1;    /* 當下 skill_level = npc 側 */
            uint16_t eff = y16(EFFHP_OFF);
            yw16(EFFHP_OFF, (eff >= half) ? (uint16_t)(eff - half) : 0);
            set_obj_busy(3);
        }
    } else {
        show_pf_text(PT_FEIZHI_DODGE);
        set_obj_busy(4);
    }

    /* 還原攻擊,並丟棄飛出的兵器(消耗 1、卸下) */
    pf_obj()[ATTACK_OFF] = saved_at;
    lose_obj_weapon();
    return 0;
}

/* 红莲教:红莲出世 HONGLIAN */
static uint8_t honglian(void)
{
    uint16_t lv;

    set_kf = TONGJI_KF; set_level = 120;
    if (judge_kf(FORCE_KF)) return 1;
    neili_cost = 350;
    if (judge_force()) return 1;

    if (find_ptemp(HONGLIAN_PF)) { return pf_fail(PT_USING); }

    sub_obj_fp();
    show_pf_text(PT_HONGLIAN_SUCESS);

    lv = skill_level;
    temp_delay = (uint8_t)(lv / 20);
    temp_data = (uint8_t)o16(ATTACK_OFF);
    temp_position = 1;
    temp_position_2 = 0;
    set_ptemp(HONGLIAN_PF);

    adc_obj_1byte((uint8_t)(lv / 9), ATTACK_OFF);
    set_obj_busy(1);
    return 0;
}

/* 红莲教:雷动九天 LEIDONG */
static uint8_t leidong(void)
{
    uint16_t lv;

    set_kf = TONGJI_KF; set_level = 90;
    if (judge_kf(FORCE_KF)) return 1;
    neili_cost = 150;
    if (judge_force()) return 1;

    if (find_ptemp(LEIDONG_PF)) { return pf_fail(PT_USING); }

    sub_obj_fp();
    show_pf_text(PT_LEIDONG_SUCESS);

    lv = skill_level;
    temp_delay = (uint8_t)(lv / 20);
    temp_data = (uint8_t)o16(STR_OFF);
    temp_position = 5;
    temp_position_2 = 0;
    set_ptemp(LEIDONG_PF);

    adc_obj_1byte((uint8_t)(lv / 6), STR_OFF);
    return 0;
}

/* 太极门:缠字决 CHAN */
static uint8_t chan(void)
{
    uint32_t exp_o, r1, exp_y;

    if (get_you_busy()) { return pf_fail(PT_NPC_BUSY); }

    set_kf = TAIJIG_KF; set_level = 90;
    if (judge_kf(FORCE_KF)) return 1;
    set_kf = TAIJIJ_KF; set_level = 90;
    if (judge_kf(WEAPON_KF)) return 1;
    neili_cost = 250;
    if (judge_force()) return 1;

    if (find_ptemp(LIAN_PF)) { return pf_fail(PT_USING); }
    if (find_ptemp(CHAN_PF)) { return pf_fail(PT_BUSY); }

    sub_obj_fp();
    show_pf_text(PT_CHAN_SUCESS);

    exp_o = o32(EXP_OFF);
    r1 = random_it32(exp_o);            /* perform_random_it(exp) */
    exp_y = y32(EXP_OFF) / 3;
    if (r1 >= exp_y) {                  /* shoot */
        uint16_t rng = (uint16_t)(skill_level / 20);
        show_pf_text(PT_CHAN_SHOOT);
        set_you_busy((uint8_t)(random_it(rng) + 1));
    } else {
        show_pf_text(PT_CHAN_DODGE);
        set_obj_busy(3);
    }
    temp_position = 0;
    temp_position_2 = 0;
    temp_delay = 6;
    set_ptemp(CHAN_PF);
    return 0;
}

/* 太极门:连字决 LIAN */
static uint8_t lian(void)
{
    uint16_t lv;

    set_kf = TAIJIG_KF; set_level = 120;
    if (judge_kf(FORCE_KF)) return 1;
    set_kf = TAIJIJ_KF; set_level = 120;
    if (judge_kf(WEAPON_KF)) return 1;
    neili_cost = 350;
    if (judge_force()) return 1;

    if (find_ptemp(LIAN_PF)) { return pf_fail(PT_USING); }

    sub_obj_fp();
    show_pf_text(PT_LIAN_SUCESS);

    lv = skill_level;
    temp_delay = (uint8_t)(lv / 30 + 3);
    temp_data = (uint8_t)o16(DEFENSE_OFF);
    temp_position = 2;
    temp_data_2 = (uint8_t)o16(ATTACK_OFF);
    temp_position_2 = 1;
    set_ptemp(LIAN_PF);

    adc_obj_1byte((uint8_t)(lv / 15), DEFENSE_OFF);
    adc_obj_1byte(10, ATTACK_OFF);
    return 0;
}

/* 太极门:三环套月 TAOYUE(SANHUAN) */
static uint8_t taoyue(void)
{
    uint8_t dmg;

    set_kf = TAIJIG_KF; set_level = 180;
    if (judge_kf(FORCE_KF)) return 1;
    set_kf = TAIJIJ_KF; set_level = 180;
    if (judge_kf(WEAPON_KF)) return 1;
    neili_cost = 400;
    if (judge_force()) return 1;

    if (find_ptemp(LIAN_PF))    { return pf_fail(PT_USING); }
    if (find_ptemp(SANHUAN_PF)) { return pf_fail(PT_BUSY); }

    sub_obj_fp();
    show_pf_text(PT_TAOYUE_SUCESS);

    dmg = (uint8_t)o16(DAMAGE_OFF);
    temp_data = dmg;
    temp_position = 3;
    temp_position_2 = 0;
    temp_delay = 6;
    set_ptemp(SANHUAN_PF);

    adc_obj_1byte((uint8_t)(skill_level / 5), DAMAGE_OFF);

    perform_flag = 0x80; attack_xxx();
    perform_flag = 0x81; attack_xxx();
    perform_flag = 0x82; attack_xxx();
    perform_flag = 0;

    pf_obj()[DAMAGE_OFF] = dmg;
    clear_xxx_ptemp(SANHUAN_PF);
    set_obj_busy(3);
    return 0;
}

/* 太极门:挤字诀 JI */
static uint8_t ji(void)
{
    uint16_t r1, cmpv;

    set_kf = TAIJIG_KF; set_level = 105;
    if (judge_kf(FORCE_KF)) return 1;
    set_kf = TAIJIQ_KF; set_level = 105;
    if (judge_kf(HAND_KF)) return 1;
    neili_cost = 350;
    if (judge_force()) return 1;

    if (find_ptemp(JI_PF)) { return pf_fail(PT_BUSY); }

    sub_obj_fp();
    show_pf_text(PT_JI_SUCESS);

    r1 = (uint16_t)random_it(o16(FP_OFF));      /* random(obj fp) */
    cmpv = (uint16_t)(y16(FP_OFF) / 3);
    if (r1 >= cmpv) {                           /* shoot1 */
        uint16_t amt;
        show_pf_text(PT_JI_SHOOT);
        amt = (uint16_t)(neili_cost + o16(FP_OFF) / 10 + o16(FORCE_OFF));
        sub_you_fp(amt);
        temp_position = 0;
        temp_position_2 = 0;
        temp_delay = 2;
        set_ptemp(JI_PF);
    } else {
        cmpv = (uint16_t)(y16(FP_OFF) / 5);
        if (r1 >= cmpv) {                       /* shoot2 */
            show_pf_text(PT_JI_SHOOT1);
            sub_you_fp(neili_cost);
        } else {                                /* dodge */
            show_pf_text(PT_JI_DODGE);
            set_obj_busy((uint8_t)(random_it(3) + 1));
        }
    }
    return 0;
}

/* 太极门:乱环诀 LUANHUAN */
static uint8_t luanhuan(void)
{
    uint16_t r1, cmpv;

    set_kf = TAIJIG_KF; set_level = 150;
    if (judge_kf(FORCE_KF)) return 1;
    set_kf = TAIJIQ_KF; set_level = 150;
    if (judge_kf(HAND_KF)) return 1;
    neili_cost = 300;
    if (judge_force()) return 1;

    if (find_ptemp(LUANHUAN_PF)) { return pf_fail(PT_BUSY); }
    if (get_you_busy()) { return pf_fail(PT_NPC_BUSY); }

    sub_obj_fp();
    show_pf_text(PT_LUANHUAN_SUCESS);

    r1 = (uint16_t)random_it(skill_level);
    kf_type = PARRY_KF;
    if (obj_flag & 0x80) query_npc_skill(); else query_skill();  /* query_you_skill */
    cmpv = (uint16_t)(skill_level / 3);
    if (r1 >= cmpv) {                           /* shoot */
        uint8_t busy;
        show_pf_text(PT_LUANHUAN_SHOOT);
        set_you_busy((uint8_t)(r1 / 30 + 2));
        temp_position = 0;
        temp_position_2 = 0;
        busy = get_you_busy();
        temp_delay = (uint8_t)(busy + 4);
        set_ptemp(LUANHUAN_PF);
    } else {
        show_pf_text(PT_LUANHUAN_DODGE);
        set_obj_busy(2);
    }
    return 0;
}

/* 太极门:阴阳诀 YINYANG */
static uint8_t yinyang(void)
{
    uint8_t at, st;
    uint16_t r1, cmpv;

    set_kf = TAIJIG_KF; set_level = 180;
    if (judge_kf(FORCE_KF)) return 1;
    set_kf = TAIJIQ_KF; set_level = 180;
    if (judge_kf(HAND_KF)) return 1;
    neili_cost = 500;
    if (judge_force()) return 1;

    if (find_ptemp(YINYANG_PF)) { return pf_fail(PT_BUSY); }

    sub_obj_fp();
    show_pf_text(PT_YINYANG_SUCESS);

    /* you 忙 → 走攻擊分支;否則 1/5 機率走「攻擊(震)」分支 */
    if (get_you_busy() == 0 && random_it(5) == 0) {
        /* yinyang_3:類震字诀 */
        show_pf_text(PT_YINYANG_ATTACK);
        r1 = (uint16_t)random_it(skill_level);
        kf_type = PARRY_KF;
        if (obj_flag & 0x80) query_npc_skill(); else query_skill();
        cmpv = (uint16_t)(skill_level / 3);
        if (r1 >= cmpv) {                       /* shoot */
            show_pf_text(PT_YINYANG_SHOOT);
            set_you_busy((uint8_t)(r1 / 25 + 2));
            temp_position = 0;
            temp_position_2 = 0;
            temp_delay = 5;
            set_ptemp(YINYANG_PF);
        } else {
            show_pf_text(PT_YINYANG_DODGE);
            set_obj_busy(2);
        }
        return 0;
    }

    /* yinyang_1:類刀影(buff 攻/膂 + 一擊) */
    at = (uint8_t)o16(ATTACK_OFF);
    st = (uint8_t)o16(STR_OFF);
    temp_data = at;
    temp_position = 1;
    temp_data_2 = st;
    temp_position_2 = 5;
    temp_delay = 7;
    set_ptemp(YINYANG_PF);

    adc_obj_1byte(15, ATTACK_OFF);
    adc_obj_1byte((uint8_t)(skill_level / 5), STR_OFF);

    perform_flag = 0x80; attack_xxx();
    perform_flag = 0;

    pf_obj()[ATTACK_OFF] = at;
    pf_obj()[STR_OFF] = st;
    clear_xxx_ptemp(YINYANG_PF);
    set_obj_busy(3);
    return 0;
}

/* 太极门:震字诀 ZHEN */
static uint8_t zhen(void)
{
    uint16_t r1, cmpv;

    set_kf = TAIJIG_KF; set_level = 90;
    if (judge_kf(FORCE_KF)) return 1;
    set_kf = TAIJIQ_KF; set_level = 90;
    if (judge_kf(HAND_KF)) return 1;
    neili_cost = 200;
    if (judge_force()) return 1;

    if (find_ptemp(ZHEN_PF)) { return pf_fail(PT_BUSY); }

    sub_obj_fp();
    show_pf_text(PT_ZHEN_SUCESS);

    r1 = (uint16_t)random_it(o16(FP_OFF));
    cmpv = (uint16_t)(y16(FP_OFF) / 3);
    if (r1 >= cmpv) {                           /* zhen_1 */
        uint16_t t2 = (uint16_t)(o16(FP_OFF) / 10);
        t2 = (uint16_t)(t2 + o16(FORCE_OFF));
        {
            uint16_t sub = (uint16_t)(y16(FP_OFF) / 30);
            if (t2 < sub) {                     /* 借位 → dodge */
                show_pf_text(PT_ZHEN_DODGE);
                set_obj_busy(2);
                return 0;
            }
            t2 -= sub;
        }
        /* shoot */
        show_pf_text(PT_ZHEN_SHOOT);
        sub_you_hp(t2);
        {
            uint16_t half = t2 >> 1;
            uint16_t eff = y16(EFFHP_OFF);
            yw16(EFFHP_OFF, (eff >= half) ? (uint16_t)(eff - half) : 0);
        }
        temp_position = 0;
        temp_position_2 = 0;
        temp_delay = 2;
        set_ptemp(ZHEN_PF);
    } else {                                    /* zhen_4:再判 */
        uint16_t q = (uint16_t)(y16(FP_OFF) >> 2);  /* /4 */
        if (r1 >= q) {                          /* shoot2 */
            uint16_t yfp = y16(FP_OFF);
            /* show_text 前原版漏 jsr show_text(a9 已設),照搬:不顯示 */
            if (yfp < 200)
                yw16(FP_OFF, 0);
            else
                sub_you_fp(100);
        } else {
            show_pf_text(PT_ZHEN_DODGE1);
            set_obj_busy((uint8_t)(random_it(3) + 2));
        }
    }
    return 0;
}

/* 花間派:三花 SANHUA */
static uint8_t sanhua(void)
{
    uint16_t lv;
    uint8_t p1;

    set_kf = SANHUA_KF; set_level = 90;
    if (judge_kf(FORCE_KF)) return 1;
    neili_cost = 350;
    if (judge_force()) return 1;

    if (find_ptemp(SANHUA_PF)) { return pf_fail(PT_USING); }

    sub_obj_fp();
    show_pf_text(PT_SANHUA_SUCESS);

    lv = skill_level;
    p1 = (uint8_t)(lv / 20);
    temp_delay = (p1 < 8) ? p1 : 8;
    temp_data = (uint8_t)o16(DEX_OFF);
    temp_position = 6;
    temp_data_2 = (uint8_t)o16(DEFENSE_OFF);
    temp_position_2 = 2;
    set_ptemp(SANHUA_PF);

    adc_obj_1byte((uint8_t)(p1 << 1), DEX_OFF);     /* asl perform_temp1 = ×2 */
    /* defense = skill/5 - 5(手動,非還原) */
    {
        uint8_t d = (uint8_t)(lv / 5);
        pf_obj()[DEFENSE_OFF] = (uint8_t)(d - 5);
    }
    return 0;
}

/* 花間派:柳浪闻莺 LIULANG */
static uint8_t liulang(void)
{
    set_kf = SANHUA_KF; set_level = 120;
    if (judge_kf(FORCE_KF)) return 1;
    set_kf = MEIHUA_KF; set_level = 60;
    if (judge_kf(HAND_KF)) return 1;
    neili_cost = 200;
    set_kf = LIU_KF; set_level = 90;
    if (judge_kf(WEAPON_KF)) return 1;
    if (skill_level >= 120)
        neili_cost = 400;
    if (judge_force()) return 1;

    if (find_ptemp(SANHUA_PF))  { return pf_fail(PT_USING); }
    if (find_ptemp(LIULANG_PF)) { return pf_fail(PT_BUSY); }

    sub_obj_fp();
    show_pf_text(PT_LIULANG_SUCESS);

    temp_position = 0;
    temp_position_2 = 0;
    temp_delay = 7;
    set_ptemp(LIULANG_PF);

    unwield_weapon();
    attack_xxx();
    attack_xxx();
    wield_weapon();
    attack_xxx();

    set_obj_busy(3);
    return 0;
}

/* 花間派:落英缤纷 LUOYING */
static uint8_t luoying(void)
{
    set_kf = SANHUA_KF; set_level = 120;
    if (judge_kf(FORCE_KF)) return 1;
    set_kf = HUATUAN_KF; set_level = 120;
    if (judge_kf(WEAPON_KF)) return 1;
    neili_cost = 400;
    if (judge_force()) return 1;

    if (find_ptemp(LUOYING_PF)) { return pf_fail(PT_BUSY); }

    sub_obj_fp();
    show_pf_text(PT_LUOYING_SUCESS);

    if (y8(WEAPON_OFF) & 0x80) {
        /* npc 有兵器:奪兵器判定(range=obj 敏捷單字節) */
        uint8_t p1 = (uint8_t)random_it(o8(DEX_OFF));
        uint8_t ydex3 = (uint8_t)(y8(DEX_OFF) / 3);
        if (ydex3 < p1) {                       /* shoot:奪下 */
            show_pf_text(PT_LUOYING_SHOOT);
            /* set_you_weapon(0):卸下/消耗承受方兵器 */
            lose_you_weapon();
        } else {
            show_pf_text(PT_LUOYING_DODGE);
            set_obj_busy(2);
        }
    } else {
        /* npc 空手:傷 hp 判定 */
        uint16_t r1 = (uint16_t)random_it(skill_level);
        uint16_t cmpv;
        kf_type = DODGE_KF;
        if (obj_flag & 0x80) query_npc_skill(); else query_skill();
        cmpv = (uint16_t)(skill_level / 3);
        if (r1 >= cmpv) {                       /* shoot1 */
            show_pf_text(PT_LUOYING_SHOOT1);
            if (sub_you_hp(r1)) {               /* you 存活 → 扣 effhp r1/2 + busy 2 */
                uint16_t half = r1 >> 1;
                uint16_t eff = y16(EFFHP_OFF);
                yw16(EFFHP_OFF, (eff >= half) ? (uint16_t)(eff - half) : 0);
                set_you_busy(2);
            }
        } else {
            show_pf_text(PT_LUOYING_DODGE1);
            set_obj_busy(2);
        }
    }

    temp_position = 0;
    temp_position_2 = 0;
    temp_delay = 6;
    set_ptemp(LUOYING_PF);
    return 0;
}

/* 雪山剑派:雪花六出 LIUCHU */
static uint8_t liuchu(void)
{
    uint8_t reps, at;

    set_kf = XUESHANG_KF; set_level = 90;
    if (judge_kf(FORCE_KF)) return 1;
    set_kf = XUESHANJ_KF; set_level = 90;
    if (judge_kf(WEAPON_KF)) return 1;

    /* neili_cost = (skill-90)/30*150 + 250,上限 600 */
    {
        uint16_t a1 = (uint16_t)((skill_level - 90) / 30);
        a1 = (uint16_t)(a1 * 150 + 250);
        neili_cost = (a1 < 600) ? a1 : 600;
    }
    if (judge_force()) return 1;

    if (find_ptemp(LIUCHU_PF)) { return pf_fail(PT_BUSY); }

    sub_obj_fp();
    show_pf_text(PT_LIUCHU_SUCESS);

    /* reps = (skill-90)/30 + 2,上限 5 */
    {
        uint16_t p1 = (uint16_t)((skill_level - 90) / 30);
        p1 += 2;
        reps = (p1 < 5) ? (uint8_t)p1 : 5;
    }

    at = (uint8_t)o16(ATTACK_OFF);
    temp_data = at;
    temp_position = 1;
    temp_position_2 = 0;
    temp_delay = 10;
    set_ptemp(LIUCHU_PF);

    adc_obj_1byte(10, ATTACK_OFF);

    do {
        attack_xxx();
    } while (--reps);

    pf_obj()[ATTACK_OFF] = at;
    clear_xxx_ptemp(LIUCHU_PF);
    set_obj_busy(3);
    return 0;
}

/* 雪山剑派:神倒鬼跌 SHENGUI(QINNA) */
static uint8_t shengui(void)
{
    uint16_t r1, cmpv;

    set_kf = XUEYING_KF; set_level = 120;
    if (judge_kf(HAND_KF)) return 1;
    neili_cost = 350;
    if (judge_force()) return 1;

    if (get_you_busy()) { return pf_fail(PT_NPC_BUSY); }
    if (find_ptemp(QINNA_PF)) { return pf_fail(PT_BUSY); }

    sub_obj_fp();
    show_pf_text(PT_SHENGUI_SUCESS);

    r1 = (uint16_t)random_it(o16(FP_OFF));
    cmpv = (uint16_t)(y16(FP_OFF) / 2);
    if (r1 >= cmpv) {                           /* shoot */
        uint16_t rng = (uint16_t)(skill_level / 35);
        show_pf_text(PT_SHENGUI_SHOOT);
        set_you_busy((uint8_t)(random_it(rng) + 3));
        sub_you_hp((uint16_t)(skill_level / 3));
    } else {
        show_pf_text(PT_SHENGUI_DODGE);
        set_obj_busy(2);
    }
    temp_position = 0;
    temp_position_2 = 0;
    temp_delay = 5;
    set_ptemp(QINNA_PF);
    return 0;
}

/* 雪山剑派:冰心决 BINGXIN */
static uint8_t bingxin(void)
{
    uint16_t lv;
    uint8_t dly;

    neili_cost = 150;
    set_kf = XUESHANG_KF; set_level = 75;
    if (judge_kf(FORCE_KF)) return 1;
    if (skill_level >= 90)
        neili_cost = 250;
    if (judge_force()) return 1;

    if (find_ptemp(BINGXIN_PF)) { return pf_fail(PT_USING); }

    sub_obj_fp();
    show_pf_text(PT_BINGXIN_SUCESS);

    lv = skill_level;
    dly = (uint8_t)(lv / 20);
    temp_delay = (dly < 10) ? dly : 10;
    temp_data = (uint8_t)o16(ARMOR_OFF);
    temp_position = 4;
    temp_position_2 = 0;
    set_ptemp(BINGXIN_PF);

    /* armor += skill/4,skill/4 上限 100 */
    {
        uint16_t s = (uint16_t)(lv >> 2);
        if (s > 100) s = 100;
        adc_obj_1byte((uint8_t)s, ARMOR_OFF);
    }
    return 0;
}

/* 忍者流:旋风三连斩 LIANZHAN */
static uint8_t lianzhan(void)
{
    if (get_obj_busy()) { return pf_fail(PT_MAN_BUSY); }

    set_kf = RENSHU_KF; set_level = 120;
    if (judge_kf(FORCE_KF)) return 1;
    set_kf = YIDAO_KF; set_level = 90;
    if (judge_kf(WEAPON_KF)) return 1;
    neili_cost = 350;
    if (judge_force()) return 1;

    if (find_ptemp(YIDAOZHAN_PF)) { return pf_fail(PT_BUSY); }
    if (find_ptemp(LIANZHAN_PF))  { return pf_fail(PT_BUSY); }

    sub_obj_fp();
    show_pf_text(PT_LIANZHAN_SUCESS);

    perform_flag = 0x80; attack_xxx();
    perform_flag = 0x81; attack_xxx();
    perform_flag = 0x82; attack_xxx();
    perform_flag = 0;

    temp_position = 0;
    temp_position_2 = 0;
    temp_delay = 5;
    set_ptemp(LIANZHAN_PF);

    set_obj_busy(1);
    return 0;
}

/* 忍者流:迎风一刀斩 YIDAO(YIDAOZHAN) */
static uint8_t yidao(void)
{
    uint8_t dm, at;

    if (get_obj_busy()) { return pf_fail(PT_MAN_BUSY); }

    set_kf = RENSHU_KF; set_level = 120;
    if (judge_kf(FORCE_KF)) return 1;
    set_kf = YIDAO_KF; set_level = 120;
    if (judge_kf(WEAPON_KF)) return 1;
    neili_cost = 550;
    if (judge_force()) return 1;

    if (find_ptemp(YIDAOZHAN_PF)) { return pf_fail(PT_BUSY); }

    sub_obj_fp();
    show_pf_text(PT_YIDAO_SUCESS);

    dm = (uint8_t)o16(DAMAGE_OFF);
    at = (uint8_t)o16(ATTACK_OFF);
    temp_data = dm;
    temp_position = 3;
    temp_data_2 = at;
    temp_position_2 = 1;
    temp_delay = 7;
    set_ptemp(YIDAOZHAN_PF);

    adc_obj_1byte((uint8_t)(skill_level / 3 + 20), DAMAGE_OFF);
    adc_obj_1byte(15, ATTACK_OFF);

    attack_xxx();

    pf_obj()[DAMAGE_OFF] = dm;
    pf_obj()[ATTACK_OFF] = at;
    clear_xxx_ptemp(YIDAOZHAN_PF);
    set_obj_busy(1);
    return 0;
}

/* 忍者流:影分身 FENSHEN */
static uint8_t fenshen(void)
{
    uint16_t lv;
    uint8_t v;

    obj_flag = 0x80;                    /* smb7 obj_flag(原版) */

    set_kf = RENSHU_KF; set_level = 120;
    if (judge_kf(FORCE_KF)) return 1;
    neili_cost = 550;
    if (judge_force()) return 1;

    if (find_ptemp(FENSHEN_PF)) { return pf_fail(PT_USING); }

    /* sub man_fp,neili_cost(直接對 man) */
    {
        uint16_t fp = hero.man_fp;
        hero.man_fp = (fp >= neili_cost) ? (uint16_t)(fp - neili_cost) : 0;
    }
    show_pf_text(PT_FENSHEN_SUCESS);
    fenshen_feature_mark();

    lv = skill_level;
    temp_delay = (uint8_t)(lv / 20);
    temp_data = hero.man_per;
    temp_position = 9;
    temp_data_2 = hero.man_kar;
    temp_position_2 = 10;
    set_ptemp(FENSHEN_PF);

    v = (uint8_t)(lv / 5);
    hero.man_per = (v >= 40) ? v : 30;
    hero.man_kar = 0xFF;                /* kar=0xFF 為使用 fenshen 的旗標 */
    return 0;
}

/* 忍者流:烟幕 YIANMU */
static uint8_t yianmu(void)
{
    uint16_t r1, t1;
    uint8_t p1;

    set_kf = RENSHU_KF; set_level = 90;
    if (judge_kf(FORCE_KF)) return 1;
    neili_cost = 300;
    if (judge_force()) return 1;

    /* 命中前置:random(obj fp) < you_fp/3 → 失敗 */
    t1 = (uint16_t)(y16(FP_OFF) / 3);
    r1 = (uint16_t)random_it(o16(FP_OFF));
    if (r1 < t1) {                      /* yianmu_fail */
        set_obj_busy(2);
        show_pf_text(PT_YIANMU_FAIL);
        return 0;
    }

    if (find_ptemp(YIANMU_PF)) { return pf_fail(PT_BUSY); }

    sub_obj_fp();
    show_pf_text(PT_YIANMU_SUCESS);

    temp_delay = (uint8_t)(skill_level / 20);
    temp_data = (uint8_t)y16(ATTACK_OFF);       /* 存承受方攻擊 */
    temp_position = 11;                          /* is_npc_attack:還原對手攻擊 */
    temp_position_2 = 0;
    set_ptemp(YIANMU_PF);

    /* 對手攻擊 -= max(20, skill/8) */
    p1 = 20;
    {
        uint16_t s = (uint16_t)(skill_level / 8);
        if (s >= 20) p1 = (uint8_t)s;
    }
    {
        uint8_t ya = (uint8_t)y16(ATTACK_OFF);
        pf_you()[ATTACK_OFF] = (ya >= p1) ? (uint8_t)(ya - p1) : 0;
    }
    return 0;
}

/* ================= 野球拳(GBC original, BSC only) ================= */
static const uint8_t yakyu_cast_msg[] = {
    /* $N嘴角一勾，似笑非笑，以迅雷不及掩耳之势贴近$n，使出了野球拳！ */
    0x24,0x4E,0xD7,0xEC,0xBD,0xC7,0xD2,0xBB,
    0xB9,0xB4,0xA3,0xAC,0xCB,0xC6,0xD0,0xA6,
    0xB7,0xC7,0xD0,0xA6,0xA3,0xAC,0xD2,0xD4,
    0xD1,0xB8,0xC0,0xD7,0xB2,0xBB,0xBC,0xB0,
    0xD1,0xDA,0xB6,0xFA,0xD6,0xAE,0xCA,0xC6,
    0xCC,0xF9,0xBD,0xFC,0x24,0x6E,0xA3,0xAC,
    0xCA,0xB9,0xB3,0xF6,0xC1,0xCB,0xD2,0xB0,
    0xC7,0xF2,0xC8,0xAD,0xA3,0xA1,
    0x00, 0x00
};
static const uint8_t yakyu_hit_msg[] = {
    /* $n顿时心神大乱，羞红了脸，呆若木鸡！ */
    0x24,0x6E,0xB6,0xD9,0xCA,0xB1,0xD0,0xC4,
    0xC9,0xF1,0xB4,0xF3,0xC2,0xD2,0xA3,0xAC,
    0xD0,0xDF,0xBA,0xEC,0xC1,0xCB,0xC1,0xB3,
    0xA3,0xAC,0xB4,0xF4,0xC8,0xF4,0xC4,0xBE,
    0xBC,0xA6,0xA3,0xA1,
    0x00, 0x00
};
static const uint8_t yakyu_miss_msg[] = {
    /* 可惜被$n轻巧闪过 */
    0xBF,0xC9,0xCF,0xA7,0xB1,0xBB,0x24,0x6E,
    0xC7,0xE1,0xC7,0xC9,0xC9,0xC1,0xB9,0xFD,
    0x00, 0x00
};

/* 訊息在本 bank(26)ROM,而 pf_show_fight_msg 是 bank 27 BANKED:呼叫即
 * 切走 0x4000 窗,原指針會讀到 bank 27 垃圾(fight.c:124 同一陷阱)。故比照
 * show_pf_text 先拷入 pf_msg(WRAM)再送;net 廣播的 memcpy 也才讀得對。 */
static void show_yakyu_msg(const uint8_t *m, uint8_t n)
{
    uint8_t i;

    if (net_flag & 0x80)
        net_repeat = 2;                 /* 同 show_pf_text:淨戰計數 */
    for (i = 0; i < n; i++)
        pf_msg[i] = m[i];
    pf_msg[n] = 0;
    pf_msg[n + 1] = 0;
    pf_show_fight_msg(pf_msg);
}

static uint8_t yakyu(void)
{
    uint8_t obj_gender, tgt_gender, stun;

    set_kf = MENGHU_KF; set_level = 50;
    if (judge_kf(HAND_KF)) return 1;
    neili_cost = 334;
    if (judge_force()) return 1;

    sub_obj_fp();
    show_yakyu_msg(yakyu_cast_msg, sizeof(yakyu_cast_msg) - 2);

    obj_gender = o8(GENDER_OFF);
    tgt_gender = y8(GENDER_OFF);
    stun = (uint8_t)(skill_level / 20);

    if (obj_gender == 2) {
        if (tgt_gender == 0) {
            set_you_busy(stun);
            show_yakyu_msg(yakyu_hit_msg, sizeof(yakyu_hit_msg) - 2);
        } else {
            show_yakyu_msg(yakyu_miss_msg, sizeof(yakyu_miss_msg) - 2);
        }
    } else {
        if (tgt_gender == 1) {
            set_you_busy(stun);
            show_yakyu_msg(yakyu_hit_msg, sizeof(yakyu_hit_msg) - 2);
        } else {
            show_yakyu_msg(yakyu_miss_msg, sizeof(yakyu_miss_msg) - 2);
        }
    }
    return 0;
}

/* ================= 分派器(hello.s perform) ================= */
uint8_t perform_dispatch(void) BANKED
{
    if (get_obj_busy())                 /* BUSY_OFF != 0 → show_man_busy */
        return pf_fail(PT_MAN_BUSY);
    switch (perform_id) {
    case DAOYING1_PF:   return daoying_gua();
    case DAOYING2_PF:   return daoying_zhen();
    case ZHANGDAO1_PF:  return zhangdao_gua();
    case ZHANGDAO2_PF:  return zhangdao_zhen();
    case LUOYING_PF:    return luoying();
    case LIULANG_PF:    return liulang();
    case SANHUA_PF:     return sanhua();
    case FEIZHI_PF:     return feizhi();
    case HONGLIAN_PF:   return honglian();
    case LEIDONG_PF:    return leidong();
    case FENSHEN_PF:    return fenshen();
    case YIANMU_PF:     return yianmu();
    case LIANZHAN_PF:   return lianzhan();
    case YIDAOZHAN_PF:  return yidao();
    case CHAN_PF:       return chan();
    case LIAN_PF:       return lian();
    case SANHUAN_PF:    return taoyue();
    case JI_PF:         return ji();
    case LUANHUAN_PF:   return luanhuan();
    case YINYANG_PF:    return yinyang();
    case ZHEN_PF:       return zhen();
    case BINGXIN_PF:    return bingxin();
    case LIUCHU_PF:     return liuchu();
    case QINNA_PF:      return shengui();
    case YAKYU_PF:      return yakyu();
    default:            return 1;
    }
}
