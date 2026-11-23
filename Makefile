# SolidCraft 64. Needs cc65 (cl65 on the PATH, or CC65=/path/to/bin).
CC65 ?=
CL65  = $(if $(CC65),$(CC65)/cl65,cl65)
SRC   = $(wildcard src/*.c) $(wildcard src/*.s)
CA65  = $(if $(CC65),$(CC65)/ca65,ca65)
LD65  = $(if $(CC65),$(CC65)/ld65,ld65)
OUT   = build

all: $(OUT)/solidcraft.prg $(OUT)/boot.prg

$(OUT)/solidcraft.prg: $(SRC) src/model.h src/sc64.cfg
	mkdir -p $(OUT)
	$(CL65) -t c64 -O -C src/sc64.cfg -m $(OUT)/solidcraft.map -o $@ $(SRC)
	@ls -l $@

# The boot loader: the disk's first file, SOLIDCRAFT. It loads SC64.BIN
# (solidcraft.prg) the slow way.
$(OUT)/boot.prg: src/boot/boot.s src/boot/boot.cfg
	mkdir -p $(OUT)
	$(CA65) -o $(OUT)/boot.o src/boot/boot.s
	$(LD65) -C src/boot/boot.cfg -o $@ $(OUT)/boot.o
	@ls -l $@

# The disk: SOLIDCRAFT (the boot loader) first, then SC64.BIN and the samples.
D64 = $(OUT)/solidcraft.d64
d64: $(OUT)/solidcraft.prg $(OUT)/boot.prg
	python3 tools/mkd64.py $(D64) $(OUT)/boot.prg SC64.BIN=$(OUT)/solidcraft.prg samples/*.SC64

# The files test harness (tests/filesdemo.c): its own program on its own disk,
# with the samples and some bad parts. Run it in VICE: tests/run-filesdemo.sh
filesdemo: $(OUT)/filesdemo.d64

$(OUT)/filesdemo.prg: tests/filesdemo.c src/files.c src/model.c src/model.h
	mkdir -p $(OUT)
	$(CL65) -t c64 -O -o $@ tests/filesdemo.c src/files.c src/model.c

$(OUT)/filesdemo.d64: $(OUT)/filesdemo.prg tests/mkdemodisk.py tools/mkd64.py samples/*.SC64
	python3 tests/mkdemodisk.py $@ $(OUT)/filesdemo.prg samples/*.SC64

test:
	python3 -m unittest discover -s tests -v

clean:
	rm -rf $(OUT) src/*.o

.PHONY: all d64 filesdemo test clean

# Lane A's test harness: the wireframe on its own (tests/viewdemo.c).
viewdemo: $(OUT)/viewdemo.prg
$(OUT)/viewdemo.prg: tests/viewdemo.c src/view.c src/line.s src/clip.c src/model.c src/model.h src/line.h src/sc64.cfg
	mkdir -p $(OUT)
	$(CL65) -t c64 -O -C src/sc64.cfg -m $(OUT)/viewdemo.map -o $@ tests/viewdemo.c src/view.c src/line.s src/clip.c src/model.c
	@ls -l $@

.PHONY: viewdemo
