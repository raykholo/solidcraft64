"""Write a SolidCraft 2000 .sc2 part (version 7), for the tests and for the
Flange sample, which SolidCraft 2000 doesn't ship as a .sc2.

The layout is rust/sc2000-core/src/file.rs in github.com/raykholo/solidcraft2000."""
import struct

RECT, CIRCLE, EXTRUDE, REVOLVE, HOLE, FILLET = 1, 2, 3, 4, 5, 6


def feature(kind, name="", x=0.0, y=0.0, z=0.0, p1=0.0, p2=0.0, ref=0, supp=0, p3=0.0, res=(0.0, 0.0, 0.0)):
    return dict(kind=kind, name=name, x=x, y=y, z=z, p1=p1, p2=p2, ref=ref, supp=supp, p3=p3, res=res)


def _text(s, n):
    return s.encode("latin-1")[:n].ljust(n, b" ")


def pack_sc2(title, feats, version=7, magic=b"SC2K"):
    out = magic + struct.pack("<h", version) + _text(title, 64) + struct.pack("<h", len(feats))
    blank = feature(0)
    for i in range(64):
        f = feats[i] if i < len(feats) else blank
        out += struct.pack("<h", f["kind"]) + _text(f["name"], 24)
        out += struct.pack("<5d", f["x"], f["y"], f["z"], f["p1"], f["p2"])
        out += struct.pack("<2h", f["ref"], f["supp"])
        out += struct.pack("<d", f["p3"]) + _text("", 32) + struct.pack("<3d", *f["res"])
    return out


def flange():
    """A 60 mm disc, 5 mm thick, with a 16 mm hole in the middle and four 5 mm
    holes on a 44 mm bolt circle: one Hole feature with a pattern of four."""
    return pack_sc2("Flange", [
        feature(CIRCLE, "Sketch1", p1=30.0),
        feature(EXTRUDE, "Extrude1", p1=5.0, ref=1),
        feature(HOLE, "Hole1", p1=16.0, ref=2),
        feature(HOLE, "Hole2", p1=5.0, ref=2, p3=4, res=(44.0, 0.0, 0.0)),
    ])


if __name__ == "__main__":
    import sys
    sys.stdout.buffer.write(flange())
