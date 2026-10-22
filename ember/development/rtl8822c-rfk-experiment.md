# Manual RTL8822C RFK experiment

This default-OFF experiment adds a bounded full GAPK → IQK → DPK transaction
before the native worker publishes AUTH. It does not enable HT admission,
HT/VHT rates, firmware RSSI feedback, aggregation or calibrated RF readiness.
The RTL8822CE HT provider stays NULL. Ordinary kernel profiles retain their
legacy behavior. This document describes a source contract, not a successful
physical calibration.

## Controlled boot boundary

Use only the separate `EMBER64_CIX_RFK_NATIVE` kernel profile. Keep the working
kernel and the existing default and recovery menus. Stage the kernel without
changing their selection. Boot the experimental kernel manually in single-user
mode (`-s`). Do not start network or supplicant services automatically. An
operator starts one explicit interface/association trial after a verified cold
power reset. Kernel replacement alone does not prove a hardware reset.

The option is `RTW88_RFK_EXPERIMENT`. Module builds reject it. The first profile
requires native lock-free 64-bit atomics. Ownership uses the NetBSD atomic API
with explicit barriers around its rare read/modify/write operations. Compiler
lock-free assertions alone do not prove kernel-link helper availability; the
actual GCC16 target build remains a separate gate. There is no runtime enable or clear
operation. An uncertain result requires a physical cold reset before another
experimental trial. Software reboot and ordinary firmware restart cannot clear
that hardware uncertainty. They are not recovery acceptance criteria.

## Actual owner and completion contract

The current AUTH worker acquires a kernel-lifetime exclusive numeric owner
before the existing all-five-queue DMA drain. Device read-index consumption
remains distinct from RF idle, transmission and air ACK. The worker selects the
current channel only after that drain. Before the first RFK write, it reads
RF18 on both paths and requires the selected primary, band, region and 20 MHz
fields to match the native channel programming. A read error or disagreement
rejects without an RFK trigger or mutation. It then executes the whole reference
chain before station setup, current EDCA and parent AUTH publication.

The worker holds no PCI lock while sleeping. `kpause` releases Giant.
After every wake, the owner compares
initialized numeric/value BSS, security, local/peer rates, media, protection,
HT provider/capsule and stage-appropriate EDCA inputs. A same-generation provider
spacing change cancels the attempt too. AUTH has no assigned AID: the experiment
does not invent one or send a RUN rate-adaptation command.

New work has a six-second absolute budget. Cleanup has the same original start
plus eight seconds. Register IO, mandatory RF delays, indirect LTE readiness,
firmware transport and scheduler time all consume those clocks. Individual
phases retain their own earlier deadlines. Each typed IO validates the clock;
transport-mandated RF write delays remain with their write (at most 13 us).
Other sourced submillisecond delays are bounded separately; millisecond waits,
report polling and the sourced RXBB 600–610 us sleep yield outside PCI.
Scheduler stalls and the
platform bus primitive itself are not bounded by this software deadline. This is a fail-closed experimental
policy, not a successful runtime or complete AUTH latency guarantee.

Each triggered GAPK, IQK, DPK, CAL_PWR and final coefficient operation must expose
an observed POST-trigger non-target sample followed by its target sample.
Both source 6d handshakes have that rule too. Constant initial success fails.
Legitimate completion before the first sample can also fail. This oracle is
stricter than the primary reference; it is not a primary guarantee of freshness.
No guessed report-clear, cookie or abort register write is added.

IQK uses the actual shared packet-H2C publisher. Its split continuation retains
the original command bytes, descriptor layout, sequence reservation and DMA
ownership. It yields while waiting for actual device consumption. Consumption
does not mean semantic IQK completion. A prior uncertain read/write remains
sticky; cleanup cannot turn a later snapshot into permission to reuse DMA.

## Originals, cancellation and quarantine

The owner captures the source-defined 0x520 and 0x1e70 originals before the early
exclusive TX pause. Internal paused snapshots cannot replace them. The normal
full chain restores those exact words last. Normal station setup can then apply
its existing DATA-only pause; parent AUTH must not inherit RFK's 0xff pause.

The first baseline contains all 110 original gain entries. Clean attempts in
the same firmware/RFE/cut profile reuse it. After it exists, a new firmware
generation rejects RFK before phase or radio IO. Full startup has not been
proved to replace all 110 persistent entries. The experiment must not recapture
its own committed gains as originals. A normal restart therefore does not make
this first prototype ready for another experimental AUTH. Clean detach also
cannot erase this restriction: discarding a populated original-baseline cache
latches the kernel-global quarantine before freeing the heap. Fresh attach in
the same kernel is blocked, even after successful DMA teardown.

GAPK gains must be committed before IQK and DPK. A later failure does not promise
their rollback. After cancellation, no new phase or trigger may start. The old
exclusive owner may only poll an existing operation, restore valid source
snapshots and finish an already-started 6d handshake within its old firmware,
resource lifetime and original deadlines. It does not become the next request.
After completed RFK, an ordinary station-mailbox sleep still requires a fresh
full-input comparison and the original token/firmware/resources/global lease
before any ordinary commit or parent AUTH. That lease check performs no IO and
neither restarts RFK nor extends its deadline to ordinary mailbox latency.

The normal DPK restoration contains another RXBB calibration trigger. Canceled
cleanup does not disguise it as a harmless restore. That report-less RXBB
sequence holds engine uncertainty until all sourced writes and the minimum
sleep complete. A possibly-landed write error or cancellation during that sleep
permits no subsequent RFK journal/GNT/handshake IO; only quarantine and the
existing stop owner follow. Fixed-delay sequence completion is not a fresh RF
report or calibrated-readiness claim. Without demonstrated full
operational restoration, any mutated failure latches kernel-global quarantine
and uses the existing stop owner. A failed final restore may already have
landed; software does not claim that hardware remains paused. Quarantine blocks
ordinary automatic starts and fresh attach, including a new device allocation.
Failed DMA/IRQ teardown keeps the existing resource ownership. If another
retired context loses its populated originals while this owner yields, the
resulting global quarantine withdraws normal IO from the live owner too. Its
same-token cleanup may still enter under the sticky quarantine, subject to
engine uncertainty, old firmware/resources and the original deadline.

## Numeric observation

`hw.rtwce_stats` adds a bounded numeric mirror only in this profile. The process
owner samples its context before yielding and after retirement. It copies the
sample under the PCI lock. The formatter reads that mirror, not a live heap
context. It contains no packet, key, address or SSID dump.

- `rfk`: token, current firmware generation and original-baseline generation.
- `flags`: active, completed, pending, engine pending, cleanup, canceled,
  handshake started, end sent and handshake finished, in bits 0 through 8.
- `global`: exclusive owner bit 0 and sticky quarantine bit 1.
- `error`: primary and cleanup errors.
- `rfk_report`: phase kind, register, read width, raw value, raw validity and
  observed non-target evidence. An invalid raw value is not a zero sample.
- `rfk_cost`: IO operations, requested explicit delay microseconds and yields.
- `rfk1` through `rfk7`: armed/accepted oracle counts for start 6d, end 6d,
  GAPK, IQK, DPK, CAL_PWR and final coefficients. Arming is not proof that a
  hardware write landed. Completion means only the experimental host oracle.

## Current operating envelope before AUTH

The experiment also gates the first AUTH producer on a current operating
snapshot. The actual power programmer publishes an identity only after its
complete checked program succeeds. It records firmware, channel, cut, RFE,
regulatory-table policy and the computed HT1SS references/differences. These are
successful source writes, not hardware TXAGC-table readback or calibrated dBm.

RFK binds that value after its own selected-channel program and before phase
writes. Every normal continuation and ordinary-mailbox retry checks it again.
After normal RFK restoration, checked reads require both RF18 paths to match
selected BW20/primary/band, MAC BW and secondary selection to be zero, and
BB1d08 bit0 to select normal TMAC. No guessed repair write follows a mismatch.
Only successful retirement can seal this snapshot before parent AUTH, without
an intervening yield. A missing, changed or quarantined proof keeps AUTH closed.
A post-commit failure withdraws current acceptance and rate policy. A newer
request keeps its own epoch and parent transition.
Stop, channel and power mutations withdraw the snapshot before IO.

The provider remains NULL. This envelope does not establish mixed-format air
traffic, hardware BlockAck, physical RFK convergence or hardware power indices.
A separate source-backed TXAGC read protocol remains unimplemented.

## Source checks and limits

Run `sh ember/tools/rtw88-rfk-contract.sh` with GCC16. The contract executes actual
owning worker, core, phase, mailbox, packet and shared stack bodies. Its explicit
service model supplies bus register samples, device completion, allocation,
Giant-release scheduling and time. It does not execute an RF engine or firmware.
The populated-baseline detach/free producer is executed separately. An external
context's equivalent quarantine arrival is injected through the host atomic
service at a real RXBB sleep or owner-drop boundary to exercise the actual
normal/cleanup and first-AUTH consumers;
this is not a two-device hardware-concurrency simulation.
The formatter test executes the actual PCI statistics body at every buffer
capacity, including maximum numeric values. The normal packet-H2C and legacy
source suites remain separate regression gates.

The cases include missing/stale completion, possibly-landed writes, snapshot and
restore faults, same-epoch profile changes, allocation cancellation, C1 spacing
changes, packet uncertainty, cleanup expiry and dying ownership. They also
require zero RFK IO for an unstarted INIT, no resampling after a new firmware
generation, and fail-closed two-path RF18 readback before mutation. The first
AUTH callback executes the actual shared management-frame builder and native
producer, checking a descriptor was posted after the full-chain gate. Causal controls and sanitizers do not qualify analog convergence,
throughput, hardware BlockAck, a physical reset boundary or HT readiness.

## Pinned primary sources

The adaptation uses FreeBSD commit
`d2145e9b96448da34d3cfede24c509af6d830c92`:

- [RTL8822C phase bodies](https://github.com/freebsd/freebsd-src/blob/d2145e9b96448da34d3cfede24c509af6d830c92/sys/contrib/dev/rtw88/rtw8822c.c),
  SHA256 `6986e6107da9261b97277d57fcc11daf5dcb0d71571c55b1080bdea1495ce160`.
- [DPK tables](https://github.com/freebsd/freebsd-src/blob/d2145e9b96448da34d3cfede24c509af6d830c92/sys/contrib/dev/rtw88/rtw8822c_table.c),
  SHA256 `ec3f4d9eed016932b9100e4e2332c12f67bd0824a3f3b4b05cc515e7916a2247`.
- [Firmware command formats](https://github.com/freebsd/freebsd-src/blob/d2145e9b96448da34d3cfede24c509af6d830c92/sys/contrib/dev/rtw88/fw.c),
  [register definitions](https://github.com/freebsd/freebsd-src/blob/d2145e9b96448da34d3cfede24c509af6d830c92/sys/contrib/dev/rtw88/reg.h)
  and [indirect register helpers](https://github.com/freebsd/freebsd-src/blob/d2145e9b96448da34d3cfede24c509af6d830c92/sys/contrib/dev/rtw88/coex.c).

The typed native IO, freshness oracle, finite budgets and lifetime policy are
explicit adaptations. Logging or accepting timeouts in the reference does not
prove that this fail-closed controller or physical RF calibration succeeded.

The RF18 precondition uses the exact fields programmed by pinned
`rtw8822c_set_channel_rf` and native `channel_rf`: mask
`0x10300 | 0xff | 0x60000 | 0x3000`, 20 MHz value `0x3000`, 5 GHz value
`0x10100`, region `0x20000` for channels 100–144 and `0x40000` for 149–177.
No unrelated RF18 bits are guessed or compared. This is selected-profile
consistency, not proof of analog tuning or calibration.

The normal-TMAC bit check follows
[Realtek phydm_set_tmac_tx_jgr3](https://github.com/rtlwifi-linux/rtk_wifi_driver_rtl8822ce/blob/5ef878c6310659d2eff5ebeb74b2a9d153fa61a6/hal/phydm/phydm_pmac_tx_setting.c#L388-L395),
SHA256 `372145f8d72d8c4840bd2d82ccfa7c2a5e0aedccb6e2f1cb2a1e318c8deba7ce`.
This GPL source supplies read-only register semantics; no implementation is
imported. The pinned BSD PHY table initializes register 0x1d08 to 0xa0000000. A final bit read
checks current consistency instead of assuming that initialization survived RFK.
