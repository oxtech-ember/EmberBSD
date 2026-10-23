# SAE security helpers

`ieee80211_rsn.h` owns the bounded SAE association RSN check.
The helper accepts one CCMP pairwise cipher, SAE, and required PMF.
An omitted group management cipher selects BIP-CMAC-128.
An explicit group management cipher must select BIP-CMAC-128.
It checks the RSN element boundary and each optional PMKID field.
It does not validate a complete association or authenticate a peer.

The helper has no driver or firmware dependency. Both `bwfm` and `aicwf`
use it before their chip-specific connection commands. The implementation
and regression moved from the shared FullMAC header without a policy change.

Run the portable RSN and driver SAE contracts from the source root:

```sh
CC=gcc-16 sh ember/tests/aicwf-sae.sh
```

Set `TMPDIR` to the build temporary directory. Set `CFLAGS` for diagnostic
builds. The common RSN assertions live in `sys/net80211/tests/rsn.c`.
Host contracts check parser bounds and security policy. They do not qualify
kernel integration, firmware behavior, or radio hardware.
