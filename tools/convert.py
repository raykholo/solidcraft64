#!/usr/bin/env python3
"""convert: bring SolidCraft 98 and SolidCraft 2000 parts over to SolidCraft 64.

    convert.py PART.sc98 PART.sc2 ... [-d DIR] [-o DISK.d64 [--prg solidcraft.prg]]

Reads .sc98 and .sc2 parts (the files are documented by the SolidCraft 2000
source, github.com/raykholo/solidcraft2000: rust/sc2000-core/src/file.rs) and
writes .SC64 parts (docs/FORMAT.md): into DIR (default: here), and/or
straight onto a new .d64 disk image.

What comes over: rectangle and circle sketches, extrudes, and holes (which
become a circle and a CUT each, a pattern one for every hole when they all
fit). What doesn't is left out with a line on stderr. SolidCraft 64 holds 16
features, so a part that needs more loses its last ones.

The .solidcraft files of the real SolidCraft are a zip of constraint-solved
sketches; they are not read.

Plain Python, standard library only.
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkd64  # noqa: E402

MAX_FEATURES = 16
NAME_LEN = 16
HEADER_BYTES = 72
RECORD_BYTES = 134

K_NONE, K_RECT, K_CIRCLE, K_EXTRUDE, K_REVOLVE, K_HOLE, K_FILLET, K_CHAMFER, K_FOREIGN = 0, 1, 2, 3, 4, 5, 6, 7, 9


class ConvertError(Exception):
    pass


# ---- reading SolidCraft 98 and 2000 ----

def _text(b):
    return bytes(b).rstrip(b"\0 ").decode("latin-1")


def read_sc(data):
    """A part from .sc98 / .sc2 bytes: {'title', 'magic', 'version', 'features': [dict]}."""
    if len(data) < HEADER_BYTES:
        raise ConvertError("too short to be a SolidCraft part")
    magic = data[:4]
    if magic not in (b"SC98", b"SC2K"):
        if data[:2] == b"PK":
            raise ConvertError("this is a .solidcraft file (a zip of sketches); SolidCraft 64 reads .sc98 and .sc2")
        raise ConvertError("not a SolidCraft 98 or 2000 part")
    version = struct.unpack_from("<h", data, 4)[0]
    top = 5 if magic == b"SC98" else 7
    if version > top:
        raise ConvertError("a version %d part; this converter knows up to %d" % (version, top))
    if version < 3:
        raise ConvertError("a version %d part is too old (3 and up)" % version)
    title = _text(data[6:70])
    count = struct.unpack_from("<h", data, 70)[0]
    if not 0 <= count <= 64:
        count = 0
    at = HEADER_BYTES
    feats = []
    for i in range(64):
        f = {}
        end = at + 2 + 24 + 5 * 8 + 4
        if end > len(data):
            raise ConvertError("cut short")
        f["kind"] = struct.unpack_from("<h", data, at)[0]
        f["name"] = _text(data[at + 2:at + 26])
        f["x"], f["y"], f["z"], f["p1"], f["p2"] = struct.unpack_from("<5d", data, at + 26)
        f["ref"], f["supp"] = struct.unpack_from("<2h", data, at + 66)
        at = end
        f["p3"], f["info"], f["res"] = 0.0, "", (0.0, 0.0, 0.0)
        if version >= 4:
            f["p3"] = struct.unpack_from("<d", data, at)[0]
            at += 8
        if version >= 5:
            f["info"] = _text(data[at:at + 32])
            at += 32
        if version >= 4:
            f["res"] = struct.unpack_from("<3d", data, at)
            at += 24
        nums = [f["x"], f["y"], f["z"], f["p1"], f["p2"], f["p3"]] + list(f["res"])
        if not all(math.isfinite(n) for n in nums):
            raise ConvertError("a number in the part is not a number")
        feats.append(f)
    for i in range(count, 64):
        feats[i]["kind"] = K_NONE
    return {"title": title, "magic": magic, "version": version, "features": feats[:count]}


# ---- geometry, as SolidCraft 2000 does it ----

class Doc:
    """The SolidCraft 2000 part, with the bits of its kernel conversion needs."""

    def __init__(self, part):
        self.f = part["features"]

    def get(self, i):  # one-based
        return self.f[i - 1] if 1 <= i <= len(self.f) else None

    def parent(self, f):
        return f["ref"] if f["ref"] > 0 else None

    def sketch_z(self, i):
        f = self.get(i)
        z = f["z"]
        r = self.parent(f)
        if r and r < i and self.get(r)["kind"] == K_EXTRUDE:
            z += self.extrude_range(r)[1]
        return z

    def extrude_range(self, i):
        f = self.get(i)
        z = self.sketch_z(f["ref"])
        return z, z + f["p1"]

    def contains(self, s, x, y, rr):
        eps = 1e-6
        if s["kind"] == K_CIRCLE:
            return math.hypot(x - s["x"], y - s["y"]) + rr <= s["p1"] + eps
        if (x - rr < s["x"] - eps or x + rr > s["x"] + s["p1"] + eps
                or y - rr < s["y"] - eps or y + rr > s["y"] + s["p2"] + eps):
            return False
        r = s["p3"]
        if r > 0:
            cx = min(max(x, s["x"] + r), s["x"] + s["p1"] - r)
            cy = min(max(y, s["y"] + r), s["y"] + s["p2"] - r)
            if (math.hypot(x - cx, y - cy) + rr > r + eps
                    and (x < s["x"] + r or x > s["x"] + s["p1"] - r)
                    and (y < s["y"] + r or y > s["y"] + s["p2"] - r)):
                return False
        return True

    def hole_span(self, i, x, y, dia, ok):
        """(top, bottom) of the hole at (x, y): it starts on the top of its extrude
        and goes its depth, or through every extrude stacked under it."""
        f = self.get(i)
        r = f["ref"]
        bottom, top = self.extrude_range(r)
        if f["p2"] > 0:
            return top, top - f["p2"]
        moved = True
        while moved:
            moved = False
            for j in range(1, i):
                g = self.get(j)
                if g["kind"] == K_EXTRUDE and not g["supp"] and j != r and ok[j]:
                    b0, b1 = self.extrude_range(j)
                    if (b0 < bottom - 1e-6 and bottom - 1e-6 <= b1 <= top + 1e-6
                            and self.contains(self.get(g["ref"]), x, y, dia / 2)):
                        bottom = b0
                        moved = True
        return top, bottom

    def hole_centres(self, f):
        n = int(f["p3"]) if f["p3"] >= 2 else 1
        dia, spacing, angle = f["res"]
        a0 = math.radians(angle)
        out = []
        for k in range(n):
            if n > 1 and dia > 0:
                a = a0 + 2 * math.pi * k / n
                out.append((f["x"] + dia / 2 * math.cos(a), f["y"] + dia / 2 * math.sin(a)))
            else:
                out.append((f["x"] + k * spacing * math.cos(a0), f["y"] + k * spacing * math.sin(a0)))
        return out


def tenths(mm):
    v = round(mm * 10)
    if not -32768 <= v <= 32767:
        raise OverflowError
    return int(v)


def sc64_name(title):
    return mkd64.c64_stem(title, NAME_LEN) if title.strip() else "PART"


FAMILY = {
    K_REVOLVE: "doesn't do revolves. Nothing turns but the part.",
    K_FILLET: "doesn't do fillets. The corners are sharp, as in 1982.",
    K_CHAMFER: "doesn't do chamfers. Every edge is already a knife edge.",
    K_FOREIGN: "can't read what this was in another SolidCraft.",
    20: "doesn't do sheet metal. It has no bend table, only a block allocation map.",
    21: "doesn't do sheet metal. It has no bend table, only a block allocation map.",
}


def convert(part, warn):
    """Returns (name, [feature tuples]) where a tuple is ('RECT', x, y, w, h),
    ('CIRCLE', cx, cy, d), ('EXTRUDE', sketch, depth, base) or ('CUT', sketch, depth, top).
    `warn(text)` is called once for everything left out."""
    d = Doc(part)
    out = []
    emitted = {}          # SolidCraft sketch id -> index in out
    kept = {}             # SolidCraft extrude id -> True
    ok = {}               # structurally sound extrude
    n = len(d.f)

    def label(i):
        return d.get(i)["name"] or "Feature%d" % i

    def sketch_ok(i):
        s = d.get(i)
        if s is None or s["kind"] not in (K_RECT, K_CIRCLE):
            return "its sketch isn't a rectangle or a circle"
        if s["supp"]:
            return "its sketch is suppressed"
        r = d.parent(s)
        if r:
            if not (r < i and d.get(r)["kind"] == K_EXTRUDE and s["res"][0] == 0 and ok.get(r)):
                return "its sketch sits on a side face or on something SolidCraft 64 left out"
        return None

    def put_sketch(i):
        """Emit sketch i (once); returns its index."""
        if i in emitted:
            return emitted[i]
        s = d.get(i)
        if s["kind"] == K_RECT:
            x, y, w, h = s["x"], s["y"], s["p1"], s["p2"]
            if w < 0:
                x, w = x + w, -w
            if h < 0:
                y, h = y + h, -h
            if s["p3"] > 0:
                warn("%s: the corner radius is dropped. SolidCraft 64's corners are square, like its pixels." % label(i))
            out.append(("RECT", tenths(x), tenths(y), tenths(w), tenths(h)))
        else:
            out.append(("CIRCLE", tenths(s["x"]), tenths(s["y"]), tenths(abs(s["p1"]) * 2)))
        emitted[i] = len(out) - 1
        return emitted[i]

    def room(k):
        return MAX_FEATURES - len(out) >= k

    for i in range(1, n + 1):
        f = d.f[i - 1]
        k = f["kind"]
        if k in (K_RECT, K_CIRCLE, K_NONE):
            continue
        name = label(i)
        if k in FAMILY:
            warn("%s: SolidCraft 64 %s" % (name, FAMILY[k]))
            continue
        if k not in (K_EXTRUDE, K_HOLE):
            warn("%s: SolidCraft 64 doesn't know what this is (kind %d)." % (name, k))
            continue
        if f["supp"]:
            warn("%s: suppressed, so it isn't here." % name)
            continue
        if k == K_EXTRUDE:
            why = sketch_ok(f["ref"]) if d.parent(f) and f["ref"] < i else "it has no sketch"
            if why:
                warn("%s: left out, %s." % (name, why))
                continue
            ok[i] = True
            z0, z1 = d.extrude_range(i)
            if abs(z1 - z0) < 1e-6:
                warn("%s: no depth, so nothing to extrude." % name)
                continue
            need = 1 if f["ref"] in emitted else 2
            if not room(need):
                warn("%s: SolidCraft 64 holds %d features and this would be one more. Left out." % (name, MAX_FEATURES))
                continue
            try:
                sk = put_sketch(f["ref"])
                out.append(("EXTRUDE", sk, tenths(abs(z1 - z0)), tenths(min(z0, z1))))
            except OverflowError:
                warn("%s: bigger than SolidCraft 64's 3276.7 mm. Left out." % name)
                continue
            kept[i] = True
        else:
            r = f["ref"]
            if not (d.parent(f) and r < i and d.get(r)["kind"] == K_EXTRUDE and kept.get(r)):
                warn("%s: the body it is in was left out, so it is too." % name)
                continue
            centres = d.hole_centres(f)
            if not room(2 * len(centres)):
                warn("%s: %d hole%s would need %d features and there is room for %d. Left out."
                     % (name, len(centres), "" if len(centres) == 1 else "s", 2 * len(centres),
                        MAX_FEATURES - len(out)))
                continue
            for (x, y) in centres:
                if not d.contains(d.get(d.get(r)["ref"]), x, y, f["p1"] / 2):
                    warn("%s: a hole is off the face. Left out." % name)
                    break
            else:
                try:
                    for (x, y) in centres:
                        top, bottom = d.hole_span(i, x, y, f["p1"], ok)
                        depth = top - bottom
                        if depth < 1e-6:
                            raise ValueError
                        out.append(("CIRCLE", tenths(x), tenths(y), tenths(abs(f["p1"]))))
                        out.append(("CUT", len(out) - 1, tenths(depth), tenths(top)))
                except OverflowError:
                    warn("%s: bigger than SolidCraft 64's 3276.7 mm. Left out." % name)
                except ValueError:
                    warn("%s: no depth, so nothing to cut." % name)
    return sc64_name(part["title"]), out


# ---- writing and reading .SC64 ----

def format_sc64(name, feats):
    lines = ["SC64 1", "NAME " + name]
    for f in feats:
        lines.append(" ".join([f[0]] + [str(v) for v in f[1:]]))
    lines.append("END")
    return "".join(l + "\r" for l in lines).encode("ascii")


def read_sc64(data):
    """(name, [feature tuples]) from .SC64 bytes. Strict, like the C64's reader."""
    lines = data.decode("ascii").replace("\n", "").split("\r")
    if lines and lines[-1] == "":
        lines.pop()
    if not lines or lines[0] != "SC64 1":
        raise ConvertError("not a SolidCraft 64 part")
    if len(lines) < 2 or not lines[1].startswith("NAME ") or len(lines[1]) > 5 + NAME_LEN:
        raise ConvertError("bad NAME line")
    nargs = {"RECT": 4, "CIRCLE": 3, "EXTRUDE": 3, "CUT": 3}
    feats = []
    for ln, line in enumerate(lines[2:], 3):
        if line == "END":
            return lines[1][5:], feats
        w = line.split(" ")
        if w[0] not in nargs or len(w) != nargs[w[0]] + 1:
            raise ConvertError("line %d: can't read %r" % (ln, line))
        try:
            v = [int(x) for x in w[1:]]
        except ValueError:
            raise ConvertError("line %d: bad number" % ln)
        if any(not -32768 <= x <= 32767 for x in v):
            raise ConvertError("line %d: number out of range" % ln)
        if w[0] in ("EXTRUDE", "CUT") and not (0 <= v[0] < len(feats) and feats[v[0]][0] in ("RECT", "CIRCLE")):
            raise ConvertError("line %d: not a sketch" % ln)
        if len(feats) >= MAX_FEATURES:
            raise ConvertError("more than %d features" % MAX_FEATURES)
        feats.append((w[0],) + tuple(v))
    raise ConvertError("cut short: no END")


# ---- the command ----

def convert_file(path, warn):
    data = open(path, "rb").read()
    part = read_sc(data)
    return convert(part, warn)


def main(argv):
    inputs, outdir, disk, prg = [], None, None, None
    it = iter(argv[1:])
    for a in it:
        if a == "-d":
            outdir = next(it, None)
        elif a == "-o":
            disk = next(it, None)
        elif a == "--prg":
            prg = next(it, None)
        elif a in ("-h", "--help"):
            print(__doc__)
            return 0
        else:
            inputs.append(a)
    if not inputs:
        sys.stderr.write("usage: convert.py PART.sc98|PART.sc2 ... [-d DIR] [-o DISK.d64 [--prg solidcraft.prg]]\n")
        return 2
    if outdir is None and disk is None:
        outdir = "."
    files, status = [], 0
    for p in inputs:
        stem = os.path.splitext(os.path.basename(p))[0]

        def warn(text, stem=stem):
            sys.stderr.write("%s: %s\n" % (stem, text))
        try:
            name, feats = convert_file(p, warn)
        except (ConvertError, OSError) as e:
            sys.stderr.write("%s: %s\n" % (p, e))
            status = 1
            continue
        if not feats:
            sys.stderr.write("%s: nothing SolidCraft 64 can hold came over, so there is no part.\n" % stem)
            status = 1
            continue
        data = format_sc64(name, feats)
        fn = mkd64.c64_name(stem)
        files.append((fn, data))
        if outdir is not None:
            os.makedirs(outdir, exist_ok=True)
            with open(os.path.join(outdir, fn), "wb") as f:
                f.write(data)
            print("%s -> %s (%d features)" % (p, os.path.join(outdir, fn), len(feats)))
    if disk is not None and files:
        entries = []
        if prg:
            entries.append(("SOLIDCRAFT", "PRG", open(prg, "rb").read()))
        entries += [(n, "SEQ", d) for n, d in files]
        try:
            img = mkd64.build_d64(entries)
        except mkd64.D64Error as e:
            sys.stderr.write("%s: %s\n" % (disk, e))
            return 1
        with open(disk, "wb") as f:
            f.write(img)
        print("%s: %d part%s" % (disk, len(files), "" if len(files) == 1 else "s"))
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv))
