/* map_cold.c - 換圖冷路徑(tools.s init_map/change_map/adjust_pos/
 * message_loop),自 map.c 遷出騰 HOME 空間;代碼在 bank 26。
 * 呼叫端 walk.c(bank25)經 BANKED 入口。 */
#pragma bank 26
#include <gb/gb.h>
#include "game.h"
#include "res.h"
#include "mapblob_res.h"

/* tools.s adjust_pos:主角落在屏中 (2,1) 附近,視窗夾在圖內 */
static void adjust_pos(void)
{
    G_Sx = G_Sy = G_Dx = G_Dy = 0;

    if (G_Init_X >= 2) {
        G_Sx = G_Init_X - 2;
        if (G_Sx > G_Max_Sx)
            G_Sx = G_Max_Sx;
    }
    if (G_Sx < G_Min_Sx && G_Init_X >= G_Min_Sx)
        G_Sx = G_Min_Sx;
    if (G_Init_Y >= 2) {
        G_Sy = G_Init_Y - 2;
        if (G_Map_Height >= ScreenY_Num && G_Sy > G_Map_Height - ScreenY_Num)
            G_Sy = G_Map_Height - ScreenY_Num;
    }
    G_role_X = (uint8_t)((G_Init_X - G_Sx) << 5);
    G_role_Y = (uint8_t)(((G_Init_Y - G_Sy) << 5) - 16);   /* 腳對齊格 */
}

void change_map(void) BANKED
{
    change_map0();
    adjust_pos();
    map_invalidate();       /* 換圖 → 增量合成緩衝作廢 */
}

void init_map(void) BANKED
{
    uint8_t hdr[INIT_MAP_LEN];

    res_read(&mapblob_res, 0, hdr, INIT_MAP_LEN);
    G_Total_Map  = (uint16_t)hdr[0] | ((uint16_t)hdr[1] << 8);
    G_Total_Door = (uint16_t)hdr[2] | ((uint16_t)hdr[3] << 8);
    G_Curr_Map = hdr[4];
    G_Init_X = hdr[5];
    G_Init_Y = hdr[6];
    G_Door_Addr = (uint16_t)hdr[7] | ((uint16_t)hdr[8] << 8);
    G_Npc_Addr  = (uint16_t)hdr[9] | ((uint16_t)hdr[10] << 8);
    G_Map_Addr = INIT_MAP_LEN;
    change_map();
}

/* tools.s message_loop:站上門格 → 讀門記錄換圖 */
void message_loop(void) BANKED
{
    uint8_t rec[6];

    if (G_Curr_Id < DOOR_OFF || G_Curr_Id >= NPC_OFF)
        return;

    res_read(&mapblob_res,
             G_Door_Addr + (uint16_t)(G_Curr_Id - DOOR_OFF) * 6,
             rec, 6);
    G_Curr_Map = rec[2];
    G_Init_X = rec[4];
    G_Init_Y = rec[5];
    change_map();
    show_player();
}
