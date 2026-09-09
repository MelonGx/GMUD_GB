/* process_input.h - progress process 的 VBlank 取消鍵鎖存 */
#ifndef PROCESS_INPUT_H
#define PROCESS_INPUT_H
#include <stdint.h>
#include <gb/gb.h>

uint8_t process_take_cancel(void) NONBANKED;

#endif
