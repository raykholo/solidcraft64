# SolidCraft 64

**Parametric CAD for the Commodore 64.** Sketch a rectangle or a circle, type
its dimensions, extrude it, and turn it round in wireframe, in 320 x 200 and
two colours. Save it to the floppy. Export an STL, slowly.

```
    **** COMMODORE 64 BASIC V2 ****
 64K RAM SYSTEM  38911 BASIC BYTES FREE

READY.
LOAD"SOLIDCRAFT",8,1

SEARCHING FOR SOLIDCRAFT
LOADING
READY.
RUN
```

SolidCraft 64 is a parody. It is not affiliated with, endorsed by, or a
replacement for SolidCraft. It is the third member of the family, after
[SolidCraft 98](https://github.com/raykholo/solidcraft98) and
[SolidCraft 2000](https://github.com/raykholo/solidcraft2000), and the only
one with a cassette port it doesn't use.

## Status

Under construction. The parts below arrive in this order:

1. The wireframe: sketch, extrude, rotate with the cursor keys or a joystick in port 2.
2. Files: `.SC64` parts on the disk, ASCII STL export, and a converter on your PC
   that brings SolidCraft 98 and SolidCraft 2000 parts over.
3. The splash, the menus, the feature list, and loading at the speed of a real
   1541 (about 400 bytes a second), border flashing. A fast loader cartridge is
   available, as it was.

## Building

You need [cc65](https://cc65.github.io). Then:

```
make          # build/solidcraft.prg
make d64      # build/solidcraft.d64, with the samples
```

To run it, open the `.d64` in [VICE](https://vice-emu.sourceforge.io) (`x64sc`)
or on a real Commodore 64 with a 1541 or an SD2IEC.

cc65 and VICE are tools only; neither is part of this project. No Commodore ROM
is in this repository or its releases. See [NOTICE](NOTICE).

## Files

| | |
|---|---|
| `src/model.h` | the part in memory, shared by everything |
| `src/sc64.cfg` | the linker's memory map ([docs/MEMORY.md](docs/MEMORY.md)) |
| `docs/FORMAT.md` | the `.SC64` part file |
| `tools/ci/` | the CI scripts and a local test image |

MIT License. See [LICENSE](LICENSE).
