# SC101IOT sensor identification checks

The SC101IOT support is an optional I2C sensor identity probe. It reads the
two chip-ID bytes through page register `0xf0` and leaves the sensor in reset
and power-down. It does not implement DVP capture. The shared camera supply
is enabled but never disabled by a sensor probe. The module provides external
XCLK; the OS waits 20 ms before SCCB but does not declare or verify the clock
frequency.

The named Mosaico SC101IOT variant uses the optional standard `flash-gpios`
resource. The common camera power helper sets it logically inactive before
sensor controls or supply and keeps ownership while the sensor is powered.
On acquisition or power-on failure and sensor identification failure, it
writes inactive before releasing the GPIO. Successful probes retain the flash
GPIO with the sensor power controls for the device lifetime. For this Mosaico
module, the active-low open-drain descriptor and module pull-up keep GPIO34
inactive when released. The shared supply is never disabled. The GPIO lifecycle
has host regression and target-compile coverage; physical flash behavior has
not been verified. This controls the OS sensor probe phase only and does not
prevent a ROM or earlier boot-stage flash pulse.

Run the actual logic, driver initialization/error cleanup, GPIO callbacks,
I2C helper and resource helper with native Ports Clang and isolated output
directories:

```sh
prefix=${HOST_PREFIX:-/path/to/host-pkg-llvm}
"$prefix/bin/python3.15" sys/arch/riscv/tests/t_esp32s31_gpio_usb.py \
  --cc "$prefix/bin/clang" \
  --out-dir /tmp/emberbsd-s31-gpio-usb
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
GPIO semantics and safe failure state. The S31 GPIO contract additionally
checks USB-pad handoff on GPIO33/34, preservation of unrelated register bits,
unaffected GPIO pads, and rejection before mutation when the named resource
is absent. SC101IOT tests exercise real initialization failures to verify
flash ownership cleanup, and successful identification retains ownership.
Sanitizers are enabled by the runners.
