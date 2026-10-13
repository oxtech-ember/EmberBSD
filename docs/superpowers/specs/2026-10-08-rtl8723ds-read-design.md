# YS-M33 RTL8723DS read transport

## Intent and acceptance boundary

The hotel tablet needs native Wi-Fi while its working graphics, Ethernet,
MCU keepalive and audio default-off behavior remain usable. The preceding
RAM kernel physically identified SDIO function 1, `024c:d723`, at 25 MHz.
A full WLAN port needs transport, chip power/firmware, radio setup and
net80211 integration. This stage proves only an error-reporting native
CMD52 register-read transport on that same sample. It does not create a
network interface or claim association.

The operator authorized continued autonomous work without questions or
physical audio/microphone checks. Keep the installed eMMC kernel and vendor
firmware unchanged. Test a separate diagnostic configuration from RAM.

## Selected approach

Use NetBSD's native SD/MMC command execution and a small BSD-licensed read
helper. Validate domain addresses before encoding function-1 CMD52 reads;
report both host errors and card R5 errors. Read four bytes in little-endian
order, committing the caller's output only after every byte succeeds.

Protocol references are Linux v6.12 commit
`adc218676eef25575469234709c2d87185ca223a`, rtw88 `sdio.c`, `sdio.h`,
`reg.h` and `rtw8723ds.c`, choosing their BSD-3-Clause license option and
retaining authorship. MAC-off register access in that implementation uses
byte reads; do not issue CMD53 or indirect-register writes in this probe.
Local registers use domain `0x10250000` (offset at most `0x0fff`);
MAC registers use `0x10260000` (offset at most `0xffff`), mapped to CMD52
address `0x10000 | offset`. Reject a word crossing either domain boundary.

The driver only matches function 1 with manufacturer `0x024c`, product
`0xd723`, interface `0x07`, a pure I/O card, and the actual FDT MMC1 parent.
Card identity comes from the common function-0 CIS, as in NetBSD
`sdmmc_print`/bwfm/bwi; per-function MANFID may be absent. Reject an invalid
or mismatched common-CIS owner. Require the existing zero-length `ember,ys-m33-sdio-probe`, its translated
host ownership marker, and a separate zero-length root
`ember,ys-m33-rtl8723ds-read-probe`. Default configuration and absent opt-in
must perform no new I/O. A dedicated `EMBER64_A133_SDIO` configuration
includes the existing audio kernel and this diagnostic device only.

At attachment read CCCR function enable/ready, then MAC SYS_CFG1 (`0xf0`),
SYS_CFG2 (`0xfc`), and local HCI suspend control (`0x86`), using CMD52 only.
Repeat SYS_CFG1 once to expose an unstable result. Stop at the first error.
Do not enable the SDIO function, write any CCCR/register, change block size,
install interrupts, load firmware, read packet FIFOs or alter radio power.
If the disabled function rejects CMD52, record that exact blocker for the
next explicitly scoped bring-up step rather than hiding it with zero data.

## Validation and recovery

Test the real helper with synthetic FDTs and a command-level fixture:
foreign/default/malformed opt-ins, wrong function/card/host, domain edges,
byte order, R5 error bits, all four partial-read failures, unchanged output
on failure, and no command with a write flag. Build the dedicated kernel
from a clean source pin using native NetBSD 11/aarch64 tools. One independent
resource/lifecycle review precedes hardware testing; fix important findings.

RAM deployment verifies container bytes/hash/CRC and uses the retained UART
recovery path with MCU keepalive. Record exact register values or failures.
Check graphics, Ethernet/SSH, MCU keepalive and disabled audio; restore
ordinary eMMC autoboot and verify its complete boot-wedge hash unchanged.
No physical touch/audio operator feedback or sustained-stability claim.
