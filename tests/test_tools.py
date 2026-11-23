"""Tests for tools/mkd64.py and tools/convert.py:  python3 -m unittest discover -s tests"""
import io
import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import convert  # noqa: E402
import mkd64  # noqa: E402
import sc2  # noqa: E402

SAMPLES = os.path.join(ROOT, "samples")


def sample(name):
    return open(os.path.join(SAMPLES, "in", name), "rb").read()


def run(data):
    w = []
    name, feats = convert.convert(convert.read_sc(data), w.append)
    return name, feats, w


class D64(unittest.TestCase):
    def test_names(self):
        self.assertEqual(mkd64.c64_name("Mounting Plate.sc98"), "MOUNTING PL.SC64")
        self.assertEqual(mkd64.c64_name("/x/y/flange.SC64"), "FLANGE.SC64")
        self.assertEqual(mkd64.c64_name("Plate #2 (v1).sc2"), "PLATE 2 V1.SC64")
        self.assertEqual(mkd64.c64_name("....sc2"), "PART.SC64")
        self.assertLessEqual(len(mkd64.c64_name("a" * 40)), 16)

    def test_round_trip(self):
        files = [("SOLIDCRAFT", "PRG", b"\x01\x08" + bytes(range(256)) * 9),
                 ("A.SC64", "SEQ", b"x" * 254), ("B.SC64", "SEQ", b"y" * 255), ("C.SC64", "SEQ", b"")]
        disk, back = mkd64.read_d64(mkd64.build_d64(files))
        self.assertEqual(disk, "SOLIDCRAFT 64")
        self.assertEqual([(n, t, d) for n, t, d in back], files)

    def test_layout(self):
        img = mkd64.build_d64([("SOLIDCRAFT", "PRG", b"\x01\x08abc")])
        self.assertEqual(len(img), 174848)
        bam = mkd64.offset(18, 0)
        self.assertEqual(img[bam:bam + 3], bytes([18, 1, 0x41]))
        self.assertEqual(img[bam + 0xA2:bam + 0xA4], b"SC")
        # 683 sectors, 19 on the directory track: the BAM and the directory, plus one file block.
        total = sum(img[bam + 4 * t] for t in range(1, 36))
        self.assertEqual(total, 683 - 2 - 1)

    def test_many_files_need_a_second_directory_sector(self):
        files = [("F%02d.SC64" % i, "SEQ", b"z" * 10) for i in range(20)]
        _, back = mkd64.read_d64(mkd64.build_d64(files))
        self.assertEqual(len(back), 20)

    def test_errors(self):
        with self.assertRaises(mkd64.D64Error):
            mkd64.build_d64([("A", "SEQ", b""), ("A", "SEQ", b"")])
        with self.assertRaises(mkd64.D64Error):
            mkd64.build_d64([("TOO LONG A NAME X", "SEQ", b"")])
        with self.assertRaises(mkd64.D64Error):
            mkd64.build_d64([("BIG", "SEQ", b"q" * 200000)])


class Convert(unittest.TestCase):
    def test_mounting_plate(self):
        name, feats, w = run(sample("Mounting Plate.sc98"))
        self.assertEqual(name, "MOUNTING PLATE")
        self.assertEqual(feats[0], ("RECT", -400, -250, 800, 500))
        self.assertEqual(feats[1], ("EXTRUDE", 0, 100, 0))
        self.assertEqual(feats[2], ("CIRCLE", -320, -170, 50))
        self.assertEqual(feats[3], ("CUT", 2, 100, 100))
        self.assertEqual(feats[-2:], [("CIRCLE", 0, 0, 100), ("CUT", 12, 250, 250)])   # through the boss and the plate
        self.assertEqual(len(feats), 14)

    def test_fillets_are_dropped_in_the_family_voice(self):
        name, feats, w = run(sample("Filleted Plate.sc2"))
        self.assertIn("Fillet1: SolidCraft 64 doesn't do fillets. The corners are sharp, as in 1982.", w)
        self.assertIn("Fillet2: SolidCraft 64 doesn't do fillets. The corners are sharp, as in 1982.", w)
        self.assertEqual(feats[1], ("EXTRUDE", 0, 60, 0))

    def test_flange_pattern_is_expanded(self):
        name, feats, w = run(sample("Flange.sc2"))
        self.assertEqual(w, [])
        self.assertEqual(feats[0], ("CIRCLE", 0, 0, 600))
        circles = [f for f in feats if f[0] == "CIRCLE"]
        self.assertEqual(circles[2:], [("CIRCLE", 220, 0, 50), ("CIRCLE", 0, 220, 50),
                                       ("CIRCLE", -220, 0, 50), ("CIRCLE", 0, -220, 50)])

    def test_the_samples_are_what_the_converter_makes(self):
        for src, dst in (("Mounting Plate.sc98", "MOUNTING PL.SC64"), ("Filleted Plate.sc2", "FILLETED PL.SC64"),
                         ("Flange.sc2", "FLANGE.SC64")):
            name, feats, _ = run(sample(src))
            self.assertEqual(convert.format_sc64(name, feats), open(os.path.join(SAMPLES, dst), "rb").read())

    def test_sc64_reader_and_writer_agree(self):
        name, feats, _ = run(sample("Mounting Plate.sc98"))
        data = convert.format_sc64(name, feats)
        self.assertTrue(data.startswith(b"SC64 1\rNAME MOUNTING PLATE\r"))
        self.assertTrue(data.endswith(b"END\r"))
        self.assertNotIn(b"\n", data)
        self.assertEqual(convert.read_sc64(data), (name, feats))

    def test_sc64_reader_refuses_bad_parts(self):
        for bad in (b"HELLO\r", b"SC64 2\rNAME X\rEND\r", b"SC64 1\rNAME X\rRECT 1 2 3\rEND\r",
                    b"SC64 1\rNAME X\rRECT 0 0 1 1\r", b"SC64 1\rNAME X\rEXTRUDE 0 1 1\rEND\r",
                    b"SC64 1\rNAME X\rRECT 0 0 99999 1\rEND\r", b"SC64 1\rNAME X\r" + b"CIRCLE 0 0 1\r" * 17 + b"END\r"):
            with self.assertRaises(convert.ConvertError, msg=bad):
                convert.read_sc64(bad)

    def test_too_many_features(self):
        feats = [sc2.feature(sc2.CIRCLE, "S1", p1=50), sc2.feature(sc2.EXTRUDE, "E1", p1=5, ref=1)]
        # 12 holes: 24 features, so the 8-hole pattern (16) does not fit after the first 2
        feats.append(sc2.feature(sc2.HOLE, "Hole1", p1=2, ref=2, p3=12, res=(60, 0, 0)))
        name, out, w = run(sc2.pack_sc2("Many", feats))
        self.assertEqual(len(out), 2)
        self.assertTrue(any(x.startswith("Hole1: 12 holes would need 24 features") for x in w), w)

    def test_revolve_and_sheet_metal_are_left_out(self):
        feats = [sc2.feature(sc2.RECT, "S1", p1=10, p2=10), sc2.feature(sc2.EXTRUDE, "E1", p1=5, ref=1),
                 sc2.feature(sc2.RECT, "S2", x=20, p1=10, p2=10), sc2.feature(sc2.REVOLVE, "Revolve1", p1=360, ref=3),
                 sc2.feature(20, "BaseFlange1")]
        name, out, w = run(sc2.pack_sc2("X", feats))
        self.assertEqual(out, [("RECT", 0, 0, 100, 100), ("EXTRUDE", 0, 50, 0)])
        self.assertEqual(len(w), 2)
        self.assertTrue(w[0].startswith("Revolve1: SolidCraft 64 doesn't do revolves"))

    def test_sketch_on_a_face_and_negative_extrude(self):
        feats = [sc2.feature(sc2.RECT, "S1", p1=20, p2=10), sc2.feature(sc2.EXTRUDE, "E1", p1=-4, ref=1),
                 sc2.feature(sc2.CIRCLE, "S2", x=5, y=5, z=0, p1=2, ref=2), sc2.feature(sc2.EXTRUDE, "E2", p1=3, ref=3)]
        name, out, w = run(sc2.pack_sc2("Neg", feats))
        self.assertEqual(out[1], ("EXTRUDE", 0, 40, -40))     # grows from -4 up to 0
        self.assertEqual(out[3], ("EXTRUDE", 2, 30, -40))     # as SolidCraft 2000 does it: on the far end of the extrude
        self.assertEqual(w, [])

    def test_through_all_goes_through_a_stack(self):
        name, out, w = run(sample("Mounting Plate.sc98"))
        self.assertEqual(out[-1][2], 250)

    def test_refusals(self):
        with self.assertRaises(convert.ConvertError):
            convert.read_sc(b"PK\x03\x04" + bytes(100))
        with self.assertRaises(convert.ConvertError):
            convert.read_sc(b"NOPE" + bytes(100))
        with self.assertRaises(convert.ConvertError):
            convert.read_sc(sc2.pack_sc2("x", [], version=9))
        with self.assertRaises(convert.ConvertError):
            convert.read_sc(sc2.pack_sc2("x", [])[:100])

    def test_old_versions(self):
        # version 3 and 4 parts have shorter records
        def old(version):
            out = b"SC2K" + struct.pack("<h", version) + b"Old".ljust(64) + struct.pack("<h", 2)
            for i in range(64):
                kind = [0, 2, 3][i] if i < 3 else 0
                x = struct.pack("<h", kind) + b" " * 24 + struct.pack("<5d", 0, 0, 0, 30 if kind == 2 else 5, 0)
                x += struct.pack("<2h", 1 if kind == 3 else 0, 0)
                if version >= 4:
                    x += struct.pack("<d", 0.0)
                if version >= 5:
                    x += b" " * 32
                if version >= 4:
                    x += struct.pack("<3d", 0, 0, 0)
                out += x
            return out
        # count is 2: record 0 is kind 0, so use kinds 2, 3 at records 0, 1
        for v in (3, 4, 5):
            data = old(v)
            part = convert.read_sc(data)
            self.assertEqual(part["version"], v)


class Command(unittest.TestCase):
    def test_writes_files_and_a_disk(self):
        with tempfile.TemporaryDirectory() as d:
            disk = os.path.join(d, "x.d64")
            rc = convert.main(["convert.py", os.path.join(SAMPLES, "in", "Flange.sc2"),
                               os.path.join(SAMPLES, "in", "Mounting Plate.sc98"), "-d", d, "-o", disk])
            self.assertEqual(rc, 0)
            self.assertTrue(os.path.exists(os.path.join(d, "FLANGE.SC64")))
            _, files = mkd64.read_d64(open(disk, "rb").read())
            self.assertEqual([f[0] for f in files], ["FLANGE.SC64", "MOUNTING PL.SC64"])

    def test_a_bad_file_is_an_error(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "x.sc2")
            open(p, "wb").write(b"junk")
            self.assertEqual(convert.main(["convert.py", p, "-d", d]), 1)


if __name__ == "__main__":
    unittest.main()
