/* menu.h - 通用選單引擎,等效原版 menu.s pop_menu/show_menu_txt
 *          + lee3.s list_menu/clean_list_right
 *
 * 原版四字節選單頭 → cmenu_t 顯式字段(語義一一對應):
 *   菜單樣式 bit6(帶顯示程序)→ shows;bit5(顯示數字)→ MF_DIGITS;
 *   bit4(單程序)→ 表內同一函數;bit3-0(項寬)→ char_form
 *   菜單數目 bit7(動態)→ n=0,條目在 dmenu_buf(名字經 name_tbl 索引)
 *   屏幕樣式 bit7(框)→ framed;bit6-4 → scr_form;bit3 → MF_USESET;
 *   bit2-0 → line_form
 *
 * 處理器返回值(等效 PushMenu/PullMenu 棧解退):
 *   0=留在選單(重畫+顯示程序) 1=退本層(PullMenu) 2=全退(PullMenu 1)
 * pop_menu 返回:0xFF=ESC 0xFE=全退傳播 其他=選中項
 */
#ifndef MENU_H
#define MENU_H
#include <stdint.h>
#include <gb/gb.h>      /* BANKED */
#include "text.h"       /* gfx_scratch */

#define MSTYLE_NORMAL 0     /* 反白當前項,無游標格 */
#define MSTYLE_ARROW  1
#define MSTYLE_BOX    2
#define MSTYLE_RADIO  3
#define MSTYLE_ICON   4
#define MSTYLE_ICON1  5     /* 右方格:圖示列+左側當前項名(查看頁) */
#define MSTYLE_CHECK  6     /* check_item 項打勾(啟用中武功) */
#define MSTYLE_CHECK1 7     /* 條目 bit7 打勾(裝備中物品) */

#define MF_DIGITS 0x01      /* 動態條目 2 字節:[id,數量],畫 xN */
#define MF_USESET 0x02      /* 初始游標=menu_set */

typedef uint8_t (*menu_fn)(void);
typedef void (*show_fn)(void);

typedef struct {
    uint8_t n;              /* 項數;0=動態(dmenu_buf[0]) */
    uint8_t line_form;      /* 每行項數 */
    uint8_t scr_form;       /* 可見行數;0=全部 */
    uint8_t framed;
    uint8_t style;
    uint8_t char_form;      /* 項寬(半形字元);0=自動 */
    uint8_t flags;
    const menu_fn *handlers;        /* NULL 或條目 NULL=CR 直接退出 */
    const show_fn *shows;           /* 顯示程序(游標移動即呼叫) */
    const uint8_t *const *items;    /* 靜態項名(0xFF 結尾) */
    const uint8_t *name_tbl;        /* 動態項名表(GAMEDATA bank) */
} cmenu_t;

extern uint8_t menu_set;    /* 當前/選中項(原版同名) */
#define dmenu_buf (gfx_scratch + 136)   /* 動態條目 [n, e0, e1...](42B) */

uint8_t pop_menu(uint8_t x, uint8_t y, const cmenu_t *m) BANKED;
void show_menu_txt(uint8_t x, uint8_t y, const cmenu_t *m) BANKED;

/* lee3.s:清單框(左 4 字分類 + 右面板);輸出 list_x0 等內域座標 */
extern uint8_t list_x0, list_y0, list_x1, list_y1;
uint8_t list_menu(uint8_t x, uint8_t y, const cmenu_t *m) BANKED;
void clean_list_right(void) BANKED;

#endif
