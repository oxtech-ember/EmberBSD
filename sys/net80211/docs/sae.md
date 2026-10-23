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
CC=gcc-16 sh ember/tests/aicwf-sae.sh
```

Set `TMPDIR` to the build temporary directory. Set `CFLAGS` for diagnostic
builds. The common RSN assertions live in `sys/net80211/tests/rsn.c`.
Host contracts check parser bounds and security policy. They do not qualify
kernel integration, firmware behavior, or radio hardware.

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
python3 sys/net80211/tests/ioctl.py --cc gcc-16 --build-dir /path/to/owned-objects
CC=gcc-16 sh ember/tests/aicwf-sae-lifetime.sh
CC=gcc-16 sh ember/tests/aicwf-sta.sh
```

`ioctl.py` accepts `--cflags` for GCC path analysis and ASan/UBSan runs.
It records the exact compile command in its output directory. The fixtures
exercise malformed requests, capability absence, copy faults, callback errors,
request clearing, and a stop or reconfiguration during copyin. The AIC lifetime
contract exercises the real callback across delayed firmware confirmations.
These host fixtures do not replace target kernel builds or hardware acceptance.
