#!/usr/bin/env python3
"""mkd64: build a 1541 disk image (.d64, 35 tracks, no error bytes) from files.

    mkd64.py OUT.d64 SOLIDCRAFT.prg [NAME=OTHER.prg ...] [PART.SC64 ...]

The first file goes on the disk as a PRG called SOLIDCRAFT (the boot loader).
NAME=FILE.prg adds another PRG called NAME (SC64.BIN, the program it loads).
Every *.SC64 goes on as a SEQ, named as docs/FORMAT.md says (c64_name below). The disk is
called "SOLIDCRAFT 64", ID "SC".

Plain Python, standard library only. The layout is the 1541's own, which is
documented all over the place: 683 sectors of 256 bytes; the BAM and the
disk name in track 18 sector 0; the directory chained from 18/1, eight
32-byte entries a sector; every file a chain of sectors whose first two
bytes say where the next one is.
"""
import os
import sys

SECTORS = [21] * 17 + [19] * 7 + [18] * 6 + [17] * 5   # tracks 1..35
TRACKS = len(SECTORS)
D64_BYTES = 683 * 256
DIR_TRACK = 18
PRG, SEQ = 0x82, 0x81
DISK_NAME = "SOLIDCRAFT 64"
DISK_ID = "SC"
EXT = ".SC64"


class D64Error(Exception):
    pass


def offset(track, sector):
    """Byte offset of a sector in the image."""
    if not 1 <= track <= TRACKS or not 0 <= sector < SECTORS[track - 1]:
        raise D64Error("no such sector: %d/%d" % (track, sector))
    return (sum(SECTORS[:track - 1]) + sector) * 256


def c64_stem(text, limit):
    """Upper case, A-Z 0-9 space and '-' only, cut at the first '.', at most
    `limit` characters, trailing spaces dropped. Never empty."""
    text = text.upper().split(".")[0]
    text = "".join(c for c in text if c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -")
    text = text[:limit].rstrip()
    return text or "PART"


def c64_name(path):
    """The Commodore file name of a part: the host file's name without its
    extension, as c64_stem, cut to 11 characters so that name + ".SC64" fits
    the 16 a Commodore file name has."""
    base = os.path.basename(path)
    return c64_stem(base, 11) + EXT


class Disk:
    def __init__(self):
        self.img = bytearray(D64_BYTES)
        self.free = [[True] * n for n in SECTORS]
        self.entries = []
        self.alloc(DIR_TRACK, 0)               # the BAM
        self.dir_sectors = [self.alloc(DIR_TRACK, 1)]

    def alloc(self, track, sector):
        if not self.free[track - 1][sector]:
            raise D64Error("sector %d/%d is taken" % (track, sector))
        self.free[track - 1][sector] = False
        return (track, sector)

    def find(self, track, start, interleave):
        """The first free sector on `track` at or after `start`, stepping by `interleave`."""
        n = SECTORS[track - 1]
        s = start % n
        for _ in range(n):
            if self.free[track - 1][s]:
                return s
            s = (s + 1) % n
        return None

    def next_sector(self, last, interleave=10, avoid_dir=True):
        """Pick the next sector for a file, after `last` (track, sector).
        The 1541 fills the tracks nearest the directory first, outwards."""
        order = list(range(DIR_TRACK - 1, 0, -1)) + list(range(DIR_TRACK + 1, TRACKS + 1))
        if last is not None and last[0] != DIR_TRACK:
            s = self.find(last[0], last[1] + interleave, interleave)
            if s is not None:
                return self.alloc(last[0], s)
        for t in order:
            s = self.find(t, 0, interleave)
            if s is not None:
                return self.alloc(t, s)
        raise D64Error("disk full")

    def write_file(self, data):
        """Store data as a chain of sectors; return (first track, first sector, blocks)."""
        chunks = [data[i:i + 254] for i in range(0, len(data), 254)] or [b""]
        last = None
        first = None
        blocks = []
        for _ in chunks:
            ts = self.next_sector(last)
            blocks.append(ts)
            if first is None:
                first = ts
            last = ts
        for i, (ts, chunk) in enumerate(zip(blocks, chunks)):
            o = offset(*ts)
            if i + 1 < len(blocks):
                self.img[o], self.img[o + 1] = blocks[i + 1]
            else:
                self.img[o] = 0
                self.img[o + 1] = len(chunk) + 1     # the index of the last used byte
            self.img[o + 2:o + 2 + len(chunk)] = chunk
        return first[0], first[1], len(blocks)

    def add(self, name, ftype, data):
        raw = name.encode("ascii")
        if len(raw) > 16:
            raise D64Error("%r is longer than 16 characters" % name)
        if any(e[0] == raw for e in self.entries):
            raise D64Error("two files would both be called %r" % name)
        t, s, blocks = self.write_file(data)
        self.entries.append((raw, ftype, t, s, blocks))

    def finish(self):
        # The directory: eight entries a sector, chained, interleave 3.
        for i in range(0, len(self.entries), 8):
            if i // 8 >= len(self.dir_sectors):
                s = self.find(DIR_TRACK, self.dir_sectors[-1][1] + 3, 3)
                if s is None:
                    raise D64Error("directory full")
                self.dir_sectors.append(self.alloc(DIR_TRACK, s))
        for n, ts in enumerate(self.dir_sectors):
            o = offset(*ts)
            if n + 1 < len(self.dir_sectors):
                self.img[o], self.img[o + 1] = self.dir_sectors[n + 1]
            else:
                self.img[o], self.img[o + 1] = 0, 0xFF
            for k, e in enumerate(self.entries[n * 8:n * 8 + 8]):
                raw, ftype, t, s, blocks = e
                p = o + k * 32
                self.img[p + 2] = ftype
                self.img[p + 3], self.img[p + 4] = t, s
                self.img[p + 5:p + 21] = raw.ljust(16, b"\xa0")
                self.img[p + 30] = blocks & 255
                self.img[p + 31] = blocks >> 8
        # The BAM.
        o = offset(DIR_TRACK, 0)
        self.img[o], self.img[o + 1] = DIR_TRACK, 1
        self.img[o + 2] = 0x41                        # DOS version "A"
        for t in range(1, TRACKS + 1):
            bits = [0, 0, 0]
            count = 0
            for s, free in enumerate(self.free[t - 1]):
                if free:
                    bits[s // 8] |= 1 << (s % 8)
                    count += 1
            p = o + 4 * t
            self.img[p] = count
            self.img[p + 1:p + 4] = bytes(bits)
        self.img[o + 0x90:o + 0xA0] = DISK_NAME.encode("ascii").ljust(16, b"\xa0")
        self.img[o + 0xA0:o + 0xA2] = b"\xa0\xa0"
        self.img[o + 0xA2:o + 0xA4] = DISK_ID.encode("ascii")
        self.img[o + 0xA4] = 0xA0
        self.img[o + 0xA5:o + 0xA7] = b"2A"
        self.img[o + 0xA7:o + 0xAB] = b"\xa0" * 4
        return bytes(self.img)


def build_d64(files):
    """files: a list of (c64 name, 'PRG' or 'SEQ', bytes). Returns the image."""
    d = Disk()
    for name, kind, data in files:
        d.add(name, PRG if kind == "PRG" else SEQ, data)
    return d.finish()


def build_from_paths(prg, parts):
    """The Makefile's call: one program, then the parts."""
    files = [("SOLIDCRAFT", "PRG", open(prg, "rb").read())]
    for p in parts:
        if "=" in p and not p.upper().endswith(EXT):
            name, path = p.split("=", 1)
            files.append((name.upper(), "PRG", open(path, "rb").read()))
            continue
        if not p.upper().endswith(EXT):
            raise D64Error("%s is not a .SC64 file" % p)
        files.append((c64_name(p), "SEQ", open(p, "rb").read()))
    return build_d64(files)


def read_d64(img):
    """The reader the tests use: returns (disk name, [(name, type, bytes)]),
    with the names as text. Follows the sector chains, so it checks them."""
    img = bytes(img)
    if len(img) != D64_BYTES:
        raise D64Error("not a 35-track .d64 (%d bytes)" % len(img))
    o = offset(DIR_TRACK, 0)
    disk = img[o + 0x90:o + 0xA0].rstrip(b"\xa0").decode("ascii")
    out = []
    t, s = img[o], img[o + 1]
    seen = set()
    while t:
        if (t, s) in seen:
            raise D64Error("the directory loops")
        seen.add((t, s))
        p = offset(t, s)
        for k in range(8):
            e = img[p + k * 32:p + k * 32 + 32]
            if e[2] == 0:
                continue
            name = e[5:21].rstrip(b"\xa0").decode("ascii")
            data, ft, fs, n = b"", e[3], e[4], 0
            chain = set()
            while ft:
                if (ft, fs) in chain:
                    raise D64Error("%s loops" % name)
                chain.add((ft, fs))
                q = offset(ft, fs)
                nt, ns = img[q], img[q + 1]
                data += img[q + 2:q + 256] if nt else img[q + 2:q + 1 + ns]
                ft, fs = nt, ns
                n += 1
            if n != e[30] + 256 * e[31]:
                raise D64Error("%s: the directory says %d blocks, the chain has %d" % (name, e[30] + 256 * e[31], n))
            out.append((name, {0x82: "PRG", 0x81: "SEQ"}.get(e[2], hex(e[2])), data))
        t, s = img[p], img[p + 1]
    return disk, out


def main(argv):
    if len(argv) < 3:
        sys.exit("usage: mkd64.py OUT.d64 PROGRAM.prg [NAME=OTHER.prg ...] [PART.SC64 ...]")
    try:
        img = build_from_paths(argv[2], argv[3:])
    except D64Error as e:
        sys.exit("mkd64: %s" % e)
    with open(argv[1], "wb") as f:
        f.write(img)
    print("%s: %d files" % (argv[1], len(argv) - 2))


if __name__ == "__main__":
    main(sys.argv)
