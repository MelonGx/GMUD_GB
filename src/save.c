/* save.c - 字節級忠實重現原版 save.s 的存檔格式
 *
 * SRAM 0xA000 起的 288 字節 = 原版 /gmud.sav 檔案映像:
 *   [0..279]   save_data(game_ver/pid 明文,其後加密)
 *   [280..285] 6 字節 ECC 校驗和(get_checksum,對明文計算,隨後加密)
 *   [286]      種子字節 1(原版存 game_second_m2 原值,明文)
 *   [287]      種子字節 2(原版存 getms 原值,明文)
 * 加密:file[5..285](SAVE_SIZE+1 字節)XOR 流加密,
 *   PRNG: seed = seed*65533 + 1 + carry, carry=io_bios_bsw bit7。
 *
 * GBC 硬體替代(功能不變):
 *   - 檔案系統 → 電池 SRAM(映像字節相同)
 *   - 種子熵源 game_second_m2/getms → DIV_REG(原版種子原值存檔尾,
 *     讀檔按檔內值還原,故熵源替換不影響互轉)
 *   - io_bios_bsw bit7 → 假設為 0(見 IO_BSW_BIT7;若日後導入真機
 *     存檔解不開,改成 1 重試)
 * 代碼在 bank 25(僅 game.c 同 bank 呼叫;SRAM 存取不涉 ROM bank)。
 */
#pragma bank 26
#include <gb/gb.h>
#include <string.h>
#include "save.h"

hero_save_t hero;

extern const uint8_t ecc_tbl[256];

#define IO_BSW_BIT7 0           /* 原版 io_bios_bsw bit7 的假設值 */
#define FILE_SIZE (SAVE_SIZE + 8)

/* ---- 雙存檔 slot(用戶決策 2026-07-10)----
 * slot0=0xA000(與單槽時代存檔向後相容)、slot1=0xA500。
 * 任務狀態工作區 0xA320..0xA388(105B,quest_temp/task_buf/task_gbuf/
 * home_buf 連續)為「當前角色」的常駐 RAM(原機 RAM 語義);每 slot 另有
 * 快照區(+尾 1 字節有效標記 0xC3),換 slot 時換出/換入;
 * 0xA1F0 兩字節 = {0x5A, 擁有者 slot} 記錄工作區屬於誰。
 * 標記缺失(雙槽功能前的舊 SRAM)視同屬於 slot0,工作區原樣保留。 */
uint8_t active_slot;
#define SLOT_BASE() ((uint8_t *)(active_slot ? 0xA500 : 0xA000))
#define TASK_WORK   ((uint8_t *)0xA320)
#define TASK_LEN    105
#define TASK_SNAP(s) ((uint8_t *)((s) ? 0xA620 : 0xA120))
#define OWN_MARK    ((uint8_t *)0xA1F0)

/* 共用暫存區(WRAM 緊張):存檔 file_buf 與文本 OutBuf 分時複用,
 * 兩者不可能同時活躍(存檔操作不在對話格式化中發生)。
 * 置於 SRAM 0xA200(存檔映像 0xA000..0xA11F 之後;main 已常開 SRAM)
 * —— WRAM 讓給棧,根治棧溢底毀全域(2026-07-10)。 */
__at(0xA200) uint8_t scratch288[FILE_SIZE];
#define filebuf scratch288

/* 原版 get_checksum:對 data[5..SAVE_SIZE-1] 算 6 字節校驗 → out */
static void get_checksum(const uint8_t *data, uint8_t *out)
{
    uint16_t n = SAVE_SIZE - 5;
    const uint8_t *p = data + 5;
    uint8_t y = 0;
    uint8_t b, t;
    uint16_t sum;

    memset(out, 0, 6);
    while (n--) {
        b = *p++;
        out[5] ^= b;
        sum = (uint16_t)out[3] + b;
        out[3] = (uint8_t)sum;
        if (sum & 0x100)
            out[4]++;
        t = ecc_tbl[b];
        out[2] ^= t & 0x3F;
        if (t & 0x40) {
            out[0] ^= y;
            out[1] ^= y ^ 0xFF;
        }
        y++;                    /* uint8 自然回繞 = 原版 iny */
    }
    out[2] = (uint8_t)(((out[2] ^ 0xFF) << 2) | 3);
}

/* 原版 secret:用 s1/s2 派生種子,XOR file[5..5+SAVE_SIZE+1) */
static void secret(uint8_t *file, uint8_t s1, uint8_t s2)
{
    uint16_t seed = (uint16_t)(s1 ^ 0x99 ^ 'J')
                  | ((uint16_t)(s2 ^ 0x66 ^ 'L') << 8);
    uint16_t n = SAVE_SIZE + 1;
    uint8_t *p = file + 5;

    while (n--) {
        /* my_random: seed*65533 的低 16 位 + 1 + carry */
        seed = seed * 65533U + 1 + IO_BSW_BIT7;
        *p++ ^= (uint8_t)seed;
    }
}

/* 解密 SRAM 映像到 filebuf 並驗證,等效原版 load_file + check_pid。
 * 回傳 1=合法存檔(filebuf 為明文映像) */
static uint8_t load_verify(void)
{
    uint8_t chk[6];
    uint8_t ok = 1;
    uint8_t i;

    /* SRAM 已由 main 常開(filebuf 本身在 SRAM,不可再 DISABLE) */
    memcpy(filebuf, SLOT_BASE(), FILE_SIZE);

    secret(filebuf, filebuf[SAVE_SIZE + 6], filebuf[SAVE_SIZE + 7]);

    /* check_pid:pid 'HERO' + 版本 */
    if (filebuf[1] != 'H' || filebuf[2] != 'E'
        || filebuf[3] != 'R' || filebuf[4] != 'O')
        ok = 0;
    if (filebuf[0] != GAME_VERSION)
        ok = 0;

    if (ok) {
        get_checksum(filebuf, chk);
        for (i = 0; i < 6; i++)
            if (chk[i] != filebuf[SAVE_SIZE + i])
                ok = 0;
    }
    return ok;
}

uint8_t save_exists(void) BANKED
{
    return load_verify();
}

uint8_t save_load(void) BANKED
{
    if (!load_verify()) {
        /* 原版校驗失敗即 delete_file */
        save_wipe();
        return 0;
    }
    memcpy(&hero, filebuf, SAVE_SIZE);
    return 1;
}

void save_store(void) BANKED
{
    uint8_t s1, s2;

    hero.game_ver = GAME_VERSION;
    hero.game_pid[0] = 'H';
    hero.game_pid[1] = 'E';
    hero.game_pid[2] = 'R';
    hero.game_pid[3] = 'O';

    /* 等效原版 set_save_data */
    memcpy(filebuf, &hero, SAVE_SIZE);
    get_checksum(filebuf, filebuf + SAVE_SIZE);
    s1 = DIV_REG;               /* 替代 game_second_m2(熵源) */
    s2 = DIV_REG ^ 0xA5;        /* 替代 getms */
    filebuf[SAVE_SIZE + 6] = s1;
    filebuf[SAVE_SIZE + 7] = s2;
    secret(filebuf, s1, s2);

    memcpy(SLOT_BASE(), filebuf, FILE_SIZE);
    task_snap_out();            /* 快照隨存檔落盤,slot 快照恆不舊於存檔 */
}

void save_wipe(void) BANKED
{
    memset(SLOT_BASE(), 0, FILE_SIZE);
}

/* ---- slot 輔助 ---- */

/* 探測 slot 是否有合法存檔;有則抄出角色名(9B 明文) */
uint8_t save_probe(uint8_t slot, uint8_t *name9) BANKED
{
    uint8_t keep = active_slot;
    uint8_t ok;

    active_slot = slot;
    ok = load_verify();
    if (ok && name9)
        memcpy(name9, filebuf + 19, 9);
    active_slot = keep;
    return ok;
}

static uint8_t owner_get(void)
{
    if (OWN_MARK[0] == 0x5A && OWN_MARK[1] <= 1)
        return OWN_MARK[1];
    return 0;                   /* 標記缺失=舊 SRAM,工作區屬 slot0 */
}

void task_snap_out(void) BANKED
{
    uint8_t ow = owner_get();

    memcpy(TASK_SNAP(ow), TASK_WORK, TASK_LEN);
    TASK_SNAP(ow)[TASK_LEN] = 0xC3;
    OWN_MARK[0] = 0x5A;
    OWN_MARK[1] = ow;
}

/* 續玩:把任務工作區綁到 active_slot。
 * 同 slot → 原樣沿用(可能比末次存檔新 = 原機 RAM 常駐語義);
 * 異 slot → 舊主快照換出,本 slot 快照換入(無快照則清零) */
void task_bind_slot(void) BANKED
{
    uint8_t ow = owner_get();

    if (ow != active_slot) {
        task_snap_out();        /* 以 ow 為主換出 */
        if (TASK_SNAP(active_slot)[TASK_LEN] == 0xC3)
            memcpy(TASK_WORK, TASK_SNAP(active_slot), TASK_LEN);
        else
            memset(TASK_WORK, 0, TASK_LEN);
    }
    OWN_MARK[0] = 0x5A;
    OWN_MARK[1] = active_slot;
}

/* 新遊戲:舊主換出(異 slot 時),工作區清零並改綁 active_slot */
void task_bind_new(void) BANKED
{
    uint8_t ow = owner_get();

    if (ow != active_slot)
        task_snap_out();
    memset(TASK_WORK, 0, TASK_LEN);
    OWN_MARK[0] = 0x5A;
    OWN_MARK[1] = active_slot;
    task_snap_out();
}

/* 結構體大小必須恰為 SAVE_SIZE(佈局見 tools/layout_save.py 輸出) */
typedef char assert_save_size[(sizeof(hero_save_t) == SAVE_SIZE) ? 1 : -1];
