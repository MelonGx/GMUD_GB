/* baishi.c - 拜師條件判定,逐師移植原版 serve.s
 *
 * 18 位師父各自的收徒條件(性別/屬性/內力上限/本門武功等級)與台詞。
 * 代碼+訊息在 bank 26(自足模組);台詞經 tplbuf(gfx_scratch+192)
 * 出境 = 原版 move_to_img → img_buf 的等效。
 * get_lvl:未習得 → 0(原版讀 man_kf 空槽,new_game 清零後同值)。
 */
#pragma bank 27
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

/* 各師父沿用原本的條件順序與訊息；僅共用拒收、收徒的語句。 */
#define BS_REJECT_IF(condition, message) \
    if (condition) { \
        bs_msg = message; \
        return 0; \
    }

#define BS_ACCEPT(message) \
    bs_msg = message; \
    return 1

/* 等效 serve.s get_lvl */
static uint8_t get_lvl(uint8_t id)
{
    uint8_t y = find_kf(id);
    return (y == 0xFF) ? 0 : hero.man_kf[y + 1];
}

/* ---- 各師父(回傳 1=收) ---- */

static uint8_t shangjianming(void)          /* 商剑鸣(八卦刀) */
{
    BS_REJECT_IF(hero.man_gender == 1, bs_bagua_gender_msg)
    BS_REJECT_IF(hero.man_maxfp < 500, bs_bagua_maxfp_msg)
    BS_REJECT_IF(get_lvl(HUNYUAN_KF) < 50, bs_bagua_flvl_msg)
    BS_REJECT_IF(get_lvl(BAGUAD_KF) < 50, bs_shang_blvl_msg)
    BS_ACCEPT(bs_shang_suc_msg);
}

static uint8_t shangbaozhen(void)           /* 商宝震 */
{
    BS_REJECT_IF(hero.man_gender == 1, bs_bagua_gender_msg)
    BS_ACCEPT(bs_baozhen_suc_msg);
}

static uint8_t wangweiyang(void)            /* 王维扬 */
{
    BS_REJECT_IF(hero.man_gender == 1, bs_bagua_gender_msg)
    BS_REJECT_IF(hero.man_maxfp < 800, bs_bagua_maxfp_msg)
    BS_REJECT_IF(get_lvl(HUNYUAN_KF) < 100, bs_bagua_flvl_msg)
    BS_REJECT_IF(get_lvl(BAGUAD_KF) < 100, bs_wang_blvl_msg)
    BS_ACCEPT(bs_wang_suc_msg);
}

static uint8_t liqingzhao(void)             /* 李青照(花间派) */
{
    BS_REJECT_IF(hero.man_gender == 0, bs_hua_gender_msg)
    BS_REJECT_IF(hero.man_int < 31 && hero.man_per < 25, bs_li_per_msg)
    BS_REJECT_IF(get_lvl(LITERATE_KF) < 100, bs_li_llvl_msg)
    BS_REJECT_IF(hero.man_maxfp < 1000, bs_li_flvl_msg)
    BS_ACCEPT(bs_li_suc_msg);
}

static uint8_t pingpopo(void)               /* 平婆婆 */
{
    BS_REJECT_IF(hero.man_gender == 0, bs_hua_gender_msg)
    BS_ACCEPT(bs_ping_suc_msg);
}

static uint8_t sangqinghong(void)           /* 桑轻虹 */
{
    BS_REJECT_IF(hero.man_gender == 0, bs_hua_gender_msg)
    BS_REJECT_IF(get_lvl(MEIHUA_KF) < 60, bs_hua_ulvl_msg)
    BS_ACCEPT(bs_hua_suc_msg);
}

static uint8_t tangwanci(void)              /* 唐晚词 */
{
    BS_REJECT_IF(hero.man_gender == 0, bs_hua_gender_msg)
    BS_REJECT_IF(get_lvl(MEIHUA_KF) < 30, bs_hua_ulvl_msg)
    BS_ACCEPT(bs_hua_suc_msg);
}

static uint8_t fangzhanglao(void)           /* 方长老(红莲教) */
{
    BS_ACCEPT(bs_honglian_suc_msg);
}

static uint8_t yuhongru(void)               /* 余鸿儒 */
{
    BS_REJECT_IF(get_lvl(TONGJI_KF) < 100, bs_yu_flvl_msg)
    BS_REJECT_IF(hero.man_str < 30, bs_yu_str_msg)
    BS_ACCEPT(bs_honglian_suc_msg);
}

static uint8_t hezhongyang(void)            /* 和仲阳(尹贺谷) */
{
    BS_REJECT_IF(get_lvl(RENSHU_KF) < 120, bs_naja_flvl_msg)
    BS_REJECT_IF(hero.man_str < 32, bs_naja_str_msg)
    BS_ACCEPT(bs_hezhong_suc_msg);
}

static uint8_t meina(void)                  /* 陈美娜 */
{
    BS_REJECT_IF(get_lvl(WUFA_KF) < 60, bs_huashi_ulvl_msg)
    BS_ACCEPT(bs_naja_suc_msg);
}

static uint8_t tengwangwan(void)            /* 藤王丸 */
{
    BS_ACCEPT(bs_naja_suc_msg);
}

static uint8_t cangyue(void)                /* 苍月道长(太极门) */
{
    BS_ACCEPT(bs_taiji_suc_msg);
}

static uint8_t qingxu(void)                 /* 清虚道长 */
{
    BS_REJECT_IF(hero.man_maxfp < 1500, bs_qingxu_maxfp_msg)
    BS_REJECT_IF(get_lvl(TAIJIG_KF) < 120, bs_qingxu_flvl_msg)
    BS_REJECT_IF(get_lvl(TAIJIQ_KF) < 100, bs_qingxu_ulvl_msg)
    BS_REJECT_IF(hero.man_int < 28, bs_qingxu_int_msg)
    BS_ACCEPT(bs_qingxu_suc_msg);
}

static uint8_t jiaotou(void)                /* 雪山教头 */
{
    BS_REJECT_IF(hero.man_dex < 22, bs_xueshan_dex_msg)
    BS_ACCEPT(bs_jiaotou_suc_msg);
}

static uint8_t bairuide(void)               /* 白瑞德 */
{
    BS_REJECT_IF(get_lvl(XUESHANG_KF) < 100 && hero.man_maxfp < 1200, bs_bairui_maxfp_msg)
    BS_REJECT_IF(hero.man_con < 32, bs_bairui_con_msg)
    BS_ACCEPT(bs_bairui_suc_msg);
}

static uint8_t fengwanjian(void)            /* 封万剑 */
{
    BS_REJECT_IF(hero.man_dex < 23, bs_xueshan_dex_msg)
    BS_REJECT_IF(get_lvl(XUESHANG_KF) < 40, bs_fengwan_flvl_msg)
    BS_ACCEPT(bs_fengwan_suc_msg);
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
    cangyue, cangyue /* 古松道长同台詞同無條件 */, qingxu, jiaotou,
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
