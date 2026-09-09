# -*- coding: utf-8 -*-
"""連結前守門:banked 代碼(有 #pragma bank 的 .c)不得自己 SWITCH_ROM。

banked 代碼放在 0x4000-0x7FFF——那正是被切換的那個窗。函式一旦執行
SWITCH_ROM,下一條指令就從新 bank 取,等於把自己切走,CPU 立刻跑飛。
HOME(0x0000-0x3FFF)不受切換影響,所以要切 bank 的例程一律留 HOME。

踩過兩次:
  2026-07-09 fight.c 自帶 SWITCH_ROM 副本 → 立即死機(見 fight.c:121);
  2026-07-29 gd_find_name 被搬進 goods_impl.c(bank 31)→ 查看「描述」
             頁起整個 UI 全白(game.c show_desc 第一步就呼叫它)。

用法: py -3.12 tools/check_selfswitch.py src
"""
import re
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
src = Path(sys.argv[1] if len(sys.argv) > 1 else "src")

bad = []
for p in sorted(src.glob("*.c")):
    lines = p.read_text(encoding="utf-8", errors="replace").splitlines()
    if not any(re.match(r"\s*#pragma\s+bank\b", ln) for ln in lines):
        continue                            # HOME:可以自由切 bank
    in_block = False
    for i, ln in enumerate(lines, 1):
        code = ln.split("/*")[0] if "/*" in ln else ln
        if in_block:                        # 粗略跳過區塊註解
            if "*/" in ln:
                in_block = False
            continue
        if "/*" in ln and "*/" not in ln:
            in_block = True
        if ln.lstrip().startswith("*") or ln.lstrip().startswith("//"):
            continue
        if "SWITCH_ROM" in code:
            bad.append((p.name, i, ln.strip()))

if bad:
    print("check_selfswitch: FAIL — banked 代碼不得自行 SWITCH_ROM")
    for f, i, ln in bad:
        print(f"  {f}:{i}  {ln}")
    print("  → 把該例程移回 HOME(不加 #pragma bank 的檔案)")
    sys.exit(1)
print("check_selfswitch: OK")
