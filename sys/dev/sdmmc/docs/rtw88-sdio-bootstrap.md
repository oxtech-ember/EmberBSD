# RTL8723DS native bootstrap

`rtwsdio` attaches to CIS `024c:d723`, function 1.
It provides firmware bootstrap states `ATTACHED`, `LOADING`, `FW_READY`,
`STOPPING` and `DETACHED`. It does not create an `ifnet`.

## Admission and ownership

Native attachment requires an enabled, non-removable FDT host with a registered
standard `mmc-pwrseq`. Diagnostic root and host markers are excluded.
Board DTS owns power resources, polarity and delays.
The YS-M33 native MMC1 node remains disabled until its board profile is verified.
This driver does not change GPIOs, regulators or device-tree properties.

The root-mount callback schedules the SD/MMC task; it does not perform bus I/O.
The task owns bootstrap, firmware bytes and synchronous chip operations.
The driver interlock protects stopping, state and task submission.
No bus operation runs under that interlock or in the interrupt callback.
CMD52 is used during power transitions and for narrow or unaligned registers.
CMD53 is enabled only after function readiness, checked block negotiation
and successful chip power-on. Host and card must support 512-byte blocks.
The checked CIS query limits parsing to 256 tuples and 4096 bytes.
Malformed or duplicate function-extension tuples prevent admission.
The encoded command argument reaches the native host without re-encoding.
The normal host bounce path handles data with a null DMA map.

The driver keeps firmware bytes alive through the final window transfer.
It uses `firmware_open("rtw88", "rtw8723d_fw.bin", ...)` and the Ports producer.
The production parser checks the 32-byte legacy header and bounded payload.
No download recipe or firmware binary is embedded in this driver.

## Interrupts and stopping

The common SD/MMC layer owns shared host interrupt masking and acknowledgement.
The driver callback only queues work. Worker code accesses HIMR and HISR.
This bootstrap leaves operational RX/TX interrupts masked.
It handles no operational status bits and performs no W1C on those bits.

Stop first prevents root/IRQ submission and firmware continuation.
It cancels and drains the root callback, then removes queued task instances
and waits for any running instance. It holds no callback-needed lock while waiting.
Cleanup masks the chip, releases IRQ routing, restores owned block settings
and releases only its function-enable bit. Firmware and callback state outlive
the last user. Cleanup uses fresh register-I/O contexts and preserves the first error.
Repeated stop succeeds without repeating resource release.

## Checks and limits

`ember/tools/rtw88-sdio-lifecycle-contract.py SOURCE OUTPUT` executes the
production attachment, transport and firmware parser with modeled host services.
Chip bootstrap routines are substituted at their boundary for lifecycle fault
and cancellation tests. The separate RTL8723D boot contract tests those real routines.
The lifecycle tests cover admission, command errors, uncertain writes, block
limits, firmware lifetime, IRQ deferral, queued and running detach, and repeated stop.
Select SDK Python and `HOST_CC` explicitly; the output directory must not exist.

Host tests do not establish target execution or physical SDIO/IRQ operation.
Target objects do not establish a linked kernel or device firmware readiness.
WLAN TX/RX, MAC/PHY calibration, net80211 and board hardware acceptance remain separate.
