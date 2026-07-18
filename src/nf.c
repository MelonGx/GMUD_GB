/* nf.c - 聯機對戰(原版 nf.s 直譯,bank 30)。
 *
 * 流程(等效 nf.s):擂台 → 创建游戏(master)/加入游戏(slave)→
 * 互換角色資料(對方裝進本機 npc 槽)→ 回合令牌制:
 *   本方回合(net_flag bit1)= 戰鬥選單出招,每條戰鬥訊息連同全量
 *   狀態封包發給對方(net_send_data 置 bit1=0 交棒);
 *   對方回合 = 循環收包(顯示訊息+套用權威狀態),收到 net_repeat==0
 *   的包 → 輪到本方(bit1=1)。
 * 勝負:收/發包後 who_win 判 hp;net 模式無殺/放選擇(fight.c 分支)。
 * 退出:逃跑發 net_quit_flag 包;收到即 fail_quit(無訊息,退擂台)。
 * 錯誤:傳輸失敗/版本不符 → 原版三條錯誤訊息 + 結束。
 *
 * 傳輸層 link.c(對戰線,取代 IrDA;硬體替代,用戶決定 2026-07-17)。 */
#pragma bank 30
#include <gb/gb.h>
#include <string.h>
#include "save.h"
#include "text.h"
#include "game.h"
#include "goods.h"
#include "menu.h"
#include "fight.h"
#include "fight_internal.h"
#include "link.h"
#include "nf.h"

__at(0xBD80) uint8_t net_pkt[250];
uint8_t nf_over;

extern const cmenu_t netgame_menu;      /* nf_home.c(bank 25) */

/* ---- 錯誤訊息(GB2312,原版 nf.s 同文)---- */
static const uint8_t nf_send_fail_msg[] = {     /* 数据传送失败! */
    0xCA, 0xFD, 0xBE, 0xDD, 0xB4, 0xAB, 0xCB, 0xCD,
    0xCA, 0xA7, 0xB0, 0xDC, '!', 0, 0
};
static const uint8_t nf_recv_fail_msg[] = {     /* 数据接收失败! */
    0xCA, 0xFD, 0xBE, 0xDD, 0xBD, 0xD3, 0xCA, 0xD5,
    0xCA, 0xA7, 0xB0, 0xDC, '!', 0, 0
};
static const uint8_t nf_ver_err_msg[] = {       /* 版本不对! */
    0xB0, 0xE6, 0xB1, 0xBE, 0xB2, 0xBB, 0xB6, 0xD4, '!', 0, 0
};

/* 顯示錯誤 + 結束網戰(等效 show_send_msg → fail_quit) */
static void nf_error(const uint8_t *msg)
{
    format_string(msg);
    show_talk_msg();
    fight_exit_code = 2;
    combat_over = 1;
    nf_over = 1;
}

/* ---- 打包/解包(nf.s package_man / trans_npc,41B man_state 版)----
 * 佈局:[0..40] man_state(name..weapon) [41] picid [42..46] usekf
 * [47] kfnum [48..87] kf(id,級)×20 [88..97] 對方 hp 塊(hp..effhp)
 * [98] 對方 busy [99] 對方 weapon。行動方把雙方權威狀態一併送出。 */
static void package_man(void)
{
    uint8_t *p = net_pkt + 10;
    uint8_t i;

    memcpy(p, hero.man_name, 41);
    p[41] = hero.man_picid;
    memcpy(p + 42, hero.man_usekf, 5);
    p[47] = hero.man_kfnum;
    for (i = 0; i < 20; i++) {                  /* 4 步距 → (id,級) 對 */
        p[48 + (uint8_t)(i << 1)] = hero.man_kf[(uint8_t)(i << 2)];
        p[49 + (uint8_t)(i << 1)] = hero.man_kf[(uint8_t)(i << 2) + 1];
    }
    memcpy(p + 88, &npc.npc_hp, 10);
    p[98] = npc.npc_busy;
    p[99] = npc.npc_goods[0];
}

static void trans_npc(void)
{
    uint8_t *p = net_pkt + 10;
    uint8_t w;

    memcpy(npc.npc_name, p, 41);        /* npc_state 同構:尾字節=goods[0] */
    located_id = p[41];                 /* 對方頭像當 NPC 圖 */
    memcpy(npc.npc_usekf, p + 42, 5);
    npc.npc_kfnum = p[47];
    memcpy(npc_kf, p + 48, 40);
    if (net_init_flag & 0x80)
        return;                         /* 初始互換:不回寫本方狀態 */

    memcpy(&hero.man_hp, p + 88, 10);   /* 對方視角的本方權威狀態 */
    hero.man_busy = p[98];
    w = p[99];
    if ((uint8_t)(w ^ hero.man_weapon) & 0x80) {
        /* 落英繽紛擊落武器:被繳械 → 現武器數量-1(原版 luoying_patch) */
        uint8_t x = find_goods(hero.man_weapon);
        if (x != 0xFF)
            hero.man_goods[x + 1]--;
    }
    hero.man_weapon = w;
}

static void package_protocol(void)
{
    net_limb_flag = limb_flag;
    net_game_version = hero.game_ver;
    memcpy(net_pkt, net_vars, 7);
    net_pkt[7] = net_pkt[8] = net_pkt[9] = 0;
}

/* ---- 發送(nf.s net_send_data;訊息已由呼叫方拷入 NET_PKT_MSG)---- */
uint8_t net_send_data(void) BANKED
{
    net_repeat--;
    package_man();
    package_protocol();
    if (link_send_block(net_pkt)) {
        nf_error(nf_send_fail_msg);
        return 1;
    }
    net_msg_vision &= 0x7F;
    net_flag &= (uint8_t)~0x02;         /* 交棒 */
    return 0;
}

/* ---- 接收(nf.s net_receive_data)。回 1=網戰結束(錯誤/退出/勝負) */
static uint8_t net_receive_data(void)
{
    if (link_recv_block(net_pkt)) {
        nf_error(nf_recv_fail_msg);
        return 1;
    }
    if (net_pkt[0] != hero.game_ver) {  /* check_version */
        nf_error(nf_ver_err_msg);
        return 1;
    }
    memcpy(net_vars, net_pkt, 7);       /* judge_msg */
    limb_flag = net_limb_flag;
    if (net_quit_flag & 0x80) {         /* 對方退出 → fail_quit(無訊息) */
        fight_exit_code = 2;
        nf_over = 1;
        return 1;
    }
    trans_npc();
    if (!(net_msg_vision & 0x80) && !(net_perform_flag & 0x80)) {
        net_flag &= 0x7F;               /* 顯示不回發 */
        pf_show_fight_msg(NET_PKT_MSG);
        net_flag |= 0x80;
        if (pf_who_win()) {
            combat_over = 1;
            nf_over = 1;
            return 1;
        }
    }
    net_msg_vision &= 0x7F;
    net_perform_flag &= 0x7F;
    net_flag |= 0x02;                   /* 輪到本方 */
    return 0;
}

/* ---- 對局主循環(nf.s netgame:host_game/user_game)---- */
static void netgame(void)
{
    while (!nf_over && !combat_over) {
        if (net_flag & 0x02) {          /* 本方回合 */
            obj_flag = 0x80;
            pf_refresh_fight();
            pf_fight_menu();            /* 出招;處理器內 net 分支交棒/結束 */
        } else {                        /* 對方回合 */
            obj_flag = 0x00;
            pf_refresh_fight();
            do {
                if (net_receive_data())
                    return;
            } while (net_repeat != 0);
        }
    }
}

/* ---- 建立連線 + 初始互換(nf.s create_game/join_game)---- */
uint8_t nf_create_game(void) BANKED
{
    link_init(1);
    net_init_flag = 0x80;
    net_msg_vision = 0x80;              /* 初始包不顯示訊息 */
    memset(NET_PKT_MSG, 0, 100);
    if (net_send_data())
        goto out;
    net_msg_vision = 0;
    if (net_receive_data())
        goto out;
    net_init_flag = 0;
    link_patience = 1;                  /* 對局期:等對方出招無上限 */
    net_flag |= 0x02;                   /* 創建方先手 */
    netgame();
out:
    link_exit();
    return 1;                           /* 退選單 */
}

uint8_t nf_join_game(void) BANKED
{
    link_init(0);
    net_init_flag = 0x80;
    if (net_receive_data())
        goto out;
    net_msg_vision = 0x80;
    memset(NET_PKT_MSG, 0, 100);
    if (net_send_data())
        goto out;
    net_msg_vision = 0;
    net_init_flag = 0;
    link_patience = 1;                  /* 對局期:等對方出招無上限 */
    net_flag &= (uint8_t)~0x02;         /* 加入方後手 */
    netgame();
out:
    link_exit();
    return 1;
}

/* ---- 擂台入口(nf.s netfight)---- */
void netfight(void) BANKED
{
    memset(net_vars, 0, sizeof(net_vars));      /* init_protocol */
    nf_over = 0;
    combat_over = 0;
    pf_init_fight();
    pop_menu(50, 30, &netgame_menu);
}
