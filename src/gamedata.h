/* 自動產生:gen_gamedata.py(goods.dat/skill.dat/skill.s),勿手改 */
#ifndef GAMEDATA_H
#define GAMEDATA_H
#include <stdint.h>

#define GAMEDATA_BANK 24

#define GOODS_NUM 82
#define KF_NUM 43
#define PF_NUM 25
#define BASIC_KF_NUM 9
#define NONE_PAI_KF 11

/* goods_type */
#define FOOD_WU 0
#define DRUG_WU 1
#define WEAPON_WU 2
#define EQUIP_WU 3
#define BOOK_WU 4
#define OTHER_WU 5

/* weapon_type */
#define AXE_QI 0
#define BLADE_QI 1
#define CLUB_QI 2
#define DAGGER_QI 3
#define FORK_QI 4
#define HAMMER_QI 5
#define SWORD_QI 6
#define STAFF_QI 7
#define THROW_QI 8
#define WHIP_QI 9

/* kf_type */
#define HAND_KF 0
#define WEAPON_KF 1
#define DODGE_KF 2
#define FORCE_KF 3
#define PARRY_KF 4
#define LEARN_KF 5

/* 基本功/知識 kf_id */
#define BASIC_FORCE_KF 0
#define BASIC_BARE_KF 1
#define BASIC_SWORD_KF 2
#define BASIC_BLADE_KF 3
#define BASIC_CLUB_KF 4
#define BASIC_STAFF_KF 5
#define BASIC_WHIP_KF 6
#define BASIC_DODGE_KF 7
#define BASIC_PARRY_KF 8
#define LITERATE_KF 9
#define LOOKS_KF 10

/* 特殊物品 id */
#define SANJIAO_GOODS 77
#define BAODIAN_GOODS 81
#define CLOTH_GOODS 42
#define JIAOYI_KF 22

/* GBC 原創輕功:凌波微步(跳舞毯 573 分,無派/逍遙派限定)。
 * 閃避訊息不在 textbank,見 fight.c random_kf_action。 */
#define LINGBO_KF 42

/* 拜師 NPC(serve.s bashi_npc_tbl 序)與門派武功 */
#define JIANMING_NPC 44
#define BAOZHEN_NPC 39
#define WEIYANG_NPC 48
#define QINGZHAO_NPC 59
#define PINGPOPO_NPC 57
#define QINGHONG_NPC 58
#define WANGCI_NPC 67
#define FANGZHANGLAO_NPC 74
#define YUHONGRU_NPC 81
#define ZHONGYANG_NPC 95
#define MEINA_NPC 88
#define TENGWANG_NPC 91
#define CANGYUE_NPC 97
#define GUSONG_NPC 98
#define QINGXU_NPC 102
#define XJIAOTOU_NPC 123
#define BAIRUIDE_NPC 111
#define WANJIAN_NPC 119
#define HUNYUAN_KF 14
#define BAGUAD_KF 11
#define MEIHUA_KF 19
#define TONGJI_KF 25
#define RENSHU_KF 26
#define WUFA_KF 27
#define TAIJIG_KF 32
#define TAIJIQ_KF 31
#define XUESHANG_KF 36

/* 名字表:0xFF 結尾條目按 id 連續排列(= 原版 find_name 佈局);
 * pai 表 0 結尾(npc.s 原樣) */
extern const uint8_t goods_name_tbl[558];
extern const uint8_t kf_name_tbl[391];
extern const uint8_t pf_name_tbl[215];
extern const uint8_t pai_name_tbl[60];

/* attr 表:goods 8B/條(BOOK 條目字節[2]=書表索引,原版為 RAM 指針);
 * kf 2B/條;pf = perform→kf 映射 + 0xFF 哨兵 */
extern const uint8_t goods_attr_tbl[656];
extern const uint8_t kf_attr_tbl[86];
extern const uint8_t pf_attr_tbl[26];

/* 武功等級/出手描述:8B(4字)×51、4B(2字)×6,無結尾符;
 * 等級表前50筆來自 skill.s,index50 為 GBC 人物總評追加「极轻很轻」。 */
#define SKILL_LEVEL_DESC_STRIDE 8
#define SKILL_LEVEL_DESC_COUNT 51
#define SKILL_LEVEL_SINGLE_MAX 49
extern const uint8_t skill_level_desc[408];
extern const uint8_t strong_lev_desc[24];

/* 秘笈武功表:db 數量,(kf_id,潛能)×數量;book_skill_off[索引]=块內偏移 */
extern const uint8_t book_skill_blob[25];
extern const uint8_t book_skill_off[5];

#endif
