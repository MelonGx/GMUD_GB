# -*- coding: utf-8 -*-
"""技能頁打勾回歸:注入基本拳腳+一門招式(八阵刀 kf=13? 用八卦掌 12),
主選單→技能→拳脚→啟用招式→退回分類欄,驗證右面板該項有打勾
(m_check 走 menu_set;舊 bug:show_menu_txt 寫死 0xFF 不打勾)。
用法: py -3.12 tools/check_mark_test.py build/hero.gbc build/cm_out
"""
import sys
sys.stdout.reconfigure(encoding="utf-8")
from pathlib import Path
from pyboy import PyBoy
sys.path.insert(0, "tools")
from gbsym import load_symbols
from bootseq import enter_game

S = load_symbols()
rom, out = sys.argv[1], Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
ram = Path(rom + ".ram")
if ram.exists():
    ram.unlink()
pb = PyBoy(rom, window="null", cgb=True)
pb.tick(160, True)
enter_game(pb)          # 標題→建角→進地圖(建角流程上線後必經)

HERO = S["_hero"]
OFF_USEKF, OFF_KFNUM, OFF_KF = 74, 79, 80

# 注入:基本拳腳(1)lv30 + 八卦掌(12,kf_attr=0x00 HAND 類)lv50;未啟用
kf = [(1, 30), (12, 50)]
for i, (kid, lvl) in enumerate(kf):
    pb.memory[HERO + OFF_KF + i * 4 + 0] = kid
    pb.memory[HERO + OFF_KF + i * 4 + 1] = lvl
pb.memory[HERO + OFF_KFNUM] = 2
pb.memory[HERO + OFF_USEKF + 0] = 0


def shot(name):
    pb.screen.image.convert("RGB").save(out / name)
    print(name, "usekf0=0x%02X" % pb.memory[HERO + OFF_USEKF])


def tap(btn, settle=30):
    pb.button_press(btn)
    pb.tick(10, True)
    pb.button_release(btn)
    pb.tick(settle, True)


tap("start", 40)
tap("right", 25)
tap("right", 25)            # 技能
tap("a", 60)                # 進清單(左欄:拳脚)
shot("C0_left_hand.png")
tap("a", 50)                # 進右欄
shot("C1_right.png")
tap("down", 30)             # 移到八卦掌
tap("a", 50)                # 啟用 → use_skills 回 1 退回左欄
shot("C2_after_enable.png") # ★右面板應在八卦掌旁打勾
u = pb.memory[HERO + OFF_USEKF]
ok = (u == (12 | 0x80))
print("usekf[0] = 0x%02X =>" % u, "enabled OK" if ok else "FAIL")
tap("b", 40)
tap("b", 40)
pb.stop(save=False)
print("RESULT:", "PASS(檢查 C2 截圖打勾)" if ok else "FAIL")
