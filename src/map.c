/* map.c - 地圖引擎,逐例程移植原版 tools.s
 *
 * 對照表:init_map/change_map0/adjust_pos/get_object0/make_ground/
 *         show_player(main_move)/adjust_status/move_blak/check_hit/
 *         message_loop → 同名函數
 * speed_read(bank_no=G_map_bank) → res_read(&mapblob_res,...)
 * 圖庫 get_img_data(id) → res_read(&tiles_res, id*128,...)
 */
#include <gb/gb.h>
#include <string.h>
#include "game.h"
#include "res.h"
#include "tiles_res.h"
#include "mapblob_res.h"
#include "player_res.h"
#include "player_girl_res.h"
#include "save.h"       /* man_gender:女版行走圖(用戶素材 2026-07-10) */
#include "blit.h"
#include "font.h"
#include "fb.h"

uint16_t G_Total_Map, G_Total_Door;
uint8_t  G_Curr_Map;
uint8_t  G_Init_X, G_Init_Y;
uint16_t G_Door_Addr, G_Npc_Addr, G_Map_Addr;

uint8_t  G_Map_Width, G_Map_Height;
uint8_t  G_Map_Ground[16];
uint8_t  G_MapName[17];
uint16_t G_Scene;

uint8_t  G_Sx, G_Sy, G_Dx, G_Dy;
uint8_t  G_Min_Sx, G_Max_Sx;
uint8_t  G_role_status, G_role_way, G_role_speed;
uint8_t  G_role_X, G_role_Y;

uint16_t G_Curr_Id;
uint16_t G_id_item;
uint8_t  located_id;

uint8_t  npc_stat_buf[32];
uint8_t  G_Task_Flag;

/* ---- 殺手系統(原版 gmud.h 同名全域)+ busy 標誌 ---- */
uint8_t  G_Killer_Map, G_Killer_X, G_Killer_Y;
uint8_t  ghost_gender_bak;          /* 通緝犯性別(選殺手圖用) */
uint8_t  busy_flag;                 /* bit7:戰鬥/學習/釣魚中(不回復) */
uint8_t  mv_active;                 /* 1=MAP 模式顯示中(mapview.c 主管) */

/* 共享圖形暫存(WRAM 緊張):地圖合成期間為 4 個圖塊緩衝;
 * 選單/查看期間後段(+376 起 200B)被 npc_desc 複用,
 * 故 ground 快取只在單次 make_ground 內有效 */
uint8_t gfx_scratch[576];
#define img_a      (gfx_scratch)
#define img_b      (gfx_scratch + 128)
#define ground_buf (gfx_scratch + 256)
#define player_buf (gfx_scratch + 384)
static uint16_t ground_cached;

/* 圖庫直取:id*128 定址(等效 get_img_attr+speed_read 的快路徑)。
 * HOME 常駐:含 SWITCH_ROM,切換 bank 代碼(如 fight.c)嚴禁自帶副本 */
void tile_fetch(uint16_t id, uint8_t *dst)
{
    uint8_t bank_save = _current_bank;
    uint8_t chunk = (uint8_t)(id >> 7);     /* 128 塊/bank */

    SWITCH_ROM(tiles_res.first_bank + chunk);
    memcpy(dst, tiles_res.chunks[chunk] + ((uint16_t)(id & 127) << 7), 128);
    SWITCH_ROM(bank_save);
}

#define KILLER_NPC_ID 125

/* 原版 face_x/face_y:朝向 → 面前格偏移 */
static const int8_t face_x[4] = { 0, -1, 1, 0 };
static const int8_t face_y[4] = { 1, 0, 0, -1 };

/* 等效 talk.s check_stat:位序 msktbl = 0x80>>(id&7),不可改(存檔兼容) */
uint8_t npc_alive(uint8_t id)
{
    return npc_stat_buf[id >> 3] & (uint8_t)(0x80 >> (id & 7));
}

/* get_object0:讀格子原始值,高 3 位(地面)已剝除(random_map 亦用) */
uint16_t get_object0(uint16_t cell_index)
{
    uint16_t raw = res_read16(&mapblob_res, G_Scene + (cell_index << 1));
    return raw & 0x1FFF;
}

/* 讀原始格(含地面位) */
static uint16_t get_cell_raw(uint16_t cell_index)
{
    return res_read16(&mapblob_res, G_Scene + (cell_index << 1));
}

/* tools.s get_image_id:格值 → 前景圖 id(0=無) */
static uint16_t get_image_id(uint16_t v)
{
    uint8_t rec[3];

    if (v < BLAK)
        return v;
    if (v < DOOR_OFF)
        return v - BLAK;
    if (v < NPC_OFF)
        return res_read16(&mapblob_res,
                          G_Door_Addr + (uint16_t)(v - DOOR_OFF) * 6);
    res_read(&mapblob_res,
             G_Npc_Addr + (uint16_t)(v - NPC_OFF) * 3, rec, 3);
    if (!npc_alive(rec[2]))
        return DEAD_NPC_IMG;
    return (uint16_t)rec[0] | ((uint16_t)rec[1] << 8);
}

void change_map0(void)
{
    uint8_t hdr[CHANGE_MAP_LEN];
    uint8_t i, solid;
    uint16_t v;

    G_Scene = res_read16(&mapblob_res,
                         G_Map_Addr + ((uint16_t)G_Curr_Map << 1));
    res_read(&mapblob_res, G_Scene, hdr, CHANGE_MAP_LEN);
    G_Map_Width = hdr[0];
    G_Map_Height = hdr[1];
    memcpy(G_Map_Ground, hdr + 2, 16);
    memcpy(G_MapName, hdr + 18, 16);
    G_MapName[16] = 0;
    G_Scene += CHANGE_MAP_LEN;

    G_Min_Sx = 0;
    G_Max_Sx = (G_Map_Width > ScreenX_Num)
                   ? G_Map_Width - ScreenX_Num : 0;
    if (G_Map_Width > ScreenX_Num) {
        solid = 1;
        for (i = 0; i < G_Map_Height; i++) {
            v = get_cell_raw((uint16_t)i * G_Map_Width) & 0x1FFF;
            if (v < BLAK || v >= DOOR_OFF) {
                solid = 0;
                break;
            }
        }
        if (solid)
            G_Min_Sx = 1;

        solid = 1;
        for (i = 0; i < G_Map_Height; i++) {
            v = get_cell_raw((uint16_t)i * G_Map_Width
                             + G_Map_Width - 1) & 0x1FFF;
            if (v < BLAK || v >= DOOR_OFF) {
                solid = 0;
                break;
            }
        }
        if (solid && G_Max_Sx > G_Min_Sx)
            G_Max_Sx--;
    }
}

void map_invalidate(void);

static void compose_cell(uint8_t j, uint8_t i);

/* tools.s 繪製迴圈:視窗內全部格子畫進 scroll_buf(逐格=compose_cell;
 * 僅換圖/緩衝作廢時走,行走用下方增量路徑) */
void make_ground(void)
{
    uint8_t i, j;
    uint8_t iy_end = G_Sy + ScreenY_Num + 1;
    uint8_t jx_end = G_Sx + ScreenX_Num + 1;

    memset(scroll_buf, 0, 20 * SCROLL_H);
    ground_cached = 0xFFFF;     /* 快取僅本次合成內有效(緩衝被複用) */
    for (i = G_Sy; i < iy_end; i++)
        for (j = G_Sx; j < jx_end; j++)
            compose_cell(j, i);
}

/* ================= 增量捲動合成(行走平滑化,用戶要求) =================
 * 行走一子步=視圖平移 8px(或主角原地移動):
 *   ① 復原主角底圖快照(scroll_buf 回純背景)
 *   ② 整塊平移緩衝 + 只重合成新露出的 8px 條帶
 *   ③ 存新位底圖快照 → 貼主角
 * 結果與 make_ground 全量合成逐字節一致(格繪製冪等,lee_block 自剪裁);
 * scroll_buf 靜止時仍含主角(UI 端 scroll_to_lcd 語義不變)。
 * iv_force_full:調試/驗證用,置 1 退回全量路徑。 */
static uint8_t iv_valid;
uint8_t iv_force_full;
static uint16_t iv_vx, iv_vy;       /* 上次合成的視圖像素原點 */
static uint8_t iv_px, iv_py;        /* 底圖快照的主角屏座標 */
static uint8_t iv_ph;               /* 快照行數(底緣裁剪後;0=無快照) */

/* 快照/圖名快取放 SRAM(常開;0xA690 前為存檔/任務區,見 save.c/task.h)。
 * 舊版放 fb 行 80-111「盲帶」是錯的:show_text 三行訊息範圍 y=54-92
 * (第 3 行溢出擴展帶是設計行為)會清掉+寫穿該區 → 學習/打坐後圖名
 * 消失、主角舊位白條;panyan 的 fb_clear 也整片摧毀
 * (2026-07-12 用戶 BGB 實測,遷 SRAM 根治)。
 * name_cache 緊湊存放:12 行 × 12B。 */
__at(0xA6A0) static uint8_t underlay[192];      /* 主角底圖 4B×48 連續 */
__at(0xA760) static uint8_t name_cache[12 * 12];
static uint8_t name_w;              /* 圖名繪製寬(px) */
static uint8_t name_ok;

void map_invalidate(void)
{
    iv_valid = 0;
    name_ok = 0;
}

/* 圖名:每子步 scroll_to_lcd 會蓋掉,原版逐次重畫;字模取用較貴,
 * 改為換圖時畫一次+快照點陣,之後回貼(輸出逐字節相同) */
static void show_map_name(void)
{
    uint8_t r, nb, m;
    uint8_t *p, *q;

    if (!name_ok) {
        name_w = font_draw_text_replace(0, 0, G_MapName);
        for (p = fb, q = name_cache, r = 0; r < 12;
             r++, p += FB_STRIDE, q += 12)
            memcpy(q, p, 12);
        name_ok = 1;
    }
    nb = name_w >> 3;
    m = (uint8_t)(0xFF00 >> (name_w & 7));      /* 部分字節的高位遮罩 */
}

/* 畫單一世界格(地面 PRINT + 前景 AND/OR),與 make_ground 逐格等價 */
static void compose_cell(uint8_t j, uint8_t i)
{
    int16_t x, y;
    uint16_t raw, fg;

    if (i >= G_Map_Height || j >= G_Map_Width)
        return;
    x = (int16_t)(j - G_Sx) * 32 - G_Dx;
    y = (int16_t)(i - G_Sy) * 32 - G_Dy;
    if (x >= ScreenX || y >= ScreenY)
        return;

    raw = get_cell_raw((uint16_t)i * G_Map_Width + j);
    {
        uint8_t g = (uint8_t)((raw >> 12) & 0x0E);
        uint16_t gid = (uint16_t)G_Map_Ground[g]
                     | ((uint16_t)G_Map_Ground[g + 1] << 8);
        if (gid != ground_cached) {
            tile_fetch(gid, ground_buf);
            ground_cached = gid;
        }
        lee_block(x, y, ground_buf, 4, 32, LCMD_PRINT);
    }
    /* Match check_hit(): ground-selector bits do not make a dynamic killer
     * non-walkable, and a walkable decoration yields to the killer sprite. */
    if ((G_Task_Flag & 0x80) && G_Curr_Map == G_Killer_Map
        && j == G_Killer_X && i == G_Killer_Y
        && (raw & 0x1FFF) < BLAK) {
        fg = ghost_gender_bak ? 466 : 474;      /* female/male killer */
    } else {
        fg = get_image_id(raw & 0x1FFF);
        if (fg == 0)
            return;
    }
    tile_fetch(fg + 512, img_b);
    lee_block(x, y, img_b, 4, 32, LCMD_AND);
    tile_fetch(fg, img_a);
    lee_block(x, y, img_a, 4, 32, LCMD_OR);
}

/* 重合成覆蓋屏矩形的所有世界格 */
static void compose_screen_rect(int16_t x0, int16_t y0,
                                int16_t x1, int16_t y1)
{
    uint16_t vx = (uint16_t)G_Sx * 32 + G_Dx;
    uint16_t vy = (uint16_t)G_Sy * 32 + G_Dy;
    uint8_t j0, j1, i0, i1, j, i;

    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > ScreenX - 1)
        x1 = ScreenX - 1;
    if (y1 > ScreenY - 1)
        y1 = ScreenY - 1;
    if (x0 > x1 || y0 > y1)
        return;
    j0 = (uint8_t)((vx + x0) >> 5);
    j1 = (uint8_t)((vx + x1) >> 5);
    i0 = (uint8_t)((vy + y0) >> 5);
    i1 = (uint8_t)((vy + y1) >> 5);
    for (i = i0; i <= i1; i++)
        for (j = j0; j <= j1; j++)
            compose_cell(j, i);
}

/* tools.s main_move/show_player:背景+主角(遮罩 AND、圖 OR)+圖名 */
void show_player(void)
{
    uint8_t frame = G_role_way * 3 + G_role_status;   /* player_1..12 序 */
    uint16_t vx = (uint16_t)G_Sx * 32 + G_Dx;
    uint16_t vy = (uint16_t)G_Sy * 32 + G_Dy;

    if (!iv_valid || iv_force_full) {
        make_ground();
    } else {
        int16_t ddx = (int16_t)(vx - iv_vx);
        int16_t ddy = (int16_t)(vy - iv_vy);

        /* ① 撤主角:復原底圖快照(scroll_buf 回純背景) */
        if (iv_ph) {
            fo_src = underlay;
            fo_dst = &scroll_buf[(uint16_t)iv_py * 20 + (iv_px >> 3)];
            fo_h = iv_ph;
            fo_copy();
        }

        ground_cached = 0xFFFF;         /* 緩衝可能被 UI 期複用過 */
        if (ddx == 0 && ddy == 0) {
            /* 原地(主角自移/選單返回):背景已完好 */
        } else if (ddy == 0 && ddx == 8) {          /* 視圖右移 */
            fo_dst = scroll_buf;
            fo_src = scroll_buf + 1;
            fo_len = 20 * SCROLL_H - 1;
            fo_copy_fwd();              /* 行尾髒字節=px152-159,下行重合成 */
            compose_screen_rect(152, 0, 159, SCROLL_H - 1);
        } else if (ddy == 0 && ddx == -8) {         /* 視圖左移 */
            fo_dst = scroll_buf + 20 * SCROLL_H - 1;
            fo_src = scroll_buf + 20 * SCROLL_H - 2;
            fo_len = 20 * SCROLL_H - 1;
            fo_copy_bwd();              /* 行首髒字節=px0-7,下行重合成 */
            compose_screen_rect(0, 0, 7, SCROLL_H - 1);
        } else if (ddx == 0 && ddy == 8) {          /* 視圖下移 */
            fo_dst = scroll_buf;
            fo_src = scroll_buf + 8 * 20;
            fo_len = (SCROLL_H - 8) * 20;
            fo_copy_fwd();
            compose_screen_rect(0, SCROLL_H - 8, 159, SCROLL_H - 1);
        } else if (ddx == 0 && ddy == -8) {         /* 視圖上移 */
            fo_dst = scroll_buf + 20 * SCROLL_H - 1;
            fo_src = scroll_buf + (SCROLL_H - 8) * 20 - 1;
            fo_len = (SCROLL_H - 8) * 20;
            fo_copy_bwd();
            compose_screen_rect(0, 0, 159, 7);
        } else {
            make_ground();              /* 非 8px 單軸位移:全量 */
        }
    }

    /* ③ 存新位底圖快照 → 貼主角(遮罩 AND、圖 OR)。
     * 主角矩形可越底緣(lee_block 自剪裁),快照同步裁剪防越界 */
    iv_ph = (G_role_Y < SCROLL_H)
                ? ((G_role_Y <= SCROLL_H - Role_Height)
                       ? Role_Height : (uint8_t)(SCROLL_H - G_role_Y))
                : 0;
    if (iv_ph) {
        fo_src = &scroll_buf[(uint16_t)G_role_Y * 20 + (G_role_X >> 3)];
        fo_dst = underlay;
        fo_h = iv_ph;
        fo_save();
    }

    {
        const res_t *pr = (hero.man_gender == 1) ? &player_girl_res
                                                 : &player_res;
        res_read(pr, (uint16_t)(12 + frame) * 192, player_buf, 192);
        lee_block(G_role_X, (int16_t)G_role_Y, player_buf, 4, 48, LCMD_AND);
        res_read(pr, (uint16_t)frame * 192, player_buf, 192);
        lee_block(G_role_X, (int16_t)G_role_Y, player_buf, 4, 48, LCMD_OR);
    }

    if (!name_ok)
        scroll_to_lcd();
    show_map_name();
    fb_present_overlay(scroll_buf, name_cache, name_w);

    iv_vx = vx;
    iv_vy = vy;
    iv_px = G_role_X;
    iv_py = G_role_Y;
    iv_valid = 1;
}

void adjust_status(void)
{
    G_role_status++;
    if (G_role_status >= 3)
        G_role_status = 0;
}

/* tools.s move_blak:面前格碰撞;1=擋路 */
uint8_t move_blak(void)
{
    uint8_t cx, cy;
    uint16_t v;
    uint8_t rec[3];

    G_Curr_Id = 0;
    located_id = 0;

    cy = (uint8_t)((G_role_Y + (Role_Height - Unit_Height) + G_Dy) >> 5)
       + G_Sy + face_y[G_role_way];
    if (cy >= G_Map_Height)
        return 1;

    cx = (uint8_t)((G_role_X + G_Dx) >> 5) + G_Sx + face_x[G_role_way];
    if (cx >= G_Map_Width)
        return 1;

    v = get_object0((uint16_t)cy * G_Map_Width + cx);
    G_Curr_Id = v;

    if (v < BLAK) {
        /* 等效 tools.s judge_killer:空格撞上殺手 → 視作 NPC 擋路 */
        if ((G_Task_Flag & 0x80) && G_Curr_Map == G_Killer_Map
            && cx == G_Killer_X && cy == G_Killer_Y) {
            G_Curr_Id = NPC_OFF;
            located_id = KILLER_NPC_ID;
            return 1;
        }
        return 0;   /* 可走 */
    }
    if (v < DOOR_OFF)
        return 1;   /* 障礙 */
    if (v < NPC_OFF)
        return 0;   /* 門可走 */

    /* NPC:記錄圖 id 與編號,擋路 */
    res_read(&mapblob_res,
             G_Npc_Addr + (uint16_t)(v - NPC_OFF) * 3, rec, 3);
    G_id_item = (uint16_t)rec[0] | ((uint16_t)rec[1] << 8);
    located_id = rec[2];
    return 1;
}

uint8_t check_hit(void)
{
    move_blak();
    return located_id != 0;
}

/* init_map/change_map/adjust_pos/message_loop(換圖冷路徑)
 * 已遷 walk.c(bank25,僅 bank25 呼叫)騰 HOME 空間 */
