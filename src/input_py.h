/* input_py.h - 拼音輸入法取名(Task#9)。原版 NC2000 取名用宿主系統
 * 拼音輸入法(不在遊戲源碼內);GBC 無宿主 IME,依文曲星操作習慣重建。
 * 資料 pinyin_res(bank 38,gen_pinyin.py),代碼 bank 39。 */
#ifndef INPUT_PY_H
#define INPUT_PY_H
#include <stdint.h>
#include <gb/gb.h>

uint8_t input_pinyin(void) BANKED;      /* 1=已寫 hero.man_name 0=取消 */

#endif
