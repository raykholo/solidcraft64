# SolidCraft 64. Needs cc65 (cl65 on the PATH, or CC65=/path/to/bin).
CC65 ?=
CL65  = $(if $(CC65),$(CC65)/cl65,cl65)
SRC   = $(wildcard src/*.c) $(wildcard src/*.s)
OUT   = build

all: $(OUT)/solidcraft.prg

$(OUT)/solidcraft.prg: $(SRC) src/model.h src/sc64.cfg
	mkdir -p $(OUT)
	$(CL65) -t c64 -O -C src/sc64.cfg -m $(OUT)/solidcraft.map -o $@ $(SRC)
	@ls -l $@

d64: $(OUT)/solidcraft.prg
	python3 tools/mkd64.py $(OUT)/solidcraft.d64 $(OUT)/solidcraft.prg samples/*.SC64

clean:
	rm -rf $(OUT) src/*.o

.PHONY: all d64 clean

# Lane A's test harness: the wireframe on its own (tests/viewdemo.c).
viewdemo: $(OUT)/viewdemo.prg
$(OUT)/viewdemo.prg: tests/viewdemo.c src/view.c src/line.s src/clip.c src/model.c src/model.h src/line.h src/sc64.cfg
	mkdir -p $(OUT)
	$(CL65) -t c64 -O -C src/sc64.cfg -m $(OUT)/viewdemo.map -o $@ tests/viewdemo.c src/view.c src/line.s src/clip.c src/model.c
	@ls -l $@

.PHONY: viewdemo
