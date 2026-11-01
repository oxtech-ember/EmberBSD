# SC101IOT sensor identification checks

The SC101IOT support is an optional I2C sensor identity probe. It reads the
two chip-ID bytes through page register `0xf0` and leaves the sensor in reset
and power-down. It does not implement DVP capture. The shared camera supply
is enabled but never disabled by a sensor probe. The module provides external
XCLK; the OS waits 20 ms before SCCB but does not declare or verify the clock
frequency.

Run the actual logic, GPIO callbacks, I2C helper and resource helper with the
native Ports Clang and isolated output directories:

```sh
prefix=${HOST_PREFIX:-/path/to/host-pkg-llvm}
"$prefix/bin/python3.15" sys/dev/i2c/tests/t_camera_sensor_power.py \
  --cc "$prefix/bin/clang" \
  --out-dir /tmp/emberbsd-camera-sensor-power
"$prefix/bin/python3.15" sys/dev/i2c/tests/t_sc101iot.py \
  --cc "$prefix/bin/clang" \
  --out-dir /tmp/emberbsd-sc101iot
"$prefix/bin/python3.15" sys/dev/i2c/tests/t_ov3640.py \
  --cc "$prefix/bin/clang" \
  --out-dir /tmp/emberbsd-ov3640
```

The tests cover the page-select/read transaction order, the `0xda4a` ID,
unknown IDs, bus errors on either ID byte, reset/PWDN ordering, active-low
GPIO semantics and safe failure state. Sanitizers are enabled by the runners.
