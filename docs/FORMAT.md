# The SolidCraft 64 part file

A part is a SEQ file named `NAME.SC64` on the disk, in plain upper-case
ASCII (which is also PETSCII for these characters), one line per record,
each line ending in a carriage return (`$0D`), as the 1541 likes it.

```
SC64 1
NAME MOUNTING PLATE
RECT 0 0 800 500
EXTRUDE 0 60 0
CIRCLE 400 250 100
CUT 2 60 60
END
```

- `SC64 1`: the magic and the format version. A reader refuses any other first line.
- `NAME <text>`: up to 16 characters.
- One line per feature, in order. Numbers are decimal, signed, in tenths of a
  millimetre, separated by single spaces:
  - `RECT x y width height` (lower-left corner)
  - `CIRCLE cx cy diameter`
  - `EXTRUDE sketch depth base` (grows up from z = base)
  - `CUT sketch depth top` (goes down from z = top)

  `sketch` is the zero-based index of an earlier `RECT` or `CIRCLE` line
  among the feature lines.
- `END`: the last line. A file without it was cut short and is refused.

At most 16 features. The feature types match `F_*` in `src/model.h`.

## File names

A Commodore file name is 16 characters, so the disk name is the part's name
with everything but A-Z, 0-9, space and hyphen removed, cut at the first `.`
and to its first 11 characters, trailing spaces dropped, then `.SC64`. (An STL
gets 12 and `.STL`.) `MOUNTING PLATE` is `MOUNTING PL.SC64`; the `NAME` line
inside still says `MOUNTING PLATE`. `tools/mkd64.py` and `src/files.c` apply the
same rule, and the directory lists parts without the `.SC64`.

## STL

`file_stl` writes ASCII STL in millimetres. Each `EXTRUDE` is a `solid PART`
and each `CUT` a `solid CUT` (the tool, not subtracted). A rectangle is a box;
a circle is a 16-sided prism.

## Why text

Because a 1541 is slow enough that the extra bytes don't show, and because
you can read it with `OPEN 2,8,2,"PART,S,R"` and a BASIC loop, which is how
it would have been debugged in 1984.
