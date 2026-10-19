# IPv4 socket fragmentation control

`IP_DONTFRAG` is an IPv4 boolean socket option. Enabling it sets the DF bit
independently of TCP path MTU discovery and route MTU locks. Oversized packets
reach the existing `EMSGSIZE` path, without local IP fragmentation. IPv6 sockets
are outside this interface; packet filtering and IPsec tunnel policies remain
independent. See `ip(4)` for the public API.

Run the portable source contract from the source-tree root:

```sh
sh ember/tools/tests/ip-dontfrag/contract.sh .
```

It compiles the actual option cases, header initialization, DF application,
PMTU branch and existing fragmentation rejection with narrow host shims.
The checks cover boolean state, other PCB flags, all IPv4/IPv6 flag collisions,
locked route MTUs, raw-header DF preservation, MTU reporting, IPv6 rejection and
forwarding without a socket. It does not execute the complete network stack;
use the native runtime check for packet-level validation.

Build and run the native runtime check as root in an isolated NetBSD/EmberBSD
VM with BPF enabled and a `/dev/bpf` or `/dev/bpfN` device:

```sh
cc -O2 -Wall -Wextra -Werror ember/tools/tests/ip-dontfrag/runtime.c -o /tmp/ip-dontfrag
/tmp/ip-dontfrag
```

It uses only loopback sockets and a read-only BPF capture. It does not change
interface MTUs, routing tables or global sysctls. It requires the existing
`lo0` MTU to be between 128 and 65507 bytes so a legal UDP datagram can exceed
that MTU. It checks default/set/get/clear behavior, captured DF bits, oversized
`EMSGSIZE` and `IP_ERRORMTU`, then successful reassembly with DF disabled.
A kernel predating the option must fail at its first `getsockopt`, with
`ENOPROTOOPT`; this test never treats an unavailable option as success.
