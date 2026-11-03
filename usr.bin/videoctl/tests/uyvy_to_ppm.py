#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Convert one explicitly described UYVY frame to binary PPM."""

import argparse
import os
import pathlib

MAX_FRAME = 64 * 1024 * 1024


def clip(value):
    return max(0, min(255, value))


def convert(data, width, height, stride, matrix, value_range):
    if width <= 0 or width % 2 or height <= 0:
        raise ValueError("UYVY requires positive even width and positive height")
    row_bytes = width * 2
    if stride < row_bytes:
        raise ValueError("stride is shorter than a UYVY row")
    frame_bytes = stride * height
    if frame_bytes > MAX_FRAME:
        raise ValueError("frame exceeds 64 MiB limit")
    if len(data) != frame_bytes:
        raise ValueError("input length does not match stride times height")

    output = bytearray()
    for row in range(height):
        line = data[row * stride:row * stride + row_bytes]
        for offset in range(0, row_bytes, 4):
            u, y0, v, y1 = line[offset:offset + 4]
            for y in (y0, y1):
                if value_range == "limited":
                    c = y - 16
                    d, e = u - 128, v - 128
                    if matrix == "601":
                        rgb = (298 * c + 409 * e, 298 * c - 100 * d - 208 * e,
                               298 * c + 516 * d)
                    else:
                        rgb = (298 * c + 459 * e, 298 * c - 55 * d - 136 * e,
                               298 * c + 541 * d)
                    output.extend(clip((component + 128) >> 8) for component in rgb)
                else:
                    d, e = u - 128, v - 128
                    if matrix == "601":
                        rgb = (y + ((359 * e + 128) >> 8),
                               y - ((88 * d + 183 * e + 128) >> 8),
                               y + ((454 * d + 128) >> 8))
                    else:
                        rgb = (y + ((403 * e + 128) >> 8),
                               y - ((48 * d + 120 * e + 128) >> 8),
                               y + ((475 * d + 128) >> 8))
                    output.extend(clip(component) for component in rgb)
    return b"P6\n%d %d\n255\n" % (width, height) + output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument("--stride", type=int, required=True)
    parser.add_argument("--matrix", choices=("601", "709"), required=True)
    parser.add_argument("--range", dest="value_range", choices=("limited", "full"),
                        required=True)
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    with args.input.open("rb") as input_file:
        data = input_file.read(MAX_FRAME + 1)
    if len(data) > MAX_FRAME:
        parser.error("input exceeds 64 MiB limit")
    try:
        ppm = convert(data, args.width, args.height, args.stride,
                      args.matrix, args.value_range)
    except ValueError as error:
        parser.error(str(error))
    fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as output_file:
        output_file.write(ppm)


if __name__ == "__main__":
    main()
