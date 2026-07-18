/* baishi.c - 拜師條件判定,逐師移植原版 serve.s
 *
 * 18 位師父各自的收徒條件(性別/屬性/內力上限/本門武功等級)與台詞。
 * 代碼+訊息在 bank 26(自足模組);台詞經 tplbuf(gfx_scratch+192)
 * 出境 = 原版 move_to_img → img_buf 的等效。
 * get_lvl:未習得 → 0(原版讀 man_kf 空槽,new_game 清零後同值)。
 */
#pragma bank 26
#include <gb/gb.h>
#include <string.h>
#include "baishi.h"
#include "baishi_msgs.h"
#include "save.h"
#include "skill.h"
#include "gamedata.h"
#include "text.h"

#define tplbuf (gfx_scratch + 192)

static const uint8_t *bs_msg;       /* 本次判定的台詞 */

/* 等效 serve.s get_lvl */
static uint8_t get_lvl(uint8_t id)
{
    uint8_t y = find_kf(id);
    return (y == 0xFF) ? 0 : hero.man_kf[y + 1];
}

/* ---- 各師父(回傳 1=收) ---- */

static uint8_t shangjianming(void)          /* 商劍鳴(八卦刀) */
{
    if (hero.man_gender == 1) {
        bs_msg = bs_bagua_gender_msg;
        return 0;
    }
    if (hero.man_maxfp < 500) {
        bs_msg = bs_bagua_maxfp_msg;
        return 0;
    }
    if (get_lvl(HUNYUAN_KF) < 50) {
        bs_msg = bs_bagua_flvl_msg;
        return 0;
    }
    if (get_lvl(BAGUAD_KF) < 50) {
        bs_msg = bs_shang_blvl_msg;
        return 0;
    }
    bs_msg = bs_shang_suc_msg;
    return 1;
}

static uint8_t shangbaozhen(void)           /* 商寶震 */
{
    if (hero.man_gender == 1) {
        bs_msg = bs_bagua_gender_msg;
        return 0;
    }
    bs_msg = bs_baozhen_suc_msg;
    return 1;
}

static uint8_t wangweiyang(void)            /* 王維揚 */
{
    if (hero.man_gender == 1) {
        bs_msg = bs_bagua_gender_msg;
        return 0;
    }
    if (hero.man_maxfp < 800) {
        bs_msg = bs_bagua_maxfp_msg;
        return 0;
    }
    if (get_lvl(HUNYUAN_KF) < 100) {
        bs_msg = bs_bagua_flvl_msg;
        return 0;
    }
    if (get_lvl(BAGUAD_KF) < 100) {
        bs_msg = bs_wang_blvl_msg;
        return 0;
    }
    bs_msg = bs_wang_suc_msg;
    return 1;
}

static uint8_t liqingzhao(void)             /* 李清照(花間派) */
{
    if (hero.man_gender == 0) {
        bs_msg = bs_hua_gender_msg;
        return 0;
    }
    if (hero.man_int < 31 && hero.man_per < 25) {
        bs_msg = bs_li_per_msg;
        return 0;
    }
    if (get_lvl(LITERATE_KF) < 100) {
        bs_msg = bs_li_llvl_msg;
        return 0;
    }
    if (hero.man_maxfp < 1000) {
        bs_msg = bs_li_flvl_msg;
        return 0;
    }
    bs_msg = bs_li_suc_msg;
    return 1;
}

static uint8_t pingpopo(void)               /* 平婆婆 */
{
    if (hero.man_gender == 0) {
        bs_msg = bs_hua_gender_msg;
        return 0;
    }
    bs_msg = bs_ping_suc_msg;
    return 1;
}

static uint8_t sangqinghong(void)           /* 桑青虹 */
{
    if (hero.man_gender == 0) {
        bs_msg = bs_hua_gender_msg;
        return 0;
    }
    if (get_lvl(MEIHUA_KF) < 60) {
        bs_msg = bs_hua_ulvl_msg;
        return 0;
    }
    bs_msg = bs_hua_suc_msg;
    return 1;
}

static uint8_t tangwanci(void)              /* 唐婉詞 */
{
    if (hero.man_gender == 0) {
        bs_msg = bs_hua_gender_msg;
        return 0;
    }
    if (get_lvl(MEIHUA_KF) < 30) {
        bs_msg = bs_hua_ulvl_msg;
        return 0;
    }
    bs_msg = bs_hua_suc_msg;
    return 1;
}

static uint8_t fangzhanglao(void)           /* 方丈老(紅蓮教) */
{
    bs_msg = bs_honglian_suc_msg;
    return 1;
}

static uint8_t yuhongru(void)               /* 余泓儒 */
{
    if (get_lvl(TONGJI_KF) < 100) {
        bs_msg = bs_yu_flvl_msg;
        return 0;
    }
    if (hero.man_str < 30) {
        bs_msg = bs_yu_str_msg;
        return 0;
    }
    bs_msg = bs_honglian_suc_msg;
    return 1;
}

static uint8_t hezhongyang(void)            /* 賀中央(哪吒門) */
{
    if (get_lvl(RENSHU_KF) < 120) {
        bs_msg = bs_naja_flvl_msg;
        return 0;
    }
    if (hero.man_str < 32) {
        bs_msg = bs_naja_str_msg;
        return 0;
    }
    bs_msg = bs_hezhong_suc_msg;
    return 1;
}

static uint8_t meina(void)                  /* 梅十三 */
{
    if (get_lvl(WUFA_KF) < 60) {
        bs_msg = bs_huashi_ulvl_msg;
        return 0;
    }
    bs_msg = bs_naja_suc_msg;
    return 1;
}

static uint8_t tengwangwan(void)            /* 滕王晚 */
{
    bs_msg = bs_naja_suc_msg;
    return 1;
}

static uint8_t cangyue(void)                /* 藏月道長(太極門) */
{
    bs_msg = bs_taiji_suc_msg;
    return 1;
}

static uint8_t qingxu(void)                 /* 清虛道長 */
{
    if (hero.man_maxfp < 1500) {
        bs_msg = bs_qingxu_maxfp_msg;
        return 0;
    }
    if (get_lvl(TAIJIG_KF) < 120) {
        bs_msg = bs_qingxu_flvl_msg;
        return 0;
    }
    if (get_lvl(TAIJIQ_KF) < 100) {
        bs_msg = bs_qingxu_ulvl_msg;
        return 0;
    }
    if (hero.man_int < 28) {
        bs_msg = bs_qingxu_int_msg;
        return 0;
    }
    bs_msg = bs_qingxu_suc_msg;
    return 1;
}

static uint8_t jiaotou(void)                /* 雪山教頭 */
{
    if (hero.man_dex < 22) {
        bs_msg = bs_xueshan_dex_msg;
        return 0;
    }
    bs_msg = bs_jiaotou_suc_msg;
    return 1;
}

static uint8_t bairuide(void)               /* 白瑞德 */
{
    if (get_lvl(XUESHANG_KF) < 100 && hero.man_maxfp < 1200) {
        bs_msg = bs_bairui_maxfp_msg;
        return 0;
    }
    if (hero.man_con < 32) {
        bs_msg = bs_bairui_con_msg;
        return 0;
    }
    bs_msg = bs_bairui_suc_msg;
    return 1;
}

static uint8_t fengwanjian(void)            /* 風萬箭 */
{
    if (hero.man_dex < 23) {
        bs_msg = bs_xueshan_dex_msg;
        return 0;
    }
    if (get_lvl(XUESHANG_KF) < 40) {
        bs_msg = bs_fengwan_flvl_msg;
        return 0;
    }
    bs_msg = bs_fengwan_suc_msg;
    return 1;
}

/* ---- 分派表(序=原版 bashi_npc_tbl) ---- */
typedef uint8_t (*bs_fn)(void);

static const uint8_t bs_npc_tbl[18] = {
    JIANMING_NPC, BAOZHEN_NPC, WEIYANG_NPC, QINGZHAO_NPC,
    PINGPOPO_NPC, QINGHONG_NPC, WANGCI_NPC, FANGZHANGLAO_NPC,
    YUHONGRU_NPC, ZHONGYANG_NPC, MEINA_NPC, TENGWANG_NPC,
    CANGYUE_NPC, GUSONG_NPC, QINGXU_NPC, XJIAOTOU_NPC,
    BAIRUIDE_NPC, WANJIAN_NPC,
};
static const bs_fn bs_fn_tbl[18] = {
    shangjianming, shangbaozhen, wangweiyang, liqingzhao,
    pingpopo, sangqinghong, tangwanci, fangzhanglao,
    yuhongru, hezhongyang, meina, tengwangwan,
    cangyue, cangyue /* gusong 同台詞同無條件 */, qingxu, jiaotou,
    bairuide, fengwanjian,
};

uint8_t baishi(uint8_t id) BANKED
{
    uint8_t i, r, n = 0;
    const uint8_t *p;

    bs_msg = bs_npc_suit_msg;
    r = 0;
    for (i = 0; i < 18; i++) {
        if (bs_npc_tbl[i] == id) {
            r = bs_fn_tbl[i]();
            break;
        }
    }
    /* 等效 move_to_img:台詞(含行終)拷到 tplbuf,雙 0 收尾 */
    p = bs_msg;
    while (n < 180) {
        tplbuf[n] = p[n];
        if (p[n] == 0 && n && p[n - 1] == 0)
            break;
        n++;
    }
    tplbuf[n] = 0;
    tplbuf[n + 1] = 0;
    return r;
}
