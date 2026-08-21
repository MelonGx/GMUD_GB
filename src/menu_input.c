/* Per-frame menu input and a small VBlank edge queue.  Slow description
 * callbacks may finish later, but direction taps made during them are kept. */
#pragma bank 27
#include <gb/gb.h>
#include <stdint.h>
#include "menu_input.h"
#include "ui.h"

#define DIR_KEYS (J_UP | J_DOWN | J_LEFT | J_RIGHT)
#define SHOW_IDLE_FRAMES 8

volatile uint8_t menu_joy_active;
volatile uint8_t menu_joy_prev;
volatile uint8_t menu_joy_now;
volatile uint8_t menu_joy_head;
volatile uint8_t menu_joy_tail;
volatile uint8_t menu_joy_queue[MENU_JOY_QUEUE_SIZE];

static uint8_t repeat_key;
static uint8_t repeat_ticks;
static uint8_t show_idle;

static uint8_t take_pressed(void)
{
    uint8_t pressed = 0;

    disable_interrupts();
    if (menu_joy_tail != menu_joy_head) {
        pressed = menu_joy_queue[menu_joy_tail];
        menu_joy_tail = (menu_joy_tail + 1) & (MENU_JOY_QUEUE_SIZE - 1);
    }
    enable_interrupts();
    return pressed;
}

static uint8_t direction_key(uint8_t raw)
{
    if (raw & J_UP)    return K_UP;
    if (raw & J_DOWN)  return K_DOWN;
    if (raw & J_LEFT)  return K_LEFT;
    if (raw & J_RIGHT) return K_RIGHT;
    return K_NONE;
}

void menu_input_begin(void) BANKED
{
    waitpadup();
    disable_interrupts();
    menu_joy_prev = 0;
    menu_joy_now = 0;
    menu_joy_head = 0;
    menu_joy_tail = 0;
    menu_joy_active = 1;
    enable_interrupts();
    repeat_key = 0;
    repeat_ticks = 0;
    show_idle = 0;
}

void menu_input_end(void) BANKED
{
    disable_interrupts();
    menu_joy_active = 0;
    menu_joy_head = 0;
    menu_joy_tail = 0;
    enable_interrupts();
}

uint8_t menu_input_wait(uint8_t show_pending, uint8_t row_pending) BANKED
{
    for (;;) {
        uint8_t pressed, held, move = 0;

        if (ui_pushed_key) {
            uint8_t key = ui_pushed_key;
            ui_pushed_key = 0;
            return key;
        }
        vsync();
        heart_beat();
        pressed = take_pressed();
        held = menu_joy_now & DIR_KEYS;

        if (pressed & J_A) {
            show_idle = 0;
            return K_CR;
        }
        if (pressed & J_B) {
            show_idle = 0;
            return K_ESC;
        }
        if (pressed & J_START) {
            show_idle = 0;
            return K_F1;
        }

        if (held) {
            if (held != repeat_key) {
                repeat_key = held;
                repeat_ticks = 0;
            } else if (repeat_ticks != 0xFF) {
                repeat_ticks++;
            }
            if (pressed & DIR_KEYS)
                move = pressed & DIR_KEYS;
            else if (repeat_ticks >= 12 && !(repeat_ticks & 3))
                move = held;
            show_idle = 0;
        } else {
            repeat_key = 0;
            repeat_ticks = 0;
            if (pressed & DIR_KEYS)
                move = pressed & DIR_KEYS;
        }
        if (move) {
            show_idle = 0;
            return direction_key(move);
        }

        if (row_pending)
            return MENU_EVENT_ROW;

        if (show_pending && !menu_joy_now) {
            if (++show_idle >= SHOW_IDLE_FRAMES) {
                show_idle = 0;
                return MENU_EVENT_SHOW;
            }
        } else if (!show_pending) {
            show_idle = 0;
        }
    }
}
