# Memory map

| Range | What |
|---|---|
| `$0002-$001B` | cc65 zero page |
| `$0801-$BFFF` | program, data, heap, and the C stack at the top (`src/sc64.cfg`). cc65's startup banks BASIC ROM out, so `$A000-$BFFF` is ours. |
| `$C000-$C3E7` | hi-res colour matrix (VIC bank 3, screen at offset `$0000`) |
| `$C400-$CFFF` | free: lane A may use it for tables (sine, row addresses) |
| `$D000-$DFFF` | I/O |
| `$E000-$FF3F` | hi-res bitmap, under the KERNAL ROM |

- The VIC sees RAM under ROM, so the bitmap can sit under the KERNAL.
- The CPU reads ROM there. To read the bitmap (OR a pixel in, say), bank the KERNAL out with `SEI`, `$01 = $35`, do the work, then put `$01` back and `CLI`. Writes go to RAM either way.
- KERNAL calls (file I/O, the keyboard) need `$01` back to its normal value. Never call one while the KERNAL is banked out.
- The text screen stays where the KERNAL keeps it (`$0400`, VIC bank 0) for the splash and dialogs. `view_init()` switches to bank 3 and hi-res, and `view_done()` switches back.
- The 8x8 font for text on the hi-res screen is copied from the character ROM at start-up (bank `$01 = $33` briefly, with interrupts off). No font or ROM data is in the repository.
