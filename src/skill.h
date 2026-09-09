/* skill.h - 武功查詢(移植原版 skill.s + npc.s query_npc_skill/find_npc_kf) */
#ifndef SKILL_H
#define SKILL_H
#include <stdint.h>
#include "text.h"       /* gfx_scratch UI 複用區 */

extern uint8_t kf_attr[2];      /* get_kf_attr 記錄:[0]=類型 [1]=兵器子類 */
extern uint16_t skill_level;    /* query_skill 輸出(原版同名,2 字節) */
extern uint8_t kf_type, kf_id;  /* 原版同名變量 */
extern uint8_t check_err;       /* check_skill 失敗碼:0=缺基本功 1=兵器不符 */

/* get_skill_desc 輸出:武藝 4 字(8B)/出手 2 字(4B),無結尾符
 * (UI 期間 gfx_scratch 複用,配置見 text.h) */
#define skill_desc_buf  (gfx_scratch + 80)
#define strong_desc_buf (gfx_scratch + 88)

uint8_t find_kf(uint8_t id);        /* → man_kf 字節偏移(4B/條),0xFF=無 */
uint8_t find_npc_kf(uint8_t id);    /* → npc_kf 字節偏移(2B/條),0xFF=無 */
void get_kf_attr(uint8_t id);       /* → kf_attr */
const uint8_t *get_kf_name(uint8_t id);
const uint8_t *get_pf_name(uint8_t id);
uint8_t get_basic_kf(uint8_t t);    /* bit7: 1=npc 側(用 npc 武器) */
uint8_t check_skill(uint8_t id);    /* 1=可用 0=失敗(見 check_err) */
void query_skill(void);             /* man 側:kf_type → skill_level */
void query_npc_skill(void);         /* npc 側 */

/* 等效 aux.s set_obj:0=man 1=npc(obj_ptr + func_buf 跳轉表) */
void set_obj(uint8_t is_npc);
void get_skill_desc(void);          /* → skill_desc_buf / strong_desc_buf */

/* ---- skill.s 成長/清單 UI 段 ---- */
void setup_attr(void);              /* 六維 = 底值 + 基本功/10 */
uint8_t improve_skill(uint16_t gain);   /* 1=升級 */
uint8_t add_kf(void);               /* 習得 kf_id,1=成功 */
uint8_t show_skills(void);          /* 主選單「技能」(menu_fn) */
uint8_t practice_cmd(void);         /* 功能→練功(menu_fn) */
uint8_t dazuo_cmd(void);            /* 功能→內力→打坐(進度迴圈) */

#endif
