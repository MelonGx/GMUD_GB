#ifndef MENU_INPUT_H
#define MENU_INPUT_H

#include <stdint.h>
#include <gb/gb.h>

#define MENU_EVENT_SHOW 0x80
#define MENU_EVENT_ROW  0x81
#define MENU_JOY_QUEUE_SIZE 8

extern volatile uint8_t menu_joy_active;
extern volatile uint8_t menu_joy_prev;
extern volatile uint8_t menu_joy_now;
extern volatile uint8_t menu_joy_head;
extern volatile uint8_t menu_joy_tail;
extern volatile uint8_t menu_joy_queue[MENU_JOY_QUEUE_SIZE];

void menu_input_begin(void) BANKED;
void menu_input_end(void) BANKED;
uint8_t menu_input_wait(uint8_t show_pending, uint8_t row_pending) BANKED;

#endif
