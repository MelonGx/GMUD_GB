/* 在 full framebuffer 繪製期間，主迴圈可能數幀不能 joypad()。
 * fb_vbl_isr 已替選單逐幀鎖存按鍵；process 共用同一佇列，避免一幀
 * A/B/START 短按落在 fb_flush 裡而完全遺失。 */
#include <gb/gb.h>
#include "process_input.h"
#include "menu_input.h"

uint8_t process_take_cancel(void) NONBANKED
{
    uint8_t pressed = 0;

    disable_interrupts();
    while (menu_joy_tail != menu_joy_head) {
        pressed |= menu_joy_queue[menu_joy_tail];
        menu_joy_tail = (menu_joy_tail + 1) & (MENU_JOY_QUEUE_SIZE - 1);
    }
    enable_interrupts();
    return pressed & (J_A | J_B | J_START);
}
