/* battle_loot.h - 戰後戰利品的同步滿包處理 */
#ifndef BATTLE_LOOT_H
#define BATTLE_LOOT_H

#include <stdint.h>
#include <gb/gb.h>      /* BANKED */

/* ids 為最多四件已判定可掉落的物品。
 * 回傳位元 i=1 表示 ids[i] 已真正加入背包。
 * 若需要新格，函式會留在戰後畫面，強制玩家現場選足要替換的舊格；
 * 選擇確認前 hero.man_goods 完全不變。 */
uint8_t battle_loot_resolve(uint8_t *ids, uint8_t count) BANKED;

/* npc_fight() 的薄入口：收集 npc.npc_goods、同步解決滿包，然後才提交
 * 石板位圖並顯示真正取得的戰利品。整段放 bank 30，避免擠爆 bank 25。 */
void battle_loot_collect_npc(void) BANKED;

#endif
