# 英雄坛说 GBC 移植
#
# ROM: MBC5 + 32KB SRAM + 電池(-yt 0x1B -ya 4),CGB 專用(-yC)
# 1MB 64 banks(2026-07-18 拼音輸入法擴容):font12 全一级字 bank 31-37、
# font16 bank 5(獨立碼表)、拼音表+IME bank 39;bank 1-4/6-8 已騰空

GBDK  = tools/gbdk
LCC   = $(GBDK)/bin/lcc
PYTHON ?= python

ROMFLAGS = -Wm-yC -Wm-yt0x1B -Wm-ya4 -Wm-yo64 -Wm-yn"HEROTAN"
SRC = $(wildcard src/*.c)

all: build/hero.gbc

build/hero.gbc: $(SRC) $(wildcard src/*.h)
	$(LCC) $(ROMFLAGS) -Wl-m -Wl-j -o $@ $(SRC)
	$(PYTHON) tools/check_home.py build/hero.noi

# 資料管線(字源:文曲星 TTF,雙字體;碼表已分家 2026-07-18:
# font12=charset12(全一级字,拼音輸入),font16=charset16(16px 實際用字))
font: assets/charset.txt
	$(PYTHON) tools/gen_imecharset.py
	rm -f src/font12_cjk_*.c src/font16_cjk_*.c
	$(PYTHON) tools/build_font.py --charset assets/charset12.txt \
		--ttf assets/wqx-12.ttf --px 12 --cell-h 13 \
		--name font12 --first-bank 31 --misc-bank 23 --out-dir src
	$(PYTHON) tools/build_font.py --charset assets/charset16.txt \
		--ttf assets/wqx-16.ttf --px 16 --cell-h 16 \
		--name font16 --first-bank 5 --misc-bank 23 --out-dir src

charset:
	$(PYTHON) tools/extract_charset.py .. assets/charset.txt

clean:
	rm -f build/hero.gbc build/*.ihx build/*.map

.PHONY: all font charset clean
