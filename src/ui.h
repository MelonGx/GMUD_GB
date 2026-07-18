/* ui.h - UI 基礎例程
 *
 * 對照原版:wait_key(system.s,含按鍵回推)、show_string/show_one_line
 * (stringx.s)、clear_nline2、message_box、message_box_for_pyh(ConfirmBox)、
 * input_digit(inputx.s)、show_text(lee1.s)、show_process(aux.s 缺失,
 * 依呼叫端重建:進度條+數字+定時 tick)、percent(math.s)
 */
#ifndef UI_H
#define UI_H
#include <stdint.h>
#include <gb/gb.h>      /* BANKED */

/* 按鍵碼(GBC 映射:CR=A ESC=B F1=START,方向=十字鍵) */
#define K_NONE  0
#define K_CR    1
#define K_ESC   2
#define K_UP    3
#define K_DOWN  4
#define K_LEFT  5
#define K_RIGHT 6
#define K_F1    7

uint8_t wait_key(void) BANKED;             /* 阻塞取鍵(回推優先) */
void    push_key(uint8_t k) BANKED;        /* 等效原版 key|=80h 回推 */

/* 像素級線/反白(y 向自動標髒) */
void ui_hline(uint8_t x0, uint8_t x1, uint8_t y) BANKED;
void ui_vline(uint8_t x, uint8_t y0, uint8_t y1) BANKED;
void ui_invert(uint8_t x, uint8_t y, uint8_t w, uint8_t h) BANKED;

void clear_nline2(uint8_t y, uint8_t n) BANKED;    /* 整寬清 n 行(像素) */

/* 逐行輸出(等效 stringx.s):ss_ptr 為原版 string_ptr */
extern const uint8_t *ss_ptr;
uint8_t show_one_line(uint8_t xh, uint8_t y) BANKED;   /* 回傳本行寫入字節數 */
uint8_t show_string(uint8_t nlines, uint8_t xh, uint8_t y) BANKED;

/* 訊息框:畫框+逐行顯示 OutBuf(容量=(y1-y0)/12 行),等任意鍵 */
void message_box(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) BANKED;

/* 確認框(等效 ConfirmBox/message_box_for_pyh):1=CR 確認 0=ESC 取消 */
uint8_t confirm_box(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1,
                    const uint8_t *msg) BANKED;

/* 數字輸入(等效 inputx.s input_digit,原機數字鍵 GBC 無 →
 * 上下 ±1 帶按住連發;A 確認、B 取消回傳原值) */
uint16_t input_digit(uint8_t x, uint8_t y, uint16_t val, uint16_t max) BANKED;

/* 等效 lee1.s show_text:格式化 msg → 底部 3 行顯示,停留約 2 秒 */
void show_text(const uint8_t *msg) BANKED;
void show_text_out(void) BANKED;    /* 顯示已格式化的 OutBuf(跨 bank 訊息用) */

/* 等效 aux.s show_process(缺失重建):
 * 迴圈{set_line→進度條;set_digit→cur/max 數字;tick=program()},
 * program 回傳 1 或按鍵即停。interval 以幀計(原版 ms)。 */
extern uint16_t binbuf, bcdbuf;     /* set_line/set_digit 輸出(原版同名) */
void show_process(uint8_t x, uint8_t y, void (*set_line)(void),
                  void (*set_digit)(void), uint8_t interval,
                  uint8_t (*program)(void)) BANKED;

uint8_t percent(uint16_t a, uint16_t b) BANKED;   /* a*100/b(math.s) */

/* 等效 system.s sys_refresh:遊戲時鐘(每真實秒 mud_age++)+
 * 每 TICK_TIME 秒回血耗糧(busy 期間只走鐘)。掛在 wait_key/主迴圈/
 * show_process。GBC 無 RTC,以 vblank 計秒(59.73Hz,慢 0.45%)。 */
void heart_beat(void) BANKED;
void heart_beat_reset(void) BANKED;     /* 開機錨定(game_boot 用) */

/* 等效 math.s random_it:0..range-1(mud_seed 流) */
extern uint16_t mud_seed;
uint16_t random_it(uint16_t range) BANKED;
uint32_t random_it32(uint32_t divisor) BANKED;    /* perform_random_it */

void mt_tpl_jiali(uint8_t *dst) BANKED;     /* 加力提示模板(供 bank26) */

#endif
