#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check that unsupported 64-bpp integer formats stay out of KMS lists."""

from pathlib import Path
import re
import sys


def fail(message):
    print(message, file=sys.stderr)
    raise SystemExit(1)


def block_after(source, marker):
    start = source.find(marker)
    if start < 0:
        fail(f"Missing production declaration: {marker}")
    opening = source.find("{", start + len(marker) - 1)
    if opening < 0:
        fail(f"Missing block for production declaration: {marker}")
    depth = 0
    for offset in range(opening, len(source)):
        if source[offset] == "{":
            depth += 1
        elif source[offset] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : offset]
    fail(f"Unclosed production block: {marker}")


root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
display = root / "sys/external/bsd/drm2/dist/drm/amd/display"
dm_path = display / "amdgpu_dm/amdgpu_dm.c"
resource_path = display / "dc/core/amdgpu_dc_resource.c"
dm_source = dm_path.read_text(encoding="utf-8")
resource_source = resource_path.read_text(encoding="utf-8")

rgb_formats = block_after(dm_source, "static const uint32_t rgb_formats[] =")
advertised = set(re.findall(r"\bDRM_FORMAT_[A-Z0-9_]+\b", rgb_formats))
unsupported = {
    "DRM_FORMAT_XRGB16161616",
    "DRM_FORMAT_XBGR16161616",
    "DRM_FORMAT_ARGB16161616",
    "DRM_FORMAT_ABGR16161616",
}
if advertised & unsupported:
    fail(f"Unsupported integer 16-bit formats are advertised: {sorted(advertised & unsupported)}")

retained_formats = {
    "DRM_FORMAT_XRGB8888",
    "DRM_FORMAT_ARGB8888",
    "DRM_FORMAT_XBGR8888",
    "DRM_FORMAT_ABGR8888",
    "DRM_FORMAT_RGB565",
    "DRM_FORMAT_XRGB2101010",
    "DRM_FORMAT_XBGR2101010",
    "DRM_FORMAT_ARGB2101010",
    "DRM_FORMAT_ABGR2101010",
}
if not retained_formats <= advertised:
    fail(f"Existing RGB formats were removed: {sorted(retained_formats - advertised)}")

converter = block_after(
    dm_source,
    "fill_dc_plane_info_and_addr(struct amdgpu_device *adev,",
)
if any(fmt in converter for fmt in unsupported):
    fail("Unsupported integer 16-bit formats remain in the DRM plane converter")

dal_converter = block_after(
    resource_source,
    "convert_pixel_format_to_dalsurface(",
)
float16 = re.search(
    r"case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:\s*"
    r"case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:\s*"
    r"dal_pixel_format = PIXEL_FORMAT_FP16;",
    dal_converter,
)
if not float16:
    fail("Existing float16 DAL conversion must remain supported")

integer16_internal = re.compile(r"\bSURFACE_PIXEL_FORMAT_GRPH_ABGR16161616\b")
for path in display.rglob("*"):
    if path.suffix not in {".c", ".h"}:
        continue
    if integer16_internal.search(path.read_text(encoding="utf-8")):
        fail(f"Orphaned integer 16-bit surface format remains in {path.relative_to(root)}")

print("AMDGPU display format advertisement contract: PASS")
