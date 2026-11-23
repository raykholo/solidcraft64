#!/usr/bin/env python3
"""Check the disk that tests/filesdemo.c wrote in VICE.

    check_filesdemo.py build/filesdemo-run.d64

Reads the image back with mkd64's own reader and checks what the C64 saved:
the new part against the sample it came from, and the STLs' geometry.
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import convert  # noqa: E402
import mkd64  # noqa: E402
import stlcheck  # noqa: E402


def prism_area(r):
    return 0.5 * 16 * r * r * math.sin(2 * math.pi / 16)


def close(a, b, tol=0.01):
    return abs(a - b) <= tol * abs(b)


def check_solids(stl, expect):
    """expect: [(solid name, volume mm3, (w, h, z0, z1) or None)]"""
    solids = stlcheck.parse_stl(stl)
    assert [s[0] for s in solids] == [e[0] for e in expect], [s[0] for s in solids]
    for (name, facets), (_, vol, _) in zip(solids, expect):
        assert stlcheck.winding_agrees(facets), name + ": a normal disagrees with its winding"
        assert stlcheck.watertight(facets), name + ": not a closed shell"
        v = stlcheck.volume(facets)
        assert close(v, vol), "%s: volume %.1f, expected %.1f" % (name, v, vol)
    return solids


def main(path):
    disk, files = mkd64.read_d64(open(path, "rb").read())
    by = {n: (t, d) for n, t, d in files}
    print("disk %r: %s" % (disk, ", ".join(sorted(by))))
    assert disk == "SOLIDCRAFT 64"

    # The save: the Mounting Plate under a new name, and the same bytes otherwise.
    mp = by["MOUNTING PL.SC64"][1]
    new = by["NEW PLATE.SC64"]
    assert new[0] == "SEQ"
    assert new[1] == mp.replace(b"NAME MOUNTING PLATE", b"NAME NEW PLATE"), new[1]
    name, feats = convert.read_sc64(new[1])
    assert name == "NEW PLATE" and len(feats) == 14
    print("NEW PLATE.SC64: %d features, the Mounting Plate under a new name" % len(feats))

    # The Flange, saved as a copy.
    assert by["FLANGE COPY.SC64"][1] == by["FLANGE.SC64"][1]
    print("FLANGE COPY.SC64: the same bytes as FLANGE.SC64")

    # The STL of the Mounting Plate: plate, four holes, boss, centre hole.
    stl = by["NEW PLATE.STL"][1]
    plate = 80 * 50 * 10
    boss = prism_area(10) * 15
    solids = check_solids(stl, [
        ("PART".lower(), plate, None),
        ("cut", prism_area(2.5) * 10, None), ("cut", prism_area(2.5) * 10, None),
        ("cut", prism_area(2.5) * 10, None), ("cut", prism_area(2.5) * 10, None),
        ("part", boss, None),
        ("cut", prism_area(5) * 25, None),
    ])
    lo, hi = stlcheck.bbox(solids[0][1])
    assert lo == (-40.0, -25.0, 0.0) and hi == (40.0, 25.0, 10.0), (lo, hi)
    lo, hi = stlcheck.bbox(solids[5][1])
    assert abs(hi[2] - 25.0) < 1e-6 and abs(lo[2] - 10.0) < 1e-6, (lo, hi)
    n = sum(len(s[1]) for s in solids)
    print("NEW PLATE.STL: %d bytes, %d solids, %d facets, closed, wound outward, volumes right" % (len(stl), len(solids), n))

    # The STL of the Flange: disc, centre hole, four bolt holes.
    stl = by["FLANGE.STL"][1]
    solids = check_solids(stl, [("part", prism_area(30) * 5, None), ("cut", prism_area(8) * 5, None)]
                          + [("cut", prism_area(2.5) * 5, None)] * 4)
    n = sum(len(s[1]) for s in solids)
    print("FLANGE.STL: %d bytes, %d solids, %d facets, closed, wound outward, volumes right" % (len(stl), len(solids), n))

    # The bad parts are untouched.
    for bad in ("BAD1.SC64", "BAD2.SC64", "BAD3.SC64", "BAD4.SC64"):
        assert bad in by
    print("ok")


if __name__ == "__main__":
    main(sys.argv[1])
