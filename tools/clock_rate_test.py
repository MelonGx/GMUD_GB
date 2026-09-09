# -*- coding: utf-8 -*-
"""遊戲世界時鐘的一秒必須是真實的一秒。

原版 system.s sys_refresh 掛在 RTC 上,一秒就是一秒。移植版用幀數數,
而 GBC 一秒是 4194304/70224 = 262144/4389 幀 ≈ 59.7275,不是 60。
舊版寫死 60 幀,遊戲鐘每秒慢 0.456%——一小時慢 16 秒、一天慢 6 分半。
2026-08-02 改成 59 幀為底 + 餘數 3193/4389 累加(src/clock.c)。

oracle:hero.mud_age 每個遊戲秒 +1,單調不回捲,比 game_sec/min 好用。
時鐘只在 gmud_loop 閒置分支跑 heart_beat,所以測試就是站著不動數幀。

只數「N 幀走了幾秒」分辨不出 0.456%——1200 幀不論修沒修都是 20 秒。
改成逐幀取樣、記每次進位的幀號:修好之後相鄰間隔會是 59 與 60 混著出現
(60 佔 3193/4389 ≈ 72.75%),沒修則清一色 60。這個判據又快又利。

用法: py -3.12 tools/clock_rate_test.py build/hero.gbc
"""
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, str(Path(__file__).parent))

from pyboy import PyBoy
from gbsym import load_symbols
from bootseq import enter_game

S = load_symbols()
rom = sys.argv[1]
HERO = S["_hero"]
OFF_MUD_AGE = 5                 # uint32,每遊戲秒 +1

FPS = 4194304.0 / 70224.0       # 59.7275
FRAMES = 3600                   # 約 60 遊戲秒 → 59 個間隔

Path(rom + ".ram").unlink(missing_ok=True)
pb = PyBoy(rom, window="null", cgb=True)
pb.tick(150, True)
enter_game(pb)
pb.tick(120, True)              # 等進世界、heart_beat 重錨


def mud_age():
    v = 0
    for i in range(4):
        v |= pb.memory[HERO + OFF_MUD_AGE + i] << (8 * i)
    return v


prev = mud_age()
marks = []
for f in range(FRAMES):
    pb.tick(1, True)
    v = mud_age()
    if v != prev:
        marks.append(f)
        prev = v
pb.stop(save=False)

assert len(marks) > 20, "時鐘沒在走(只進位 %d 次)" % len(marks)
gaps = [marks[i + 1] - marks[i] for i in range(len(marks) - 1)]
avg = sum(gaps) / float(len(gaps))
n59 = gaps.count(59)
n60 = gaps.count(60)
print("進位 %d 次,間隔 59 幀 ×%d、60 幀 ×%d、其他 ×%d"
      % (len(marks), n59, n60, len(gaps) - n59 - n60))
print("平均 %.3f 幀/秒(真實 %.3f;舊版寫死 60 = %+.3f%%)"
      % (avg, FPS, (60.0 / FPS - 1.0) * 100))

if n59 == 0:
    raise SystemExit("間隔全是 60 幀 → 餘數補償沒生效,遊戲鐘每秒慢 0.456%")
if abs(avg / FPS - 1.0) > 0.004:
    raise SystemExit("遊戲鐘平均偏差過大:%.3f 幀/秒" % avg)
print("OK  遊戲世界時鐘的一秒等於真實一秒")
