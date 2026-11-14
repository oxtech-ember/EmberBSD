#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compare a QEMU screendump (PPM) with the test pattern of drm2-kms-client.

  kms-shot-compare.py FRAME SHOT.ppm      FRAME is 0 or 1: compare every pixel and
                                          the control points with the pattern
  kms-shot-compare.py --differ A.ppm B.ppm  the two snapshots must differ
  kms-shot-compare.py --same A.ppm B.ppm    the two snapshots must be equal

The pattern is pattern_px() of drm2-kms-client.c, mirrored here.  Exit 0 if all
comparisons hold.
"""
import sys

MARK = 16
BARS = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
        (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]
CORNERS = [
    [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)],
    [(0, 0, 255), (255, 255, 0), (255, 0, 255), (0, 0, 0)],
]


def pattern_px(frame, x, y, W, H):
    if (x < MARK or x >= W - MARK) and (y < MARK or y >= H - MARK):
        c = (2 if y >= H - MARK else 0) + (1 if x >= W - MARK else 0)
        return CORNERS[frame][c]
    if y < (H * 3) // 4:
        k = (x * 8) // W
        if frame:
            k = 7 - k
        return BARS[k]
    k = (x * 255) // (W - 1)
    if frame:
        k = 255 - k
    return (k, k, k)


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    # header: P6 <ws> W <ws> H <ws> maxval <single ws> data (comments allowed)
    pos = 0
    toks = []
    while len(toks) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        toks.append(data[start:pos])
    pos += 1
    if toks[0] != b"P6" or toks[3] != b"255":
        raise SystemExit("not a P6 255 ppm: %r" % toks)
    W, H = int(toks[1]), int(toks[2])
    pix = data[pos:pos + W * H * 3]
    if len(pix) != W * H * 3:
        raise SystemExit("short ppm")
    return W, H, pix


def control_points(W, H):
    pts = set()
    # the four markers: the pixel on each side of every edge
    for cx in (0, W - MARK):
        for cy in (0, H - MARK):
            for dx in (0, MARK - 1):
                for dy in (0, MARK - 1):
                    pts.add((cx + dx, cy + dy))
    for x in (MARK, W - MARK - 1):
        for y in (0, H - 1):
            pts.add((x, y))
    for y in (MARK, H - MARK - 1):
        for x in (0, W - 1):
            pts.add((x, y))
    # both sides of every bar boundary, in the middle of the bar region
    ymid = (H * 3) // 8
    for b in range(1, 8):
        bx = (b * W + 7) // 8       # first x with (x * 8) // W == b
        pts.add((bx - 1, ymid))
        pts.add((bx, ymid))
    # the last row of the bars and the first row of the gradient
    yb = (H * 3) // 4
    for x in (W // 3, 2 * W // 3):
        pts.add((x, yb - 1))
        pts.add((x, yb))
    # the gradient: left, right, and a few in between
    for x in (0, 1, 2, W // 4, W // 2, 3 * W // 4, W - 2, W - 1):
        pts.add((x, H - 1 - MARK - 3))
    # middle of every bar
    for b in range(8):
        pts.add((((2 * b + 1) * W) // 16, ymid))
    return sorted(pts)


def compare(frame, path):
    W, H, pix = read_ppm(path)
    bad = 0
    first = None
    for y in range(H):
        row = pix[y * W * 3:(y + 1) * W * 3]
        for x in range(W):
            exp = pattern_px(frame, x, y, W, H)
            got = (row[3 * x], row[3 * x + 1], row[3 * x + 2])
            if got != exp:
                bad += 1
                if first is None:
                    first = (x, y, exp, got)
    cp = control_points(W, H)
    cbad = 0
    for (x, y) in cp:
        exp = pattern_px(frame, x, y, W, H)
        o = (y * W + x) * 3
        got = (pix[o], pix[o + 1], pix[o + 2])
        if got != exp:
            cbad += 1
            print("  control point (%d,%d): expected %s got %s" % (x, y, exp, got))
    print("%s: %dx%d pattern %d: %d control points, %d mismatches; %d pixels, %d mismatches%s" % (
        path, W, H, frame, len(cp), cbad, W * H, bad,
        "" if first is None else "; first at (%d,%d) expected %s got %s" % first))
    return cbad == 0 and bad == 0


def diff_count(a, b):
    Wa, Ha, pa = read_ppm(a)
    Wb, Hb, pb = read_ppm(b)
    if (Wa, Ha) != (Wb, Hb):
        return None
    return sum(1 for i in range(0, len(pa), 3) if pa[i:i + 3] != pb[i:i + 3])


def main():
    if len(sys.argv) == 4 and sys.argv[1] in ("--differ", "--same"):
        n = diff_count(sys.argv[2], sys.argv[3])
        if n is None:
            print("different sizes")
            return 1
        print("%s vs %s: %d pixels differ" % (sys.argv[2], sys.argv[3], n))
        return 0 if (n > 0) == (sys.argv[1] == "--differ") else 1
    if len(sys.argv) == 3:
        return 0 if compare(int(sys.argv[1]), sys.argv[2]) else 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
