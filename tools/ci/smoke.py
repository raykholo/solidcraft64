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
        step("RUN", keys="RUN\r")
        time.sleep(3)
        shot("running")
        # Steps for the splash, the menus, the wireframe and the files are added
        # as each lane lands: see the git log.
    finally:
        m.quit()
        try:
            vice.wait(timeout=30)
        except subprocess.TimeoutExpired:
            vice.kill()
    print("smoke test passed")


if __name__ == "__main__":
    main()
