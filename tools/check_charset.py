# -*- coding: utf-8 -*-
"""字庫覆蓋檢查:掃描所有進 ROM 的文本來源,找出 assets/charset.txt
缺漏的 GB2312 字(執行期渲染成首字 fallback「、」)。

掃描範圍:
  - assets/textbank.bin 的字串內容(走 class 表結構化抽取太複雜,
    改掃「來源」:asm_text.py 的輸入字串,見 --sources)
  - src/*_msgs.c / menutext.c / gamedata.c 等生成檔的陣列字節
  - 手寫 C 檔中的 GB 字節陣列(fight_home.c 選單項等)

比對前自動套 gbsubst.fix_text(執行期字節=修正後字節)。

用法: py -3.12 tools/check_charset.py            # 全掃
輸出: 缺字列表(gb碼 + 字 + 出處);0 缺字時 exit 0,否則 exit 1
"""
import re
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, str(Path(__file__).parent))
from gbsubst import CUSTOM_GLYPHS, fix_text

HERE = Path(__file__).resolve().parent.parent      # gbc/
ROOT = HERE.parent                                  # gmud/

# ---- charset ----
have = set()
for ln in (HERE / "assets/charset.txt").read_text(encoding="utf-8").splitlines():
    ln = ln.strip()
    if ln:
        have.add(int(ln.split()[0], 16))

miss = {}       # code -> (char, set(sources))


def note(code, src):
    if code in have:
        return
    ch = CUSTOM_GLYPHS.get(code)
    if ch is None:
        try:
            ch = code.to_bytes(2, "big").decode("gb2312")
        except UnicodeDecodeError:
            return                      # 非法對且非自造字:不是文本,忽略
    miss.setdefault(code, (ch, set()))[1].add(src)


def is_texty(bs):
    """文本判定:高位對絕大多數是常用區合法 GB(A1A1-F7FE 且低位 A1+);
    二進制表(attr/點陣)會大量產生無效或罕見對 → 濾除"""
    good = bad = 0
    i = 0
    n = len(bs)
    while i < n:
        b = bs[i]
        if b < 0x81:
            i += 1
            continue
        if i + 1 < n and 0xA1 <= b <= 0xF7 and 0xA1 <= bs[i + 1] <= 0xFE:
            good += 1
        else:
            bad += 1
        i += 2
    return good >= 2 and bad * 4 <= good


def scan_blob(bs, src, filt=False):
    """對齊走訪:ASCII 單步,高位 0x81+ 視為 GB 對;$ 轉義跳過"""
    if filt and not is_texty(bs):
        return
    i = 0
    n = len(bs)
    while i < n:
        b = bs[i]
        if b == 0x24 and i + 1 < n and bs[i + 1] < 0x80:    # $x 轉義
            i += 2
            continue
        if b < 0x81:
            i += 1
            continue
        if i + 1 < n:
            note((b << 8) | bs[i + 1], src)
        i += 2


# ---- C 生成檔/手寫檔的陣列字節 ----
C_FILES = ["fight_msgs.c", "task_msgs.c", "npc_msgs.c", "menutext.c",
           "baishi_msgs.c", "gamedata.c", "fight_home.c", "npc.c",
           "text.c", "task.c", "game.c", "pyh_quest.c", "menu.c"]
for f in C_FILES:
    p = HERE / "src" / f
    if not p.exists():
        continue
    data = p.read_text(encoding="latin-1")
    for m in re.finditer(r"\{[^{}]*\}", data, re.S):
        bs = bytes(int(x, 16)
                   for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(0)))
        scan_blob(fix_text(bs), "src/" + f, filt=True)  # C 檔混二進制表,須濾

# ---- textbank 來源(.s/.txt/.gb 的字串字面量,scode 支) ----
TB_SOURCES = ["data/npc_data.gb", "data/kf_data.gb", "data/npc_quest.txt",
              "data/perform.txt", "data/goods_data", "text_addr.s"]


def scan_asm(path, src):
    """抽 db '…' 字面量;處理 if scode/else/endif(取 scode=1 支)"""
    keep = [True]
    for raw in path.read_bytes().split(b"\n"):
        line = raw.split(b";")[0].rstrip()
        s = line.strip()
        low = s.lower()
        if low.startswith(b"if"):
            keep.append(b"scode" in low or low == b"if 1")
            continue
        if low == b"else":
            keep[-1] = not keep[-1]
            continue
        if low == b"endif":
            keep.pop()
            continue
        if not all(keep):
            continue
        for m in re.finditer(rb"'([^']*)'", line):
            scan_blob(fix_text(m.group(1)), src)


for f in TB_SOURCES:
    p = ROOT / f
    if p.exists():
        scan_asm(p, f)

# ---- 報告 ----
if miss:
    print("缺字 %d 個(執行期渲染為「、」):" % len(miss))
    for code in sorted(miss):
        ch, srcs = miss[code]
        print("%04x %s  <- %s" % (code, ch, ", ".join(sorted(srcs))))
    print("\n修法:附加「%04x 字」行到 assets/charset.txt(UTF-8 無 BOM),"
          "然後 make font && make(先刪 build/hero.gbc)")
    sys.exit(1)
print("charset OK:所有文本來源的字都在 assets/charset.txt")
