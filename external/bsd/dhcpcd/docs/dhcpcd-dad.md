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

The corrected source and shared patch are maintained by the common Ports
`net/dhcpcd-source` recipe. The shared behavioral fixture is maintained beside
that recipe in Ports; OS tests consume the selected prepared source tree.
The DHCP state fix itself is tracked in
[commit 4cbeb4ea](https://github.com/oxtech-ember/EmberBSD-Ports/commit/4cbeb4eaf5378a3f01d8d740590dc974ccf88ca3).
The OS build selects that prepared tree through `EMBER_DHCPCD_SOURCE`; it
does not use a second vendored copy. A kernel-only update does not replace
`/sbin/dhcpcd`. Build the client with an identified, compatible EmberBSD
sysroot. Preserve its existing configuration, hooks and previous executable
during installation.

## Regression

The canonical fixture extracts complete production functions and headers from
the selected prepared source in the Ports checkout. It replaces OS I/O and
timers; it does not parse DHCP packets or emulate a physical radio. Select the
host-prefix compiler and tools, then run it with a new output directory:

```sh
CC=/absolute/host-prefix/bin/clang \
TEST_AWK=/absolute/host-prefix/bin/nawk \
TEST_SHASUM=/absolute/host-prefix/bin/shasum \
/absolute/host-prefix/bin/mksh /absolute/EmberBSD-Ports/pkgsrc/net/dhcpcd/tests/dhcpcd-dad.sh \
  /absolute/prepared-dhcpcd /absolute/new-check-directory
```

The selected path is the prepared source root; its `src/` directory supplies
`dhcp.c`, `dhcp.h` and the other headers. To test an older `dhcp.c` against
those selected headers, pass its absolute path as the third argument. The
original source must fail the unconfirmed-lease cases. The revision with only
the DAD guard must fail the active-fallback case.

For target execution, use the same test with the cross compiler:

```sh
CC=/absolute/toolchain/bin/aarch64--netbsd-gcc \
TEST_AWK=/absolute/host-prefix/bin/nawk \
TEST_SHASUM=/absolute/host-prefix/bin/shasum \
CFLAGS='--sysroot=/absolute/emberbsd-sysroot -O2 -g -std=c11 -Wall -Wextra -Werror' \
DHCPCD_DAD_RUN=0 \
/absolute/host-prefix/bin/mksh /absolute/EmberBSD-Ports/pkgsrc/net/dhcpcd/tests/dhcpcd-dad.sh \
  /absolute/prepared-dhcpcd /absolute/new-target-check-directory
```

Copy the resulting `check` executable to the selected device and run it there.
Use the existing `build.sh` make wrapper to build the client:

```sh
/absolute/tools/bin/nbmake-evbarm \
    -C external/bsd/dhcpcd/sbin/dhcpcd \
    EMBER_DHCPCD_SOURCE=/absolute/prepared-dhcpcd dhcpcd
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

## Validation, 2026-10-15

The client was cross-built with GCC 16.2.0 from
`a6f115893c64a58452af50d89d0fd3dd84d8b9d4`.
The EmberBSD sysroot came from `f76133ff69a57de1571c892123eadbc589afd1bb`.
The installed client SHA256 was
`090fd24c8cc00eed07d0c49c832ba81ddd161f9de3f7c42eaed8b39a101316d7`.
Its normal INET, ARP, IPv4LL, INET6, DHCPv6, AUTH and PRIVSEP features remained enabled.

The shared Ports fixture reported 77 production-function checks under host ASan/UBSan
and on a physical AArch64 Pi 5. Negative controls failed on the affected earlier revisions.
Valgrind and LeakSanitizer were not run on this platform.
The fixture replaces I/O; hardware checks below test the real network path.

Production analysis used target GCC preprocessing and build flags.
Cppcheck 2.22.0 reported 49 existing findings before and after the patch.
GCC 16.2.0 `-fanalyzer` retained one context-owned descriptor warning.
Apple Clang 21 Static Analyzer retained one option-parser dead-store warning.
No new diagnostics appeared. These findings are outside the changed state transitions;
they do not constitute a clean analysis of the imported module.
The earlier duplicate OS fixture retained four Cppcheck findings: three const suggestions and one
condition fixed by its I/O stub. Its Clang analysis produced no findings.

On a Pi 5 Model B with 8 GiB RAM, all six controlled WPA2 reconnects recovered
DHCP without a residual IPv4LL address. Some cycles started fallback before the
server reply; the completed DHCP bind removed it. A separate test ignored DHCP
replies, obtained IPv4LL, then restored DHCP and removed fallback.
A normal reboot preserved the corrected client, mDNS name and SSH access.
See the [board evidence](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-rpi5/README.md)
for the kernel, transfer checks and acceptance limits.
