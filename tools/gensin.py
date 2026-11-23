#!/usr/bin/env python3
"""Print the sine table used by src/view.c: 256 steps per turn, amplitude 127."""
import math
for i in range(0, 256, 16):
    print("    " + ", ".join("%d" % round(127 * math.sin(2 * math.pi * (i + k) / 256)) for k in range(16)) + ",")
