# DHCP address validation after reconnect

EmberBSD keeps IPv4 link-local fallback enabled. A `169.254.0.0/16` address
provides local connectivity when DHCP is unavailable. It must not replace
server validation of a saved DHCP lease.

## Failure and correction

On carrier recovery, the kernel checks an existing IPv4 address for duplicates.
The DHCP client also asks the server to confirm its saved lease.
These operations can finish in either order.

In dhcpcd 10.2.3, a successful duplicate-address check could call `dhcp_bind`
while the client still waited for DHCP. The client then ignored a valid server
NAK because its state was already `DHS_BOUND`. An uncancelled timeout could
start IPv4LL beside that unconfirmed address. mDNS could publish both addresses.

The correction rejects DAD completion in `DHS_REBOOT` and `DHS_REQUEST`.
Discovery also waits for DHCP unless an explicit expired-lease fallback is active.
The server can still accept or reject the lease. IPv4LL remains available
while no usable DHCP lease exists. A successful configured bind also removes
an active fallback, including when the DHCP reply arrives after DAD.
Failed address application does not discard fallback connectivity.
Static, INFORM and explicit last-lease behavior retain separate paths.

This changes `external/bsd/dhcpcd/dist/src/dhcp.c`. A kernel-only update does
not replace `/sbin/dhcpcd`. Build the client from the corrected OS revision
with an identified, compatible EmberBSD sysroot. Preserve its existing
configuration, hooks and previous executable during installation.

## Regression

The fixture extracts complete production functions from the selected source.
It replaces OS I/O and timers. It does not parse DHCP packets or emulate a
physical radio. Run it on the host with a new output directory:

```sh
sh ember/tests/dhcpcd-dad.sh "$PWD" /absolute/new-check-directory
```

To test an older source file, pass its absolute path as the third argument.
The original source must fail the unconfirmed-lease cases. The revision with
only the DAD guard must fail the active-fallback case.

For target execution, use the same test with the cross compiler:

```sh
CC=/absolute/toolchain/bin/aarch64--netbsd-gcc \
CFLAGS='--sysroot=/absolute/emberbsd-sysroot -O2 -g -std=c11 -Wall -Wextra -Werror' \
DHCPCD_DAD_RUN=0 \
sh ember/tests/dhcpcd-dad.sh "$PWD" /absolute/new-target-check-directory
```

Copy the resulting `check` executable to the selected device and run it there.
Use the existing `build.sh` make wrapper to build the client:

```sh
/absolute/tools/bin/nbmake-evbarm \
    -C external/bsd/dhcpcd/sbin/dhcpcd dhcpcd
```

Record the wrapper configuration, OS revision, sysroot origin and output hash.

## Physical diagnosis

Capture only ARP and DHCP on the affected interface. Enable `debug` in the
client configuration for a bounded reproduction. A diagnostic log should
show whether `DAD completed` preceded `acknowledged` or a server NAK.
The broken path logs `leased`, then `bound, ignoring`, then starts IPv4LL.
A `leased` line alone does not prove that a server accepted the lease.

After installing the correction, repeat reconnects with IPv4LL enabled.
Check DHCP replies, interface addresses, mDNS and a transfer through Wi-Fi.
Also test unavailable DHCP followed by recovery. The fallback must remain
available during the outage and disappear after a usable DHCP bind.

This work is AI-assisted. It retains the upstream license and authorship.
It is an EmberBSD correction, not a claim of upstream acceptance.
