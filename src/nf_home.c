/* nf_home.c - 聯機對戰選單(bank 25):创建游戏/加入游戏(nf.s
 * netgame_menu)。pop_menu 直呼處理器函指標,處理器必須在 HOME/bank 25,
 * 故薄 thunk 以 BANKED 跨 bank 呼叫 nf.c(bank 30)。 */
#pragma bank 25
#include <gb/gb.h>
#include "menu.h"
#include "nf.h"

static const uint8_t it_create[] = {            /* 创建游戏 */
    0xB4, 0xB4, 0xBD, 0xA8, 0xD3, 0xCE, 0xCF, 0xB7, 0xFF
};
static const uint8_t it_join[] = {              /* 加入游戏 */
    0xBC, 0xD3, 0xC8, 0xEB, 0xD3, 0xCE, 0xCF, 0xB7, 0xFF
};

static uint8_t h_create(void) { return nf_create_game(); }
static uint8_t h_join(void)   { return nf_join_game(); }

static const menu_fn nf_h[2] = { h_create, h_join };
static const uint8_t *const nf_items[2] = { it_create, it_join };

/* 原版 netgame_menu:2 項直排 BOX 框 */
const cmenu_t netgame_menu =
    { 2, 1, 0, 1, MSTYLE_BOX, 0, 0, nf_h, 0, nf_items, 0 };
