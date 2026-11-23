"""smoke.py - boot a C64 in VICE, load SolidCraft 64 off the .d64, and drive it.

    python3 tools/ci/smoke.py build/solidcraft.d64 build/shots

Runs x64sc under Xvfb (tools/ci/vice-run) with true drive emulation, warp on,
and the binary monitor; every step waits for text on the screen, then saves a
screenshot. Exits non-zero, with the screen's text, when a step doesn't happen.
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vicemon import Vice  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    d64, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    vice = subprocess.Popen(
        [os.path.join(HERE, "vice-run"), "-warp", "-drive8truedrive", "-8", d64,
         "-binarymonitor", "-binarymonitoraddress", "ip4://127.0.0.1:6502"],
        stdout=open(os.path.join(out, "vice.log"), "w"), stderr=subprocess.STDOUT)
    m = Vice()
    n = [0]

    def shot(name):
        n[0] += 1
        path = os.path.join(out, "%02d-%s.png" % (n[0], name))
        m.screenshot(path)
        print("  screenshot", path)

    def step(title, keys=None, wait=None, timeout=120, name=None, below=None):
        print(title)
        if keys:
            m.type(keys)
        if wait:
            m.wait_text(wait, timeout=timeout, below=below)
        if name:
            shot(name)

    try:
        step("The C64 boots", wait="READY.", name="basic")
        t0 = time.time()
        step("LOAD\"SOLIDCRAFT\",8,1", keys='LOAD"SOLIDCRAFT",8,1\r', wait="SEARCHING FOR SOLIDCRAFT")
        step("  ... loading", wait="LOADING", name="loading")
        step("  ... loaded", wait="READY.", below="LOADING", timeout=600, name="loaded")
        print("  loaded in %.0f s of warp" % (time.time() - t0))
        step("RUN: the boot loader asks about the cartridge", keys="RUN\r",
             wait="FAST LOADER CARTRIDGE FITTED", name="cartridge")
        step("N: the slow way, border flashing", keys="N", wait="SEARCHING FOR SC64.BIN")
        time.sleep(1)
        shot("stripes")
        step("The splash", wait="LOADED IN", timeout=900, name="splash")
        step("Any key: the main screen", keys=" ", wait="F1 FILE", name="main")

        step("F3 Sketch > Rectangle, 80 x 50", keys="{F3}", wait="CIRCLE", name="sketch-menu")
        step("  the dialog", keys="\r", wait="WIDTH", name="rect-dialog")
        step("  typed", keys="0\r0\r80\r50\r", wait="1/16 FEATURES")
        step("F3 Sketch > Circle, 20 across", keys="{F3}{DOWN}\r", wait="DIAMETER")
        step("  typed", keys="40\r25\r20\r", wait="2/16 FEATURES")
        step("F5 Features > Extrude the rectangle 6 mm", keys="{F5}\r", wait="SKETCH2")
        step("  picked", keys="\r", wait="BASE Z")
        step("  typed", keys="6\r0\r", wait="3/16 FEATURES")
        step("F5 Features > Cut the circle, through", keys="{F5}{DOWN}\r", wait="SKETCH2")
        step("  picked", keys="{DOWN}\r", wait="TOP Z")
        step("  the defaults: 5 mm down from the top", keys="\r\r", wait="4/16 FEATURES", name="features")

        print("F7 View > Wireframe")
        m.type("{F7}\r")
        time.sleep(4)
        shot("wireframe")
        for i in range(3):
            m.type("{RIGHT}{RIGHT}{UP}")
            time.sleep(4)
            shot("turned-%d" % (i + 1))
        step("RUN/STOP: back", keys="{STOP}", wait="F1 FILE")

        step("F1 File > Save as PLATE", keys="{F1}{DOWN}{DOWN}\r", wait="SAVE AS")
        step("  saved", keys="{DEL}{DEL}{DEL}{DEL}{DEL}PLATE\r", wait="SAVED.", timeout=600, name="saved")
        step("F1 File > Export STL", keys="{F1}{DOWN}{DOWN}{DOWN}\r", wait="EXPORT STL AS")
        step("  written", keys="\r", wait="STL WRITTEN.", timeout=900, name="stl")
        step("F1 File > New", keys="{F1}\r", wait="0/16 FEATURES")
        step("F1 File > Open: the disk's parts", keys="{F1}{DOWN}\r", wait="MOUNTING PL", timeout=600,
             name="open")
        step("  the Mounting Plate, from SolidCraft 98", keys="{DOWN}{DOWN}\r", wait="LOADED.", timeout=600, name="opened")
        print("F7 View > Wireframe")
        m.type("{F7}\r")
        time.sleep(6)
        shot("mounting-plate")
        step("RUN/STOP: back", keys="{STOP}", wait="F1 FILE")
    finally:
        m.quit()
        try:
            vice.wait(timeout=30)
        except subprocess.TimeoutExpired:
            vice.kill()
    check_disk(d64)
    print("smoke test passed")


def check_disk(d64):
    """What the C64 wrote, read back on this side."""
    sys.path.insert(0, os.path.join(HERE, ".."))
    import mkd64
    _, files = mkd64.read_d64(open(d64, "rb").read())
    got = {name: data for name, _, data in files}
    want = (b"SC64 1\rNAME PLATE\rRECT 0 0 800 500\rCIRCLE 400 250 200\r"
            b"EXTRUDE 0 60 0\rCUT 1 50 60\rEND\r")
    part = got.get("PLATE.SC64")
    if part != want:
        sys.exit("PLATE.SC64 on the disk is %r, not %r" % (part, want))
    print("PLATE.SC64 on the disk: as typed")
    stl = got.get("PLATE.STL", b"")
    facets = stl.count(b"facet normal")
    if not stl.startswith(b"solid ") or not stl.rstrip().endswith(b"endsolid cut") or facets < 12:
        sys.exit("PLATE.STL on the disk isn't an STL (%d bytes, %d facets)" % (len(stl), facets))
    print("PLATE.STL on the disk: %d bytes, %d facets" % (len(stl), facets))


if __name__ == "__main__":
    main()
