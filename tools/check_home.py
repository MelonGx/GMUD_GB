# -*- coding: utf-8 -*-
"""連結後守門:每個 bank 都不得超過 16384 字節。ASlink 溢出不報錯,
兩次實測都是「無聲全壞」:
  2026-07-14 bank0 GSFINAL 被擠到 0x4001 → 白屏、IE=0、CPU 跑飛;
  2026-07-29 _CODE_26 漲到 0x4007(超 7 字節)→ 建角後進世界永久卡死,
             症狀完全不指向 bank 26(當時改的是 ui_invert 的 BANKED 標記,
             它連一次都沒被呼叫過)。
所以 bank0 與 bank1-31 一起檢查,別再靠人工看 .map。
用法: py -3.12 tools/check_home.py build/hero.noi
"""
import re
import sys

noi = sys.argv[1] if len(sys.argv) > 1 else "build/hero.noi"
segs = {}
for line in open(noi):
    m = re.match(r"DEF ([sl])__([A-Z_0-9]+) 0x([0-9A-Fa-f]+)$", line.strip())
    if m:
        kind, name, val = m.group(1), m.group(2), int(m.group(3), 16)
        segs.setdefault(name, {})[kind] = val

BANK0 = ("CODE", "HOME", "BASE", "LIT", "INITIALIZER", "GSINIT", "GSFINAL")
bad = []
top = 0
for name in BANK0:
    d = segs.get(name)
    if not d or "s" not in d:
        continue
    end = d["s"] + d.get("l", 0)
    if d["s"] < 0x4000:                 # bank0 起始的段才受限
        top = max(top, end)
        if end > 0x4000:
            bad.append((name, d["s"], end))

# ---- bank1-31:_CODE_<n> 每段長度不得超過 0x4000 ----
# banked 段一律從 0x4000 起,故只需看長度 l__CODE_<n>。
over = []
tight = []
for name, d in segs.items():
    m = re.fullmatch(r"CODE_(\d+)", name)
    if not m or "l" not in d:
        continue
    n, ln = int(m.group(1)), d["l"]
    if n == 0:
        continue
    if ln > 0x4000:
        over.append((n, ln))
    elif 0x4000 - ln < 64:
        tight.append((n, 0x4000 - ln))

if bad or over:
    for name, s, e in bad:
        print(f"check_home: FAIL bank0 {name} @{s:#x}..{e:#x} 溢出 0x4000!")
    for n, ln in sorted(over):
        print(f"check_home: FAIL bank{n} _CODE_{n} = {ln} 字節,"
              f"超出 {ln - 0x4000}!")
    sys.exit(1)
# free==0 的多半是 bin2banks 切出來的整塊資料 bank(tiles/textbank/guide),
# 本來就填滿,不算警訊;1..63 才是「程式 bank 快撐爆」的訊號。
squeeze = [(n, f) for n, f in sorted(tight) if f]
if squeeze:
    print("check_home: WARN 逼近上限 " +
          ", ".join(f"bank{n}={f}B" for n, f in squeeze))
print(f"check_home: OK bank0 top={top:#x} (剩 {0x4000 - top} 字節)")
