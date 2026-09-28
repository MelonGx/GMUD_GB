/* sram_layout.h - bank 0 SRAM slot and task workspace layout.
 * These constants preserve the existing addresses and sizes exactly.
 */
#ifndef SRAM_LAYOUT_H
#define SRAM_LAYOUT_H

#define SRAM_SAVE_SLOT0       0xA000
#define SRAM_SAVE_SLOT1       0xA500
#define SRAM_TASK_SNAP0      0xA120
#define SRAM_TASK_SNAP1      0xA620
#define SRAM_OWNER_MARK      0xA1F0
#define SRAM_SCRATCH         0xA200
#define SRAM_QUEST_TEMP      0xA320
#define SRAM_TASK_BUF        0xA368
#define SRAM_TASK_GBUF       0xA370
#define SRAM_HOME_BUF        0xA388
#define SRAM_KMAP_NAME       0xA389
#define SRAM_QUEST_LIST      0xB340

#define SRAM_SCRATCH_LEN     288
#define SRAM_QUEST_TEMP_LEN   72
#define SRAM_TASK_BUF_LEN      8
#define SRAM_TASK_GBUF_LEN    24
#define SRAM_KMAP_NAME_LEN    17
#define SRAM_QUEST_LIST_LEN  591
#define SRAM_TASK_WORK_LEN   105

#endif
