/* task.c - 平一指任務系統,逐例程移植原版 task.s + npc_quest.s + qlist.s
 *
 * 對照表:pyh_task/judge_reward/test_player/check_player/get_task/
 *   quest_accurate_index/cal_reward/log10/set_task_temp/set_quest_over/
 *   find_quest_id/query_if_give/work_me/create_ghost/start_job/init_ghost/
 *   copy_status/set_ghost_kf/pyh_task_bonus/voluntary_work/show_bonus/
 *   inssort/get_all_npc(qlist.s)/get_all_goods(aux.s)/npc_quest → 同名
 *
 * get_all_goods 源檔隨 aux.s 丟失,已自原版二進制(obj.bin pc 7E5B)
 * 反彙編還原:76 項物品經驗表逐條複製,無過濾,count=76。
 *
 * 原版變數疊在 game_buf 共享區(task.s/npc_quest.s/pyh_quest.s 同址複用,
 * 例如義工工錢 varbuf+2 覆蓋 quest_delay);task_gbuf 依原版偏移佈局,
 * 保留跨模組別名行為。quest_temp 在存檔區外 → 斷電即失(忠實原版;
 * 原機 RAM 常駐可跨次執行,GBC WRAM 斷電清零屬硬體差異)。
 *
 * 平一指(查殺 QUEST_KILL):query_kill 好人趕走、壞人接查殺任務
 * (query_check,與尋人共用 get_all_npc);戰勝標記見 fight_win_task。
 *
 * 代碼在 bank 28。
 */
#pragma bank 28
#include <gb/gb.h>
#include <string.h>
#include "game.h"
#include "save.h"
#include "text.h"
#include "task.h"
#include "task_msgs.h"
#include "ui.h"
#include "fb.h"
#include "blit.h"
#include "goods.h"
#include "skill.h"      /* kf_id($g/$k) */
#include "gamedata.h"   /* kf_name_tbl/pf_name_tbl */
#include "res.h"
#include "textbank_res.h"

/* NPC 編號(h/id.h define 序) */
#define BUKUAI_NPC     3
#define CUNZHANG_NPC   6
#define FUREN_NPC     10
#define OLDLADY_NPC   25
#define PINGYIZHI_NPC 26
#define TEACHER_NPC   31
#define KILLER_NPC   125
#define NPC_NUM      126
#define NONE_NPC       0

#define MAXREWARD       200
#define GHOST_NUM         8
#define GHOST_AGE        34
#define GHOST_BONUS      80
#define REWARD_MAXDELAY 1200        /* 20 分鐘 */
#define DONE_LIMIT       300
#define WAIT_LIMIT      1200
#define TEMP_SIZE        12
#define QUEST_ID_OFF      5

/* 任務狀態置 SRAM(main 常開;WRAM 讓給棧 2026-07-10)。附帶效果:
 * quest_temp 跨斷電保留=更貼近原機 RAM 常駐語義(先前 WRAM 斷電清零
 * 屬硬體差異);新遊戲時由 game_boot 清零(=原版程式載入 BSS 清零)。 */
__at(0xA320) uint8_t quest_temp[72];
__at(0xA368) uint8_t task_buf[8];
__at(0xA388) uint8_t home_buf;

/* game_buf 共享區(原版偏移):quest_numbers@0 quest_index@1 quest_exp@2-5
 * quest_delay@6-9 quest_reward@10-11 ghost_level@12 task_temp1@16-17
 * task_temp2@18-19 task_temp3@20-21;npc_quest.s 的 msg_number@0
 * msg_addr@1-2 give_id@3 give_count@4 receive_id@5;pyh_quest.s 的
 * varbuf+2@4(工錢槽,覆蓋 quest_exp 高半+quest_delay=原版行為).
 * 原未用的 13-15/22-23 作為通緝犯斷電狀態，也會被 save.c
 * 的每 slot 任務快照保留。 */
__at(0xA370) uint8_t task_gbuf[24];
#define quest_numbers (task_gbuf[0])
#define quest_index   (task_gbuf[1])
#define quest_exp     (task_gbuf + 2)
#define quest_delay   (task_gbuf + 6)
#define quest_reward  (task_gbuf + 10)
#define ghost_level   (task_gbuf[12])
#define task_temp1    (task_gbuf + 16)

#define GHOST_SAVE_MAGIC 0xA7
#define ghost_save_mark   (task_gbuf[13])
#define ghost_save_map    (task_gbuf[14])
#define ghost_save_x      (task_gbuf[15])
#define ghost_save_y      (task_gbuf[22])
#define ghost_save_gender (task_gbuf[23])

__at(0xA389) static uint8_t kmap_name[17];  /* 兇手圖名(原版存 G_img_buf) */
static uint8_t *km_slot;            /* kill_npc_msg type8 二次間接槽 */

#define tplbuf (gfx_scratch + 192)

/* 任務列表暫存:fb 狀態列區(112-143 行,640B)。行走/對話期間該區
 * 不被 flush(僅 fight 經 flush_status_row 使用),藉作大暫存;
 * 內容不跨對話存活。列表:count,{exp 4B,id 1B}×,最大 1+118*5=591B */
/* Full-screen mode makes framebuffer rows 112..143 visible.  Keep the
 * 1 + 118 * 5 byte quest sort table in free SRAM bank 0 instead. */
__at(0xB340) static uint8_t quest_list[591];

static void putw(uint8_t *p, const void *v)
{
    uint16_t a = (uint16_t)v;
    p[0] = (uint8_t)a;
    p[1] = (uint8_t)(a >> 8);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* 等效 show_talk_msg 前置的 format_string(訊息均在本 bank,直讀安全) */
static void fmt_show(const uint8_t *msg)
{
    format_string(msg);
    show_talk_msg();
}

/* ================= $ 轉義主體(string.s $t/$q/$g/$k/$p) =================
 * format_string(HOME) 轉呼至此,省 HOME 空間;輸出到 OutBuf(WRAM) */

uint8_t perform_id;                 /* 原版同名全域($p;perform 模組寫) */

/* string.s type_char(杀寻运送去)/home_char(扫地辟柴挑水) */
static const uint8_t type_char[10] = {
    0xC9, 0xB1, 0xD1, 0xB0, 0xD4, 0xCB, 0xCB, 0xCD, 0xC8, 0xA5,
};
static const uint8_t home_char[12] = {
    0xC9, 0xA8, 0xB5, 0xD8, 0xB1, 0xD9, 0xB2, 0xF1, 0xCC, 0xF4, 0xCB, 0xAE,
};

/* 等效 npc.s get_npc_name:NPC_NAME 記錄前 9 字節(res 自帶切 bank) */
static const uint8_t *get_npc_name(uint8_t id)
{
    static uint8_t nb[10];

    res_read(&textbank_res, text_get_ptr(0, id), nb, 9);   /* TC_NPC_NAME */
    nb[9] = 0;
    return nb;
}

/* 名字類拷貝:到 0/0xFF/' ' 止(等效 text.c put_str) */
static uint8_t *esc_put(uint8_t *out, const uint8_t *s)
{
    uint8_t ch;

    while ((ch = *s++) != 0 && ch != 0xFF && ch != ' ')
        *out++ = ch;
    return out;
}

uint8_t *task_escape(uint8_t c, uint8_t *out) BANKED
{
    const uint8_t *p;

    switch (c) {
    case 't':                               /* 任務類型字 */
        p = type_char + ((uint8_t)(task_buf[0] << 1));
        *out++ = p[0];
        *out++ = p[1];
        break;
    case 'q':                               /* 任務對象名 */
        if (task_buf[0] == QUEST_NPC || task_buf[0] == QUEST_KILL) {
            out = esc_put(out, get_npc_name(task_buf[1]));
        } else if (task_buf[0] == QUEST_HOME) {
            p = home_char + ((uint8_t)(task_buf[1] << 2));
            *out++ = p[0];
            *out++ = p[1];
            *out++ = p[2];
            *out++ = p[3];
        } else {                            /* 尋物等 */
            out = esc_put(out, get_goods_name(task_buf[1] & 0x7F));
        }
        break;
    case 'g':                               /* 物品名(goods_id=kf_id) */
        out = esc_put(out, get_goods_name(kf_id & 0x7F));
        break;
    case 'k':                               /* 武功名 */
        out = esc_put(out, gd_find_name(kf_name_tbl, kf_id));
        break;
    case 'p':                               /* 絕招名 */
        out = esc_put(out, gd_find_name(pf_name_tbl, perform_id));
        break;
    default:
        break;
    }
    return out;
}

/* ---- 等效 tools.s random_map:為殺手挑一張圖一個空位 ----
 * 臨時 change_map0 到候選圖讀格子,選畢複製圖名並還原當前圖;
 * 位置要求:本格全空(<BLAK),四鄰不是門;一圖試 8 次,不成換圖 */
static uint8_t rm_cell_class(uint8_t x, uint8_t y)
{
    return (uint8_t)(get_object0((uint16_t)y * G_Map_Width + x) >> 10);
}

static void random_map(uint8_t *name_out)
{
    uint8_t back_map = G_Curr_Map;
    uint8_t try_time;

    for (;;) {
        G_Curr_Map = (uint8_t)random_it((uint16_t)(G_Total_Map - 1)) + 1;
        G_Killer_Map = G_Curr_Map;
        change_map0();
        for (try_time = 8; try_time; try_time--) {
            G_Killer_X = (uint8_t)random_it((uint16_t)(G_Map_Width - 2)) + 1;
            G_Killer_Y = (uint8_t)random_it((uint16_t)(G_Map_Height - 2)) + 1;
            if (rm_cell_class(G_Killer_X, G_Killer_Y) >= 1)
                continue;
            if (rm_cell_class(G_Killer_X - 1, G_Killer_Y) == 2)
                continue;
            if (rm_cell_class(G_Killer_X + 1, G_Killer_Y) == 2)
                continue;
            if (rm_cell_class(G_Killer_X, G_Killer_Y - 1) == 2)
                continue;
            if (rm_cell_class(G_Killer_X, G_Killer_Y + 1) == 2)
                continue;
            memcpy(name_out, G_MapName, 16);
            name_out[16] = 0;
            G_Curr_Map = back_map;
            change_map0();
            return;
        }
    }
}

/* ---- find_quest_id / set_quest_over ---- */
static uint8_t find_quest_id(uint8_t type)
{
    return quest_temp[(uint8_t)(type * TEMP_SIZE) + QUEST_ID_OFF];
}

static void set_quest_over(uint8_t type)
{
    quest_temp[(uint8_t)(type * TEMP_SIZE) + QUEST_ID_OFF] = QUEST_OVER;
}

/* ================= 列表建立(qlist.s / aux.s) ================= */

static const uint8_t skip_npc_tbl[8] = {
    NONE_NPC, OLDLADY_NPC, FUREN_NPC, CUNZHANG_NPC,
    PINGYIZHI_NPC, BUKUAI_NPC, TEACHER_NPC, KILLER_NPC,
};

static uint8_t npc_alive8(uint8_t id)
{
    return npc_stat_buf[id >> 3] & (uint8_t)(0x80 >> (id & 7));
}

/* 等效 qlist.s get_all_npc:活著且不在跳過表的 NPC,{exp,id} 列表 */
static void get_all_npc(void)
{
    uint16_t base = res_read16(&textbank_res, TC_NPC_EXP << 1);
    uint8_t *dst = quest_list + 1;
    uint8_t id, k, n = 0;

    for (id = 0; id < NPC_NUM; id++) {
        for (k = 0; k < 8; k++) {
            if (id == skip_npc_tbl[k])
                break;
        }
        if (k < 8)
            continue;
        if (!npc_alive8(id))
            continue;
        res_read(&textbank_res, base + ((uint16_t)id << 2), dst, 4);
        dst[4] = id;
        dst += 5;
        n++;
    }
    quest_list[0] = n;
}

/* aux.s get_all_goods(obj.bin 7EA8 還原):物品 1..76 的任務經驗表 */
static const uint32_t goods_exp_tbl[76] = {
    10, 10, 10, 10, 10, 10, 10, 10, 10, 140000,
    10, 10000, 10, 10, 110000, 12000, 10, 10, 10, 10000,
    10, 10, 75000, 180000, 300000, 65000, 65000, 140000, 45000, 130000,
    85000, 30000, 130000, 15000, 15000, 55000, 130000, 25000, 160000, 200,
    10, 10, 200, 10, 5000, 10, 5000, 10, 10, 3000,
    40000, 300000, 160000, 20000, 180000, 160000, 85000, 85000, 160000, 30000,
    1000, 180000, 180000, 55000, 65000, 70000, 90000, 50000, 30000, 60000,
    70000, 100, 10, 200, 12000, 10,
};

static void get_all_goods(void)
{
    uint8_t *dst = quest_list + 1;
    uint8_t id;

    for (id = 1; id <= 76; id++) {
        wr32(dst, goods_exp_tbl[id - 1]);
        dst[4] = id;
        dst += 5;
    }
    quest_list[0] = 76;
}

/* 等效 task.s inssort(x=5,y=4):5B 條目按前 4B(小端)穩定升序 */
static void inssort5(void)
{
    uint8_t n = quest_list[0];
    uint8_t i, j, k;
    uint8_t *a, *b;
    uint8_t t;

    for (i = 1; i < n; i++) {
        for (j = i; j; j--) {
            a = quest_list + 1 + (uint16_t)j * 5;
            b = a - 5;
            if (rd32(a) >= rd32(b))
                break;
            for (k = 0; k < 5; k++) {
                t = a[k];
                a[k] = b[k];
                b[k] = t;
            }
        }
    }
}

/* ================= 發任務主流程(task.s) ================= */

/* 等效 quest_accurate_index:量力而行,index=經驗夠格的條目數
 * 回傳 0=沒有可選對象(quest_numbers==0) */
static uint8_t quest_accurate_index(void)
{
    const uint8_t *p = quest_list + 1;

    quest_numbers = quest_list[0];
    quest_index = 0;
    if (!quest_numbers)
        return 0;
    for (;;) {
        quest_index++;
        if (quest_index == quest_numbers) {
            quest_index--;
            break;
        }
        if (hero.man_exp < rd32(p)) {
            quest_index--;
            break;
        }
        p += 5;
    }
    return 1;
}

/* 等效 get_quest_data:選中條目 → quest_exp(4B) + quest_id */
static void get_quest_data(void)
{
    const uint8_t *e = quest_list + 1 + (uint16_t)quest_index * 5;

    memcpy(quest_exp, e, 4);
    task_buf[1] = e[4];
}

/* 等效 log10(以 10 為底的級數) */
static uint8_t log10_8(uint8_t v)
{
    uint8_t y = 0;

    while (v /= 10)
        y++;
    return y;
}

/* 等效 cal_reward */
static void cal_reward(void)
{
    uint16_t r;
    uint32_t s;
    uint16_t rnd_kar;
    uint8_t sum;

    /* reward = MAXREWARD*(1+index)/numbers */
    r = (uint16_t)((uint16_t)MAXREWARD * (uint8_t)(quest_index + 1)
                   / quest_numbers);
    /* *(1+log10(exp/10000)),取低字節(原版 8 位 log10) */
    r = r * (uint8_t)(log10_8((uint8_t)(hero.man_exp / 10000UL)) + 1);
    /* *exp/(exp+dx)*dx/(exp+dx) */
    s = hero.man_exp + rd32(quest_exp);
    r = (uint16_t)((uint32_t)hero.man_exp * r / s);
    r = (uint16_t)(rd32(quest_exp) * r / s);
    /* += random(kar)+random(int)(8 位和,原版 adc) */
    rnd_kar = random_it(hero.man_kar);
    sum = (uint8_t)((uint8_t)rnd_kar + (uint8_t)random_it(hero.man_int));
    r += sum;
    if (r >= MAXREWARD)
        r = MAXREWARD + rnd_kar;
    wr16(quest_reward, r);
}

/* 等效 set_task_temp:{index,exp[4],id,delay[4],reward[2]} 寫入槽 */
static void set_task_temp(void)
{
    uint8_t *slot = quest_temp + (uint8_t)(task_buf[0] * TEMP_SIZE);

    slot[0] = quest_index;
    memcpy(slot + 1, quest_exp, 4);
    slot[5] = task_buf[1];
    memcpy(slot + 6, quest_delay, 4);
    memcpy(slot + 10, quest_reward, 2);
}

/* 等效 judge_reward:三類任務有一類已完成未領賞 → 提示先領賞 */
static uint8_t judge_reward(void)
{
    uint8_t t;

    for (t = 0; t <= QUEST_KILL; t++) {
        if (find_quest_id(t) == HAS_REWARD) {
            fmt_show(tm_quest_reward_msg);
            return 1;
        }
    }
    return 0;
}

/* 等效 query_if_give:尋物任務交納確認 */
static void query_if_give(void)
{
    uint8_t x;

    format_string(tm_if_give_msg);
    if (!confirm_box(6, 0, 6 + 12 * 12, 26, NULL))
        return;
    x = find_goods(task_buf[1]);
    hero.man_goods[x + 1]--;
    set_quest_over(task_buf[0]);
    task_buf[1] = QUEST_OVER;
}

/* 等效 test_player:讀當前任務 id;尋物且身上有 → 問交納 */
static void test_player(void)
{
    uint8_t id = find_quest_id(task_buf[0]);

    task_buf[1] = id;
    if (id == 0)
        return;
    if (task_buf[0] == QUEST_NPC || task_buf[0] == QUEST_KILL)
        return;
    if (id == QUEST_OVER)
        return;                             /* find_goods(0xFF) 必不中 */
    if (find_goods(id) == 0xFF)
        return;
    query_if_give();
}

static const uint8_t *const exist_msg_tbl[3] = {
    tm_npc_exist_msg, tm_goods_exist_msg, tm_kill_exist_msg,
};
static const uint8_t *const new_msg_tbl[3] = {
    tm_npc_new_msg, tm_goods_new_msg, tm_kill_new_msg,
};

/* 等效 get_task:建列表→量力抽選→算賞→寫槽→報新任務 */
static void get_task(void)
{
    if (task_buf[0] == QUEST_GOODS)
        get_all_goods();
    else
        get_all_npc();                      /* 尋人/殺人共用 NPC 表 */
    inssort5();

    if (!quest_accurate_index()) {
        fmt_show(tm_no_npc_msg);
        return;
    }
    quest_index = (uint8_t)random_it(quest_index);
    get_quest_data();
    /* cal_delay:原版 if 0 整段停用,quest_delay 維持殘值(共享區別名) */
    cal_reward();
    set_task_temp();
    fmt_show(new_msg_tbl[task_buf[0]]);     /* $q=對象名 */
}

/* 等效 check_player */
static void check_player(void)
{
    uint8_t *slot;

    if (task_buf[1] == 0) {
        get_task();
        return;
    }
    if (task_buf[1] == QUEST_OVER) {        /* 已完成 → 記 HAS_REWARD */
        slot = quest_temp + (uint8_t)(task_buf[0] * TEMP_SIZE);
        slot[QUEST_ID_OFF] = HAS_REWARD;
        wr32(slot + 6, hero.mud_age);       /* 完成時間 */
        fmt_show(tm_quest_done_msg);
        return;
    }
    fmt_show(exist_msg_tbl[task_buf[0]]);   /* 任務進行中的催促 */
}

/* 等效 query_check(query_npc/query_goods/query_kill 公共段) */
static void query_check(uint8_t type)
{
    task_buf[0] = type;
    if (judge_reward())
        return;
    test_player();
    check_player();
}

/* ================= 捕快:殺通緝犯(ghost) ================= */

/* 通緝犯的地圖位置原本只在 WRAM，斷電後任務槽還在卻無人可殺。
 * 使用 task_gbuf 未用位元組做每 slot 持久狀態；magic 可區分新格式
 * 與舊存檔的隨機殘值。 */
static uint8_t ghost_state_saved(void)
{
    return ghost_save_mark == GHOST_SAVE_MAGIC;
}

static void ghost_state_clear(void)
{
    G_Task_Flag &= 0x7F;
    G_Killer_Map = 0;
    G_Killer_X = 0;
    G_Killer_Y = 0;
    ghost_gender_bak = 0;
    ghost_save_mark = 0;
    ghost_save_map = 0;
    ghost_save_x = 0;
    ghost_save_y = 0;
    ghost_save_gender = 0;
}

static void ghost_state_store(void)
{
    ghost_save_map = G_Killer_Map;
    ghost_save_x = G_Killer_X;
    ghost_save_y = G_Killer_Y;
    ghost_save_gender = ghost_gender_bak ? 1 : 0;
    ghost_save_mark = GHOST_SAVE_MAGIC;     /* 最後提交，避免半寫狀態 */
    G_Task_Flag |= 0x80;
}

/* 讀檔/新遊戲共用：只在任務槽與持久狀態同時合法時復活。
 * 舊存檔可能有 id=125 卻沒有 magic，先保持非活躍，下次與捕快
 * 說話時由 work_me 重新派發位置。 */
void ghost_task_restore(void) BANKED
{
    uint8_t *slot = quest_temp + QUEST_GHOST * TEMP_SIZE;

    G_Task_Flag &= 0x7F;
    G_Killer_Map = G_Killer_X = G_Killer_Y = 0;
    ghost_gender_bak = 0;
    if (ghost_state_saved() && slot[QUEST_ID_OFF] == KILLER_NPC
        && ghost_save_map != 0 && ghost_save_x != 0 && ghost_save_y != 0
        && ghost_save_gender <= 1) {
        G_Killer_Map = ghost_save_map;
        G_Killer_X = ghost_save_x;
        G_Killer_Y = ghost_save_y;
        ghost_gender_bak = ghost_save_gender;
        G_Task_Flag |= 0x80;
        return;
    }
    if (ghost_state_saved())
        ghost_state_clear();                /* 過期/破損 marker 不復活 */
}

/* 等效 create_ghost:隨機姓名/性別/賞金,寫入 GHOST 槽 */
static void create_ghost(void)
{
    uint16_t r;
    uint8_t m;

    ghost_state_clear();                    /* 新任務不沿用舊位置 */
    quest_index = ghost_level;
    task_buf[0] = QUEST_GHOST;
    task_buf[1] = KILLER_NPC;

    m = (uint8_t)random_it(GHOST_NUM);
    quest_exp[0] = tm_name_xing_tbl[m << 1];
    quest_exp[1] = tm_name_xing_tbl[(m << 1) + 1];
    ghost_gender_bak = 0;
    m = (uint8_t)random_it(GHOST_NUM);
    if (m >= 4)
        ghost_gender_bak = 1;
    quest_exp[2] = tm_name_ming_tbl[m << 1];
    quest_exp[3] = tm_name_ming_tbl[(m << 1) + 1];

    r = (uint16_t)((random_it(GHOST_BONUS) + GHOST_BONUS) * ghost_level);
    wr16(quest_reward, r);
    wr32(quest_delay, hero.mud_age);
    set_task_temp();
}

/* 等效 start_job:置殺手上圖 + 報「聞有惡人…」 */
static void start_job(void)
{
    uint8_t *slot = quest_temp + QUEST_GHOST * TEMP_SIZE;

    random_map(kmap_name);
    ghost_state_store();

    /* quest_exp is shared scratch and may have been overwritten by another
     * task before a legacy quest is reissued.  The quest slot is canonical. */
    memcpy(quest_exp, slot + 1, 4);
    quest_delay[0] = 0;                     /* 原版 lm2 quest_exp+4,#0: */
    quest_delay[1] = 0;                     /* 名字字串結尾(別名覆蓋) */
    km_slot = kmap_name;
    memcpy(tplbuf, tm_kill_npc_msg, TM_KILL_NPC_MSG_LEN);
    putw(tplbuf + TM_KILL_NPC_MSG_S0, quest_exp);
    putw(tplbuf + TM_KILL_NPC_MSG_S1, &km_slot);
    fmt_show(tplbuf);
}

/* 等效 work_me:0=可派新任務(隨後 start_job) 1=擋回 */
static uint8_t work_me(void)
{
    uint8_t *slot = quest_temp + QUEST_GHOST * TEMP_SIZE;
    uint8_t id = slot[QUEST_ID_OFF];
    uint32_t t = rd32(slot + 6);

    if (id == 0) {                          /* 第一次 */
        ghost_level = 1;
        create_ghost();
        return 0;
    }
    if (id == QUEST_OVER) {                 /* 已殺 → 冷卻 300 秒 */
        if (hero.mud_age > t + DONE_LIMIT) {
            ghost_level = (slot[0] >= 10) ? 2 : (uint8_t)(slot[0] + 1);
            create_ghost();
            return 0;
        }
        fmt_show(tm_time_limit_msg);
        return 1;
    }
    /* 進行中 */
    if (!ghost_state_saved()) {             /* 舊存檔移轉：保留人名/賞金 */
        ghost_state_clear();
        wr32(slot + 6, hero.mud_age);        /* 重新給完整 20 分鐘 */
        return 0;                            /* start_job 補一個新位置 */
    }
    if (hero.mud_age >= t + WAIT_LIMIT) {   /* 超時失敗 → 降一級重派 */
        ghost_level = (slot[0] < 2) ? 1 : (uint8_t)(slot[0] - 1);
        create_ghost();
        return 0;
    }
    memcpy(task_temp1, slot + 1, 4);        /* 名字 → 催促訊息 */
    task_temp1[4] = 0;
    task_temp1[5] = 0;
    memcpy(tplbuf, tm_gnot_done_msg, TM_GNOT_DONE_MSG_LEN);
    putw(tplbuf + TM_GNOT_DONE_MSG_S0, task_temp1);
    fmt_show(tplbuf);
    return 1;
}

static void query_ghost(void)
{
    if (!(hero.man_daode & 0x80)) {         /* 壞人 → 拿下! */
        fmt_show(tm_bad_man_msg);
        return;
    }
    task_buf[0] = QUEST_GHOST;
    if (!work_me())
        start_job();
}

/* ---- 兇手 NPC 生成(init_ghost/copy_status/set_ghost_kf) ---- */

static const uint8_t ghost_index_tbl[10] = {
    80, 85, 90, 95, 100, 105, 110, 115, 120, 125,
};

/* 門派招式表(task.s menpai_tbl;第 0 列=non_pai,原版與八卦門同址) */
static const uint8_t menpai_tbl[7][6] = {
    { 12, 11, 15, 14, 11, 11 },     /* non_pai(=八卦) */
    { 12, 11, 15, 14, 11, 11 },     /* 八卦门:掌/刀/游龙/混元/刀,钢刀 */
    { 19, 17, 16, 20, 17, 22 },     /* 花间派 */
    { 24, 23, 21, 25, 23, 21 },     /* 红莲教 */
    { 27, 29, 28, 26, 29, 11 },     /* 挪呀谷 */
    { 31, 30, 33, 32, 30, 35 },     /* 太极门 */
    { 39, 38, 35, 36, 38, 35 },     /* 雪山剑派 */
};
#define BASIC_KF_NUM 9

static void set_ghost_kf(uint8_t kf_lvl)
{
    const uint8_t *pai = menpai_tbl[npc.npc_pai];
    uint8_t i, x = 0;

    for (i = 0; i < 5; i++)
        npc.npc_usekf[i] = pai[i] | 0x80;
    npc.npc_goods[0] = pai[5] | 0x80;       /* 佩武器 */

    for (i = 0; i < BASIC_KF_NUM; i++) {
        npc_kf[x++] = i;
        npc_kf[x++] = kf_lvl;
    }
    for (i = 0; i < 4; i++) {
        npc_kf[x++] = pai[i];
        npc_kf[x++] = kf_lvl;
    }
    npc.npc_kfnum = BASIC_KF_NUM + 4;
}

/* 等效 copy_status:以玩家屬性按等級係數生成兇手 → npc */
static void copy_status(void)
{
    uint8_t *slot = quest_temp + QUEST_GHOST * TEMP_SIZE;
    uint8_t pct = ghost_index_tbl[(uint8_t)(slot[0] - 1)];
    uint16_t v;
    uint8_t i, hi;

    /* 戰鬥資料塊照抄玩家(attack..effhp),六維再用底值覆蓋 */
    npc.npc_attack = hero.man_attack;
    npc.npc_defense = hero.man_defense;
    npc.npc_damage = hero.man_damage;
    npc.npc_armor = hero.man_armor;
    npc.npc_exp = hero.man_exp;
    npc.npc_force = hero.man_force;
    npc.npc_str = hero.attr_str;
    npc.npc_dex = hero.attr_dex;
    npc.npc_int = hero.attr_int;
    npc.npc_con = hero.attr_con;
    npc.npc_per = hero.attr_per;
    npc.npc_kar = hero.attr_kar;

    memcpy(npc.npc_name, slot + 1, 4);      /* 姓+名(2 字) */
    npc.npc_name[4] = 0;
    npc.npc_name[5] = 0;
    npc.npc_busy = 0;   /* 原版 ghost_flag 未初始化(殘值);取定值 0 */

    npc.npc_pai = (uint8_t)random_it(6) + 1;
    npc.npc_age = GHOST_AGE;

    npc.npc_exp = hero.man_exp * (uint32_t)pct / 100;
    v = (uint16_t)((uint32_t)hero.man_maxhp * pct / 100);
    npc.npc_maxhp = v;
    npc.npc_effhp = v;
    npc.npc_hp = v;
    v = (uint16_t)((uint32_t)hero.man_maxfp * pct / 100);
    npc.npc_maxfp = v;
    npc.npc_fp = v;

    hi = 0;                                 /* 玩家最高武功等級 */
    for (i = 0; i < hero.man_kfnum; i++) {
        uint8_t lv = hero.man_kf[(uint8_t)(i << 2) + 1];
        if (lv > hi)
            hi = lv;
    }
    set_ghost_kf((uint8_t)((uint16_t)pct * hi / 100));

    memset(npc.npc_goods + 1, 0, 4);        /* equip..goods[4] 清零 */
    npc.npc_money = 10;
    npc_desc[0] = 0;
    npc.npc_daode = 0;
    npc.npc_gender = ghost_gender_bak;
    npc.npc_force = npc.npc_maxfp / 40;
}

uint8_t init_ghost(void) BANKED
{
    if (!(G_Task_Flag & 0x80))
        return 0;
    if (located_id != KILLER_NPC)
        return 0;
    copy_status();
    return 1;
}

/* 等效 npc.s man_win 的 QUEST_GHOST 段 + task.s show_bonus */
static void ghost_win(void)
{
    uint8_t *slot = quest_temp + QUEST_GHOST * TEMP_SIZE;
    uint16_t r = rd16(slot + 10);
    uint32_t v;

    set_quest_over(QUEST_GHOST);
    /* 獎勵經驗/潛能僅 BSC 檔 x8(2026-07-17 用戶定版);
     * 16 位封頂,顯示值=實發值 */
    v = (uint32_t)r << reward_shift();
    if (v > 0xFFFFUL)
        v = 0xFFFFUL;
    wr16(task_buf + 2, (uint16_t)v);
    v = (uint32_t)(r >> 2) << reward_shift();       /* divid2 #4 */
    if (v > 0xFFFFUL)
        v = 0xFFFFUL;
    wr16(task_buf + 4, (uint16_t)v);

    hero.man_exp += rd16(task_buf + 2);
    v = (uint32_t)hero.man_pot + rd16(task_buf + 4);
    hero.man_pot = (v > 0xFFFFUL) ? 0xFFFF : (uint16_t)v;   /* 飽和 */

    memcpy(tplbuf, tm_bonus_msg, TM_BONUS_MSG_LEN);
    putw(tplbuf + TM_BONUS_MSG_S0, task_buf + 2);
    putw(tplbuf + TM_BONUS_MSG_S1, task_buf + 4);
    format_string(tplbuf);
    message_box(12, 10, 140, 60);
    scroll_to_lcd();
    fb_flush();

    ghost_state_clear();
}

/* ================= 帳房先生:領賞(pyh_task_bonus) ================= */

static void task_bonus(void)
{
    uint8_t t, found = 0;
    uint16_t r = 0;
    uint32_t v;
    uint8_t *slot;

    for (t = 0; t <= QUEST_KILL; t++) {
        slot = quest_temp + (uint8_t)(t * TEMP_SIZE);
        if (slot[QUEST_ID_OFF] == HAS_REWARD) {
            found = 1;
            slot[QUEST_ID_OFF] = 0;         /* 槽清空,可接新任務 */
        }
        /* 原版對「非完成槽」也走時限加賞(殘值時間),忠實保留 */
        if (hero.mud_age <= rd32(slot + 6) + REWARD_MAXDELAY)
            r += rd16(slot + 10);
    }
    if (!found) {
        fmt_show(tm_no_bonus_msg);
        return;
    }
    r *= 3;                                 /* 常時領賞加成 */
    /* 獎勵僅 BSC 檔 x8(2026-07-17 用戶定版),
     * 16 位封頂;潛能入帳飽和 */
    if (random_it(100) >= 70) {             /* 30%:潛能(半額) */
        r >>= 1;
        v = (uint32_t)r << reward_shift();
        if (v > 0xFFFFUL)
            v = 0xFFFFUL;
        wr16(task_buf + 4, (uint16_t)v);
        v += hero.man_pot;
        hero.man_pot = (v > 0xFFFFUL) ? 0xFFFF : (uint16_t)v;
        memcpy(tplbuf, tm_get_pot_msg, TM_GET_POT_MSG_LEN);
        putw(tplbuf + TM_GET_POT_MSG_S0, task_buf + 4);
    } else {                                /* 70%:實戰經驗 */
        v = (uint32_t)r << reward_shift();
        if (v > 0xFFFFUL)
            v = 0xFFFFUL;
        wr16(task_buf + 2, (uint16_t)v);
        hero.man_exp += (uint16_t)v;
        memcpy(tplbuf, tm_get_exp_msg, TM_GET_EXP_MSG_LEN);
        putw(tplbuf + TM_GET_EXP_MSG_S0, task_buf + 2);
    }
    fmt_show(tplbuf);
}

/* ================= 官方秘技判定(task.h 說明) ================= */

uint8_t yobdc_mode(void)
{
    static const uint8_t nm[6] = "yobdc";   /* 含結尾 0,防前綴誤中 */
    return memcmp(hero.man_name, nm, 6) == 0;
}

/* ================= 老太太:義工(voluntary_work) ================= */

static void voluntary_work(void)
{
    uint8_t y;

    /* 原版經驗上限 5000;yobdc 秘技無限做(2026-07-18 用戶定版) */
    if (!yobdc_mode() && hero.man_exp >= 5000UL) {
        fmt_show(tm_exp_high_msg);
        return;
    }
    if (home_buf) {
        fmt_show(tm_have_task_msg);
        return;
    }
    y = (uint8_t)random_it(3) & 3;
    home_buf = y + 1;                       /* 1 掃地 2 挑水 3 劈柴 */
    memcpy(task_temp1, tm_random_task + ((uint8_t)(y << 2)), 4);
    task_temp1[4] = 0;
    task_temp1[5] = 0;
    memcpy(tplbuf, tm_voluntary_task_msg, TM_VOLUNTARY_TASK_MSG_LEN);
    putw(tplbuf + TM_VOLUNTARY_TASK_MSG_S0, task_temp1);
    fmt_show(tplbuf);
}

/* ================= 平一指:查殺(task.s query_kill) ================= */

/* 等效 task.s query_kill:好人趕走;壞人接查殺任務(=query_check)。
 * QUEST_KILL 與 QUEST_NPC 共用 get_all_npc 列表(quest_tbl[KILL]=
 * quest_npc_tbl);訊息用 kill_new/kill_exist(new/exist_msg_tbl[2]);
 * 目標 NPC 名經 $q → get_npc_name(task_buf[1])。 */
static void query_kill(void)
{
    if (hero.man_daode & 0x80) {            /* 好人 → 趕走(bmi good_man) */
        fmt_show(tm_good_man_msg);
        return;
    }
    query_check(QUEST_KILL);
}

/* ================= pyh_task 分派(SWITCH 命中恆回 1) ================= */

static uint8_t pyh_task(void)
{
    switch (located_id) {
    case CUNZHANG_NPC:
        query_check(QUEST_NPC);
        return 1;
    case FUREN_NPC:
        query_check(QUEST_GOODS);
        return 1;
    case PINGYIZHI_NPC:
        query_kill();
        return 1;
    case BUKUAI_NPC:
        query_ghost();
        return 1;
    case TEACHER_NPC:
        task_bonus();
        return 1;
    case OLDLADY_NPC:
        voluntary_work();
        return 1;
    default:
        return 0;
    }
}

/* ================= npc_quest(npc_quest.s,資料驅動) ================= */

#define nq_msg_addr   (task_gbuf + 1)
#define nq_give_id    (task_gbuf[3])
#define nq_give_count (task_gbuf[4])
#define nq_receive_id (task_gbuf[5])

/* 訊息窗:讀 456B(256B 尋址窗 + 200B 拷貝保底,同原版 data_read_buf 語義) */
#define nq_window quest_list
#define NQ_WINDOW_LEN 456

/* 等效 find_id:npc_id_tbl 掃描 located_id;0xFF=沒有 */
static uint8_t nq_find_id(void)
{
    uint16_t base = res_read16(&textbank_res, TC_NPC_ID << 1);
    uint8_t y, b;

    for (y = 0; ; y++) {
        res_read(&textbank_res, base + y, &b, 1);
        if (b == NONE_NPC)
            return 0xFF;
        if (b == located_id)
            return y;
    }
}

/* 等效 get_n_msg/skip_one_msg:窗內跳過 x 則訊息(雙 0 收尾) */
static uint16_t nq_get_n_msg(uint8_t x)
{
    uint16_t o = 1;                         /* 跳過 msg_number */

    while (x--) {
        for (;;) {
            while (nq_window[o])
                o++;
            o++;
            if (nq_window[o] == 0)
                break;
            o++;
        }
        o++;
    }
    return o;
}

/* 等效 move_text+show_talk_msg:原文直出(原版不過 format_string) */
static void nq_show(uint16_t o)
{
    memcpy(OutBuf, nq_window + o, 200);
    show_talk_msg();
}

static uint8_t npc_quest(void)
{
    uint8_t y = nq_find_id();
    uint16_t base, o;
    uint8_t x, n;

    if (y == 0xFF)
        return 1;

    base = res_read16(&textbank_res, TC_NPC_QUEST << 1);
    res_read(&textbank_res, base + (uint16_t)y * 5, nq_msg_addr, 5);
    res_read(&textbank_res, rd16(nq_msg_addr), nq_window, NQ_WINDOW_LEN);

    n = nq_window[0];                       /* msg_number */
    nq_show(nq_get_n_msg((uint8_t)random_it(n)));

    if (nq_give_id == 0xFF)
        goto out;
    x = find_goods(nq_give_id);
    if (x == 0xFF)
        goto out;
    if (hero.man_goods[x + 1] < nq_give_count)
        goto out;

    o = nq_get_n_msg(n);                    /* 第 n+1 則:求物(WRAM 窗) */
    /* Three wrapped text rows plus a separate in-box A/B hint row. */
    if (!confirm_box(6, 0, 6 + 12 * 12, 62, nq_window + o))
        goto out;
    scroll_to_lcd();

    x = find_goods(nq_give_id);
    hero.man_goods[x + 1] -= nq_give_count;
    add_goods(nq_receive_id);

    nq_show(nq_get_n_msg((uint8_t)(n + 1))); /* 第 n+2 則:答謝 */
out:
    scroll_to_lcd();
    fb_flush();
    return 0;
}

/* ================= 收攏的 BANKED 入口(HOME 跳板節約) ================= */

/* 等效 npc.s npc_talk 的任務段:任務 NPC → 尋人達成 → 資料 quest;
 * 回 0 = 都沒話說(npc.c 接 dunno 隨機語) */
uint8_t talk_task(void) BANKED
{
    uint8_t id;

    if (pyh_task())
        return 1;
    id = find_quest_id(QUEST_NPC);
    if (id != 0 && id == located_id) {      /* 尋人:找到目標了 */
        set_quest_over(QUEST_NPC);
        fmt_show(tm_know_msg);
        return 1;
    }
    return (uint8_t)(npc_quest() == 0);
}

/* 等效 npc.s man_win 的任務判定段(勝利且對方死亡後呼叫)。
 * 原版順序:先查 QUEST_KILL(命中→記完成+si_kill++,跳過 ghost),
 * 再查 QUEST_GHOST(命中→發賞 show_bonus)。 */
void fight_win_task(void) BANKED
{
    if (find_quest_id(QUEST_KILL) == located_id) {
        set_quest_over(QUEST_KILL);
        hero.si_kill++;
        return;
    }
    if (find_quest_id(QUEST_GHOST) == located_id)
        ghost_win();
}
