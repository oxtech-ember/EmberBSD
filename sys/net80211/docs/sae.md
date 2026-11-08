# SAE security helpers

`ieee80211_rsn.h` owns the bounded SAE association RSN check.
The helper accepts one CCMP pairwise cipher, SAE, and required PMF.
An omitted group management cipher selects BIP-CMAC-128.
An explicit group management cipher must select BIP-CMAC-128.
It rejects a null input pointer, regardless of the supplied length.
It checks the RSN element boundary and each optional PMKID field.
It does not validate a complete association or authenticate a peer.

The helper has no driver or firmware dependency. Both `bwfm` and `aicwf`
use it before their chip-specific connection commands. The implementation
and regression originate in the shared FullMAC header.

Run the portable RSN and driver SAE contracts from the source root:

```sh
sh ember/tests/aicwf-sae.sh
```

Select `CC` through the SDK native runner (LLVM/Clang by default). Set `TMPDIR`
to the build temporary directory. Set `CFLAGS` for diagnostic
builds. The common RSN assertions live in `sys/net80211/tests/rsn.c`.
Host contracts check parser bounds and security policy. They do not qualify
kernel integration, firmware behavior, or radio hardware.

The [station management contract](station-management.md) shares selected PSK/SAE
stream validation and RRM/WNM syntax between the firmware adapters.

## External SAE ioctl boundary

`ieee80211_ioctl_sae()` owns authorization, request allocation, copyin,
common ABI validation, and explicit request clearing. Drivers provide their
probed capabilities and a synchronous chip callback. A missing callback fails
before allocation. Disabled SAE can still be configured off when firmware
capabilities are absent. The existing enablement policy is unchanged.
The callback calls `ieee80211_sae_configure_check()` with current capabilities
and mode under its state ownership, after copyin. A pre-copyin capability
snapshot is used only for the non-sleeping GET reply.

The callback must not retain the request. It owns chip state and firmware
commands. AIC keeps its epoch checks across sleeping commands. Broadcom keeps
its existing `splnet()` state serialization inside the callback. The shared
allocation and copyin run before that callback. Broadcom's existing sleeping
firmware operations and frame allocation inside its serialized chip path
are not redesigned by this extraction.

`ic_sta_mgmt` remains the sole station management callback. Its ioctl path
and the AIC callback use the same common request validator. The ioctl path
clears the complete temporary station request before freeing it.

Run the production-function contracts in an output directory owned by the run:

```sh
python3 sys/net80211/tests/ioctl.py --cc "$CC" --build-dir /path/to/owned-objects
sh ember/tests/aicwf-sae-lifetime.sh
sh ember/tests/aicwf-sta.sh
```

`ioctl.py` accepts `--cflags` for GCC path analysis and ASan/UBSan runs.
It records the exact compile command in its output directory. The fixtures
exercise malformed requests, capability absence, copy faults, callback errors,
request clearing, and a stop or reconfiguration during copyin. The AIC lifetime
contract exercises the real callback across delayed firmware confirmations.
These host fixtures do not replace target kernel builds or hardware acceptance.

Cppcheck uses the generated `compile_commands.json` with `--inline-suppr`.
Two fixture-local annotations explain its opaque `copyin` alias and the fixed
mutable callback signature. They do not suppress production diagnostics.

## Native ioctl and supplicant ABI

The native capability `IEEE80211_SAE_CAP_KEY_EPOCH` adds `SET_KEY`,
`DELETE_KEY` and `SET_PORT` to the existing bounded request. Key payloads
are the native `ieee80211req_key`/`ieee80211req_del_key` structures; port payload
is exactly one boolean byte. Every operation carries the current BSSID and
nonzero generation. Ordinary untagged key/port requests are rejected during
native SAE. The ioctl owner checks its internal serial again after sleeping
copyin. Request buffers are explicitly cleared on all exits.

The BSD adapter uses random 32-bit equality tokens, fails closed on entropy
failure, and retires an earlier configuration before beginning another. This
prevents deterministic token reuse across supplicant restarts; it does not
make collisions mathematically impossible. Kernel queue tags use a separate
monotonic 64-bit serial, with exhaustion failing closed. Failed retirement
blocks later key callbacks and reconnect until cleanup succeeds. Untagged
routing disconnect events may close the adapter locally, but cannot mutate a
different kernel generation.

Run the native and BSD production-source fixtures together:

```sh
sh ember/tests/net80211-sae.sh
sh ember/tests/net80211-bip.sh
sh ember/tools/ccmp-contract.sh
```

`SAE_TEST_CFLAGS`, `NET80211_BIP_CFLAGS` and `CCMP_TEST_CFLAGS` select diagnostic
flags; `TEST_RUNNER` selects a memory-checker wrapper. `SAE_ANALYZE=1` runs
Cppcheck and Clang Static Analyzer on the recorded actual-source fixtures.
Keep `-Werror` and analyzer errors enabled. See the
[consumer matrix](wpa3-consumers.md) for firmware ownership and remaining
physical acceptance. These tests do not establish hardware connectivity.

## Opt-in native owner

`ieee80211_sae.c` implements external SAE for station providers that transport
complete raw management frames and use software CCMP. Registration is explicit
through `ieee80211_sae_attach()` after the provider finishes net80211 setup.
The common ioctl advertises native capabilities only while CCMP is registered;
firmware SAE callbacks keep their existing ownership. Registration does not
qualify a radio, firmware build, or hardware connection.

Native registration also requires the kernel's atomic 64-bit load/store
publication contract. Sleeping transport workers read the published serial
without accessing the separately allocated SAE owner. Targets without that
atomic contract reject native registration and retain the existing FullMAC
path; there are no torn 64-bit reads or new libatomic dependencies.

All owner operations run under the provider's net80211/Giant serialization.
Providers call `ieee80211_sae_state_request()` before queuing deferred state
work and before taking their own locks. It immediately retires invalid or
leaving transitions; CONFIGURED-to-SCAN remains available for discovery.
`ieee80211_sae_retire()` is required at synchronous stop/reset boundaries.
The common detach path retires before removing crypto and node state.

Configuration accepts a complete selected SAE/CCMP/required-PMF IE stream.
A nonzero user generation is an equality token bound to a BSSID; the owner
maintains a separate monotonic 64-bit serial. Sleeping ioctl copyin compares
that serial before and after the copy. Failed enablement never publishes a
requested token, a live different configuration returns EBUSY, and matching
disable remains idempotent after retirement. A retired native association
cannot fall back to open authentication or accept untagged WPAKEY/DELKEY.

AUTH emits START and forwards bounded SAE authentication frames, including
legitimate Retry/Power Management/More Data header flags. Only current external
success permits ASSOC. RUN closes the controlled port until tagged SET_PORT
and committed PTK plus IGTK are present. Successful atomic PTK/GTK/IGTK rekeys
preserve an already authorized port; initial key installs never authorize it.
Failed staged data-key installation closes the port. Identical-key reinstall
does not call the cipher setkey routine or reset PN/IPN state.

Native management RX validates peer and header format before dispatch.
Unicast robust frames require the current software CCMP PTK; group robust
frames require a final valid BIP-CMAC-128 MMIE with IGTK 4 or 5 and a fresh IPN.
The IPN advances only after successful verification. Unauthenticated disconnects
are ignored; current unicast reasons 6/7 notify the supplicant's SA Query path.
Verified SA Query frames use the existing event ABI, and verified disconnects
retire through the normal SCAN/node-leave notification path. Group robust TX
is unsupported and rejected for this station profile.

Common output marks robust unicast management for protection and attaches a
private serial tag. Providers check `ieee80211_sae_tx_current()` after management
dequeue and after data encapsulation, before any deferred transport copies.
The ordinary crypto entry point selects the current PTK and records completed
management encryption, preventing double encryption. Key-update begin/end
callbacks fence software-owned asynchronous packets around replacement and
retirement. Bytes already handed to a device remain ciphertext under their
original key. Native non-EAPOL data additionally requires current association,
controlled-port authorization and CCMP regardless of legacy privacy flags.

Run the actual native owner and common ioctl fixtures with a selected compiler:

```sh
python3 sys/net80211/tests/native-sae.py --cc "$CC" --build-dir /tmp/native-sae-test
python3 sys/net80211/tests/ioctl.py --cc "$CC" --build-dir /tmp/native-sae-ioctl-test
```

Both accept `--cflags`; the native fixture accepts a shell-split `TEST_RUNNER`
prefix for Memcheck. Each output directory contains the generated source and
compile database. The native fixture compiles the full production owner, the
actual node authorization functions and AUTH validator, and the common BIP
helper. Kernel allocation, transport, and block-cipher services are modeled;
independent CCMP and BIP known-answer suites validate cryptography. Tests cover
configuration/retry, current-peer and generation admission, delayed copyin,
controlled ports, rekey/reinstall and failure behavior, PMF replay, malformed
management, queue retirement and allocation/serial exhaustion. These fixtures
are host evidence, not target-kernel or hardware acceptance.
