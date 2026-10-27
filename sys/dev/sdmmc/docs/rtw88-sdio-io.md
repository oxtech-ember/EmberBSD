# Checked rtw88 SDIO helpers

[`rtw88_sdio_io.c`](../rtw88_sdio_io.c) provides shared CMD52/CMD53 encoding,
R5 error handling, bounded register transfers, and function ownership.
It contains no native attachment, GPIO operations, firmware loader, or WLAN path.
The [diagnostic probe](rtl8723ds-probe.md) continues to perform only its admitted
CMD52 operations. Linking this helper does not enable CMD53 on a device.

The caller serializes the bus, callbacks, function state, and read scratch.
CMD52 permits functions 0 and 1 and rejects addresses outside 17 bits.
Register transfers select LOCAL offsets through `0xfff` or MAC offsets through
`0xffff`; MAC offsets map to the SDIO `0x10000` aperture.
Transfers cannot cross a domain boundary. Initial software limits are 4096 bytes
per transfer and 512 per host request. These limits do not qualify hardware.

CMD53 must be enabled explicitly after function acquisition, readiness, and block
size negotiation. The caller enables it only after the chip permits CMD53.
Block counts are nonzero and bounded by 511; byte count zero encodes 512 bytes.
Read data stays in separate caller-owned scratch until every command succeeds.
Scratch must cover the transfer and must not overlap the destination.
A failed write may have reached the card; the helper does not promise rollback.

Function acquisition records ownership before attempting an enable write.
Adoption of an enabled function never acquires permission to disable it.
Production release rereads IOEx and clears only owned function 1.
The separate diagnostic snapshot policy preserves the original one-shot probe
contract. Retain state after a restore error so the caller can retry cleanup.

Run the production-code model with the selected SDK host tools:

```sh
HOST_CC="$HOST_PREFIX/bin/clang" \
    "$HOST_PREFIX/bin/python3.15" ember/tools/rtw88-sdio-io-contract.py \
    "$PWD" /absolute/new-test-output
```

Also run `rtl8723ds-read-test.sh` and `rtl8723ds-function-test.sh` through the SDK
native-check runner. The read contract needs host libfdt headers and a library.
These contracts model errors and ownership. They do not establish card interrupt
delivery, chip power sequencing, CMD53 behavior, or WLAN on physical hardware.
