#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check that each Mosaico kernel config includes only its selected sensor."""

import pathlib
import re

CONF_DIR = pathlib.Path(__file__).resolve().parents[1]
SYS_DIR = CONF_DIR.parents[2]
INCLUDE_RE = re.compile(r'^include "([^"]+)"\s*$')
SENSOR_RE = re.compile(r'^(ov3640s|sc101iots)\*?\s+at\s+iic\?\s+addr\s+(0x[0-9a-fA-F]+)\s*$')


def configured_sensors(config, seen=None):
    if seen is None:
        seen = set()
    path = CONF_DIR / config
    if path in seen:
        raise AssertionError(f"configuration include cycle: {path}")
    seen.add(path)
    sensors = set()
    for line in path.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        match = INCLUDE_RE.match(line)
        if match:
            include = SYS_DIR / match.group(1)
            sensors.update(configured_sensors(include, seen.copy()))
            continue
        match = SENSOR_RE.match(line)
        if match:
            sensors.add((match.group(1), int(match.group(2), 16)))
    return sensors


expected = {
    "ESP_MOSAICO": set(),
    "ESP_MOSAICO_CAMERA": {("ov3640s", 0x3C)},
    "ESP_MOSAICO_SC101IOT": {("sc101iots", 0x68)},
}
for config, selected in expected.items():
    actual = configured_sensors(config)
    if actual != selected:
        raise SystemExit(
            f"{config}: expected sensor attachments {sorted(selected)}, got {sorted(actual)}"
        )

print("Mosaico sensor attachment selection passed")
