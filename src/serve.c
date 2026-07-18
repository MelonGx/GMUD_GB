/* serve.c - 內力四式與練功邏輯,逐分支移植原版 lee1.s(dazuo/recover/
 * heal/jiali)與 pyh.s(pyh_practice/pyh_learn)。演算法與訊息全對照原版。
 * 代碼在 bank 26:tick 經 HOME 包裝(skill.c)或 25 端 BANKED 呼叫;
 * mt_* 訊息只作指針傳給 25 端讀者,模板複製走 mt_tpl_jiali(ui.c)。 */
#pragma bank 26
#include <gb/gb.h>
#include <string.h>
#include "serve.h"
#include "save.h"
#include "skill.h"
#include "gamedata.h"
#include "text.h"
#include "ui.h"
#include "menutext.h"
#include "blit.h"
#include "fb.h"

/* 等效 lee1.s common:視角=主角 */
static void common(void)
{
    obj_flag = 0x80;
    set_obj(0);
}

/* 內功未啟用的共同失敗出口 */
static uint8_t force_not_enabled(void)
{
    if (hero.man_usekf[FORCE_KF] & 0x80)
        return 0;
    show_text(mt_heal_enable_msg);
    return 1;
}

/* ---- 打坐 tick(等效 lee1.s dazuo;1=停) ---- */
uint8_t dazuo(void) BANKED
{
    uint16_t a8;

    common();
    if (force_not_enabled())
        return 1;

    kf_type = FORCE_KF;
    query_skill();
    if (skill_level == 0)
        goto no_level;

    a8 = skill_level / 10 + hero.man_con / 5;

    if (hero.man_fp < (uint16_t)(hero.man_maxfp << 1)) {
        hero.man_fp += a8;
        return 0;
    }
    /* 內力已滿 2 倍:嘗試擴容 maxfp=有效等級*10+根骨*(年齡-14)+exp/1000 */
    {
        uint8_t age = hero.man_age;
        if (age > 60)
            age = 60;
        a8 = skill_level * 10;
        a8 += (uint16_t)hero.attr_con * (age - 14);
        a8 += (uint16_t)(hero.man_exp / 1000);
    }
    if (hero.man_maxfp >= a8)
        goto no_level;
    hero.man_maxfp++;
    hero.man_fp = 0;
    return 0;

no_level:
    hero.man_fp = hero.man_maxfp;           /* 原版 dazuo_no_level */
    show_text(mt_dazuo_level_msg);
    return 1;
}

/* ---- 吸氣(等效 lee1.s recover,一次一擊) ---- */
uint8_t recover(void) BANKED
{
    uint16_t t1, keeneed, cost, gain;

    net_msg_vision |= 0x80;     /* perform.s:127(淨戰:吸氣期間訊息不上
                                 * 對方畫面;單機恆 0 無副作用) */
    common();
    if (force_not_enabled())
        return 1;

    kf_type = FORCE_KF;
    query_skill();

    if (hero.man_fp < 20) {
        show_text(mt_not_neili_msg);
        return 1;
    }
    t1 = 10 + skill_level / 15;
    keeneed = hero.man_effhp - hero.man_hp;
    cost = keeneed * 20 / t1 + 1;
    if (cost > hero.man_fp)
        cost = hero.man_fp;
    if (keeneed == 0) {
        show_text(mt_recover_hp_msg);
        return 1;
    }
    gain = cost * t1 / 20;
    hero.man_hp += gain;
    if (hero.man_hp > hero.man_effhp)
        hero.man_hp = hero.man_effhp;
    hero.man_fp = (hero.man_fp >= cost) ? hero.man_fp - cost : 0;
    show_text(mt_recover_over_msg);
    return 1;
}

/* ---- 療傷(等效 lee1.s heal) ---- */
uint8_t heal(void) BANKED
{
    uint16_t diff, gain;

    common();
    if (force_not_enabled())
        return 1;

    kf_type = FORCE_KF;
    query_skill();

    if (skill_level < 45) {
        show_text(mt_heal_level_msg);
        return 1;
    }
    if (hero.man_maxfp < 150) {
        show_text(mt_heal_maxneili_msg);
        return 1;
    }
    if (hero.man_fp < 100) {
        show_text(mt_not_neili_msg);
        return 1;
    }
    if (hero.man_effhp == hero.man_maxhp) {
        show_text(mt_heal_nohurt_msg);
        return 1;
    }
    if (hero.man_effhp < hero.man_maxhp / 3) {
        show_text(mt_heal_badhurt_msg);
        return 1;
    }

    show_text(mt_heal_sucess_msg);

    diff = hero.man_maxhp - hero.man_effhp;
    if (diff == 0) {
        show_text(mt_heal_noeff_msg);
        show_text(mt_heal_noeff_msg1);
        return 1;
    }
    gain = 10 + skill_level / 5;
    if (gain > diff)
        gain = diff;
    hero.man_effhp += gain;
    hero.man_fp -= 50;
    show_text(mt_heal_over_msg);
    return 1;
}

/* ---- 加力(等效 lee1.s jiali:上限=內功有效等級/2) ---- */
void jiali(void) BANKED
{
    uint16_t maxv, v;
    uint8_t *tpl = gfx_scratch + 192;

    common();
    if (force_not_enabled())
        return;

    kf_type = FORCE_KF;
    query_skill();
    maxv = skill_level >> 1;

    v = input_digit(53, 44, hero.man_force, maxv);
    hero.man_force = v;
    if (v >= maxv) {                        /* 頂到上限:提示 */
        mt_tpl_jiali(tpl);                  /* 模板在 bank25,經 ui 助手 */
        show_text(tpl);
    }
}

/* ---- 學習 tick(等效 pyh.s pyh_learn;1=停) ----
 * learn_master_lvl = 原版 game_buf(learn_it 填入的師父等級)。
 * 順序照原版:師父等級→經驗→潛能(先扣)→識字學費→悟性/機緣點數 */
uint8_t learn_master_lvl;

uint8_t pyh_learn(void) BANKED
{
    uint8_t y, my_skill;
    uint32_t need;
    uint16_t fee, gain;
    const uint8_t *msg;

    y = find_kf(kf_id);
    if (y == 0xFF)
        return 1;
    my_skill = hero.man_kf[y + 1];

    if (learn_master_lvl < my_skill) {      /* 功夫高過師父 */
        msg = mt_skill_high_msg;
        goto fail;
    }

    need = (uint32_t)my_skill * my_skill * my_skill / 10;
    if (hero.man_exp < need) {
        msg = mt_exp_msg;
        goto fail;
    }

    if (hero.man_pot == 0) {
        msg = mt_learn_pot_msg;
        goto fail;
    }
    hero.man_pot--;                         /* 原版先扣潛能再查學費 */

    if (kf_id == LITERATE_KF) {             /* 讀書識字學費階梯 */
        if (my_skill < 20)
            fee = 5;
        else if (my_skill < 30)
            fee = 10;
        else if (my_skill < 60)
            fee = 50;
        else if (my_skill < 80)
            fee = 150;
        else if (my_skill < 100)
            fee = 300;
        else if (my_skill < 120)
            fee = 500;
        else
            fee = 1000;
        if (hero.man_money < fee) {
            msg = mt_learn_money_msg;
            goto fail;
        }
        hero.man_money -= fee;
    }

    /* 1 潛能 → 點數 = [悟性/2 + rand(悟性)]/2 + rand(機緣/5) */
    gain = (uint8_t)((hero.man_int >> 1) + random_it(hero.man_int)) >> 1;
    gain += random_it(hero.man_kar / 5);
    if (improve_skill(gain)) {
        show_text(mt_up_msg);
        scroll_to_lcd();
        fb_flush();
        /* 升級後詢問是否續學(原版 message_box_for_pyh) */
        if (!confirm_box(6, 42, 6 + 12 * 12, 70, mt_learn_confirm_msg)) {
            scroll_to_lcd();
            fb_flush();
            return 1;
        }
        scroll_to_lcd();
        fb_flush();
    }
    return 0;

fail:
    show_text(msg);
    return 1;
}

/* ---- 練功 tick(等效 pyh.s pyh_practice;1=停) ---- */
uint8_t pyh_practice(void) BANKED
{
    uint8_t y, my_skill, master_skill, basic;
    uint32_t need;
    const uint8_t *msg;

    if (hero.man_effhp != hero.man_maxhp) {
        msg = mt_hurt_msg;
        goto fail;
    }

    y = find_kf(kf_id);
    if (y == 0xFF)
        return 1;
    my_skill = hero.man_kf[y + 1];

    if (!check_skill(kf_id)) {
        msg = check_err ? mt_no_weapon_msg : mt_no_basic_msg;
        goto fail;
    }

    basic = get_basic_kf(kf_attr[0]);       /* check_skill 已填 kf_attr */
    y = find_kf(basic);
    if (y == 0xFF)
        return 1;
    master_skill = hero.man_kf[y + 1];
    if (master_skill < my_skill) {
        msg = mt_basic_low_msg;
        goto fail;
    }

    /* 經驗門檻 skill³/10 */
    need = (uint32_t)my_skill * my_skill * my_skill / 10;
    if (hero.man_exp < need) {
        msg = mt_exp_msg;
        goto fail;
    }

    /* 內力上限門檻 (skill + basic/2)*10 */
    if (hero.man_maxfp < ((uint16_t)my_skill + master_skill / 2) * 10) {
        msg = mt_maxfp_msg;
        goto fail;
    }

    if (improve_skill(master_skill / 5 + 1)) {
        show_text(mt_up_msg);
        scroll_to_lcd();
        fb_flush();
    }
    return 0;

fail:
    show_text(msg);
    return 1;
}
