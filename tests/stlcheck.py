"""A small ASCII STL reader and checker, for the tests."""
import re


def parse_stl(data):
    """[(name, [(normal, (v0, v1, v2))])] from ASCII STL bytes. Strict about the
    keywords being real ASCII lower case, as other programs' readers are."""
    text = data.decode("ascii")
    solids = []
    cur = None
    tri = None
    for raw in text.split("\n"):
        line = raw.strip()
        if not line:
            continue
        w = line.split()
        if w[0] == "solid":
            cur = (" ".join(w[1:]), [])
        elif w[0] == "endsolid":
            solids.append(cur)
            cur = None
        elif w[0] == "facet":
            assert w[1] == "normal" and len(w) == 5, line
            tri = {"n": tuple(float(x) for x in w[2:5]), "v": []}
        elif w[0] == "vertex":
            tri["v"].append(tuple(float(x) for x in w[1:4]))
        elif w[0] == "endfacet":
            assert len(tri["v"]) == 3
            cur[1].append((tri["n"], tuple(tri["v"])))
        elif w[0] in ("outer", "endloop"):
            pass
        else:
            raise AssertionError("unknown STL line %r" % line)
    assert cur is None, "a solid is not closed"
    return solids


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def volume(facets):
    """Signed volume in mm3: positive when the triangles face outward."""
    return sum(dot(v[0], cross(v[1], v[2])) for _, v in facets) / 6.0


def bbox(facets):
    pts = [p for _, v in facets for p in v]
    return tuple(min(p[i] for p in pts) for i in range(3)), tuple(max(p[i] for p in pts) for i in range(3))


def winding_agrees(facets):
    """Every facet's stored normal points the way its winding says."""
    for n, v in facets:
        c = cross(sub(v[1], v[0]), sub(v[2], v[0]))
        if dot(c, n) <= 0:
            return False
    return True


def watertight(facets):
    """Every edge is used once each way: the shell is closed and consistently wound."""
    edges = {}
    for _, v in facets:
        for i in range(3):
            e = (v[i], v[(i + 1) % 3])
            edges[e] = edges.get(e, 0) + 1
    return all(c == 1 and (e[1], e[0]) in edges for e, c in edges.items())
