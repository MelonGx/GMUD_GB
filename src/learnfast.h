/* learnfast.h - 學習常態 tick 的等價批次快路徑 */
#ifndef LEARNFAST_H
#define LEARNFAST_H
#include <stdint.h>
#include <gb/gb.h>

#define LEARN_FAST_OK       0
#define LEARN_FAST_BOUNDARY 1
#define LEARN_FAST_CANCEL   2

void learn_process_begin(void) BANKED;
uint8_t learn_process_tick(void) BANKED;

#endif
