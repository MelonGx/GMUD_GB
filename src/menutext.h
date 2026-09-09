/* 自動產生:gen_menutext.py,勿手改 */
#ifndef MENUTEXT_H
#define MENUTEXT_H
#include <stdint.h>
#include "text.h"

/* type9 記錄(len+ptr)落在 gfx_scratch UI 複用區(見 text.h) */
#define mt_wuyi_rec     (gfx_scratch + 92)
#define mt_chushou_rec  (gfx_scratch + 96)
#define mt_chardesc_rec (gfx_scratch + 92)

extern const uint8_t mt_confirm_msg[21];
extern const uint8_t mt_if_save_msg[32];
extern const uint8_t mt_save_fail_msg[11];
extern const uint8_t mt_save_succ_msg[11];
extern const uint8_t mt_xingbei[6];
extern const uint8_t mt_no_per_msg[9];
extern const uint8_t mt_man_per_desc[144];
extern const uint8_t mt_girl_per_desc[144];
extern const uint8_t mt_hp_msg[104];
void fix_mt_hp_msg(uint8_t *b);
extern const uint8_t mt_desc_msg[73];
void fix_mt_desc_msg(uint8_t *b);
extern const uint8_t mt_score_msg[139];
void fix_mt_score_msg(uint8_t *b);
extern const uint8_t mt_heal_enable_msg[31];
extern const uint8_t mt_heal_sucess_msg[32];
extern const uint8_t mt_heal_over_msg[68];
extern const uint8_t mt_heal_noeff_msg[51];
extern const uint8_t mt_heal_noeff_msg1[32];
extern const uint8_t mt_heal_maxneili_msg[28];
extern const uint8_t mt_heal_nohurt_msg[15];
extern const uint8_t mt_heal_badhurt_msg[42];
extern const uint8_t mt_heal_level_msg[39];
extern const uint8_t mt_dazuo_level_msg[19];
extern const uint8_t mt_jiali_flow_msg[22];
void fix_mt_jiali_flow_msg(uint8_t *b);
extern const uint8_t mt_not_neili_msg[15];
extern const uint8_t mt_recover_over_msg[36];
extern const uint8_t mt_recover_hp_msg[17];
extern const uint8_t mt_up_msg[16];
extern const uint8_t mt_no_basic_msg[37];
extern const uint8_t mt_no_weapon_msg[33];
extern const uint8_t mt_exp_msg[37];
extern const uint8_t mt_basic_low_msg[47];
extern const uint8_t mt_maxfp_msg[29];
extern const uint8_t mt_hurt_msg[26];
extern const uint8_t mt_book_fail1_msg[13];
extern const uint8_t mt_book_fail2_msg[21];
extern const uint8_t mt_book_bao_msg[26];
extern const uint8_t mt_book_bao_msg2[24];
extern const uint8_t mt_deal_msg[23];
extern const uint8_t mt_deal1_msg[27];
extern const uint8_t mt_learn_msg[19];
extern const uint8_t mt_have_master_msg[38];
extern const uint8_t mt_have_kf_msg[26];
extern const uint8_t mt_apprentice_msg[73];
extern const uint8_t mt_daxia_low_msg[24];
extern const uint8_t mt_goods_price_msg[22];
void fix_mt_goods_price_msg(uint8_t *b);
extern const uint8_t mt_learn_pot_msg[26];
extern const uint8_t mt_learn_money_msg[37];
extern const uint8_t mt_skill_high_msg[39];
extern const uint8_t mt_learn_confirm_msg[19];
extern const uint8_t mt_basic_kf_msg[36];
extern const uint8_t mt_kfdesc_msg[18];
void fix_mt_kfdesc_msg(uint8_t *b);
extern const uint8_t mt_main_menu_i0[5];
extern const uint8_t mt_main_menu_i1[5];
extern const uint8_t mt_main_menu_i2[5];
extern const uint8_t mt_main_menu_i3[5];
extern const uint8_t *const mt_main_menu_items[4];
extern const uint8_t mt_sys_menu_i0[6];
extern const uint8_t mt_sys_menu_i1[6];
extern const uint8_t mt_sys_menu_i2[6];
extern const uint8_t mt_sys_menu_i3[6];
extern const uint8_t *const mt_sys_menu_items[4];
extern const uint8_t mt_neili_menu_i0[6];
extern const uint8_t mt_neili_menu_i1[6];
extern const uint8_t mt_neili_menu_i2[6];
extern const uint8_t mt_neili_menu_i3[6];
extern const uint8_t *const mt_neili_menu_items[4];
extern const uint8_t mt_look_menu_i0[5];
extern const uint8_t mt_look_menu_i1[5];
extern const uint8_t mt_look_menu_i2[5];
extern const uint8_t *const mt_look_menu_items[3];
extern const uint8_t mt_goods_menu_i0[5];
extern const uint8_t mt_goods_menu_i1[5];
extern const uint8_t mt_goods_menu_i2[5];
extern const uint8_t mt_goods_menu_i3[5];
extern const uint8_t mt_goods_menu_i4[5];
extern const uint8_t mt_goods_menu_i5[5];
extern const uint8_t *const mt_goods_menu_items[6];
extern const uint8_t mt_left_menu_i0[5];
extern const uint8_t mt_left_menu_i1[5];
extern const uint8_t mt_left_menu_i2[5];
extern const uint8_t mt_left_menu_i3[5];
extern const uint8_t mt_left_menu_i4[5];
extern const uint8_t mt_left_menu_i5[5];
extern const uint8_t *const mt_left_menu_items[6];

#endif
