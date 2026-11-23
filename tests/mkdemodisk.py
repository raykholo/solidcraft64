#!/usr/bin/env python3
"""The disk for tests/filesdemo.c: the program, the samples, and four bad parts.

    mkdemodisk.py OUT.d64 filesdemo.prg samples/*.SC64
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import mkd64  # noqa: E402

BAD = {
    "BAD1.SC64": b"HELLO\r",                                           # not a part
    "BAD2.SC64": b"SC64 1\rNAME X\rRECT 0 0 10\rEND\r",                # a line it can't read
    "BAD3.SC64": b"SC64 1\rNAME X\rRECT 0 0 10 10\r",                  # no END
    "BAD4.SC64": b"SC64 1\rNAME X\r" + b"CIRCLE 0 0 10\r" * 17 + b"END\r",   # 17 features
}


def main(argv):
    out, prg, parts = argv[1], argv[2], argv[3:]
    files = [("FILESDEMO", "PRG", open(prg, "rb").read())]
    for p in parts:
        files.append((mkd64.c64_name(p), "SEQ", open(p, "rb").read()))
    for name, data in BAD.items():
        files.append((name, "SEQ", data))
    open(out, "wb").write(mkd64.build_d64(files))


if __name__ == "__main__":
    main(sys.argv)
