# RGE raw-wire correctness probe

`ember/tools/rge-wire-probe.c` is a bounded native diagnostic for two RGE
ports connected by one cable. It sends raw Ethernet frames through BPF.
It does not use IP routing or change interface configuration.

The source contract and one physical CIX sample pass the checks below.
This does not qualify throughput, sustained load, camera traffic, VLANs,
or other Ethernet drivers.

## Build and run

Use the current GCC16 application compiler and its matching EmberBSD sysroot.
Build from a clean source commit. For example, with operator-selected paths:

```sh
"$CC" --sysroot="$SYSROOT" -std=gnu11 -O2 -g -Wall -Wextra -Werror \
  -static ember/tools/rge-wire-probe.c -o "$OUT/rge-wire-probe"
```

Transfer the program through the normal verified device procedure.
Run its pure self-test first. This command does not open any device:

```sh
./rge-wire-probe --selftest
```

An administrator must configure `rge0` and `rge1` UP with MTU 9000 before
the wire test. Save their original state and restore it after the test.
The program checks UP, RUNNING, MTU, Ethernet MAC identity and BPF capabilities.
It does not configure addresses, routes, offloads, link speed, or MTU.

```sh
./rge-wire-probe rge0 rge1
./rge-wire-probe rge0 rge1 4096
```

The default count is 32 frames. An explicit count must be a multiple of eight
between 8 and 65536. Every group of eight tests each length in both directions:
60, 1514, 1515 and 9014 bytes, excluding FCS. Counts are bounded correctness
runs, not throughput measurements. Each outstanding frame has one three-second
monotonic poll deadline. Interrupted calls do not restart that deadline.
Kernel syscall scheduling and hardware latency remain operating-system bounds.

Use only a controlled test connection. EtherType `0x88b5` is a
[local experimental EtherType](https://www.iana.org/assignments/ieee-802-numbers).
Each frame also has a protocol magic, fresh random run nonce, sequence,
direction and length. The payload changes with sequence and byte position.
The program does not print MAC addresses or capture payloads.

## Acceptance

Two BPF clone descriptors select inbound traffic only. Their read
filters select the exact MAC pair, EtherType, magic and run nonce.
Filters are installed before interface binding. Complete-header mode preserves
the selected source MAC. Feedback is disabled. Promiscuous mode is not used.
No write filter is installed. Native `bpf_movein` applies that filter to the
last mbuf fragment, which cannot validate a jumbo frame's Ethernet header.
The program constructs each complete transmit frame itself.
Only the opposite port's inbound descriptor can satisfy a transmitted frame.

The selected native RGE path preserves FCS at the ingress BPF tap.
`sys/net/if.c` calls the tap before `ether_input`.
`sys/net/if_ethersubr.c` removes `M_HASFCS` only after this tap.
`sys/dev/pci/if_rge.c` copies the full received frame and sets `M_HASFCS`.
The probe therefore requires capture length equal to transmitted length plus
four. It compares every transmitted byte and checks the little-endian CRC32.
It rejects missing FCS, truncation, unexpected frames and duplicates.
There is no fallback to outbound capture or a different capture format.

Success requires the exact expected capture count on each descriptor and zero
BPF drops. `bs_recv` is not an acceptance count because it includes rejected
traffic. Native `SIOCGIFDATA` input/output error counters must remain unchanged.
A counter reset, wrap or increase fails this bounded observation.
No interface or payload data are dumped on failure. The summary gives the
stage, completed frame count and error number. Both descriptors are closed.

## Source checks

```sh
CC=gcc-16 CFLAGS='-O2 -g -fanalyzer' RGE_WIRE_ANALYZE=1 \
  sh ember/tools/rge-wire-probe-contract.sh
CC=gcc-16 CFLAGS='-O1 -g -fsanitize=address,undefined' \
  sh ember/tools/rge-wire-probe-contract.sh
```

The contract compiles the actual frame, CRC, BPF-record parser, filter builder,
count and deadline functions. It imports the native BPF header declarations
and executes the filters with the native `bpf_filter.c` interpreter.
It covers literal frame fields, all size/direction pairs, altered bytes,
FCS corruption, truncated/unaligned BPF records, record bounds, duplicates,
error counters, count bounds, time regression and deadline overflow.
Configured Cppcheck and Clang Static Analyzer inspect this host test closure.
CI also executes it under ASan/UBSan and Valgrind Memcheck.
The host contract does not exercise native BPF ioctls or physical hardware.
The native program must be compiled with real target headers before transfer.

## Physical finite-wire check


Current physical scope and source pins are in [DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/cix-p1-orangepi6plus/interfaces.md).

The finite cable check requires exact payload/FCS, expected packets on each BPF
descriptor, zero BPF drops and unchanged native error counters. Receiving 2048
frames on a 1024-entry RX ring exercises two revolutions. Multi-descriptor jumbo
TX means that count does not establish exactly two TX revolutions.
Raw Ethernet traffic is distinct from IP/TCP, throughput and endurance.
Restore the previous MTU and administrative/IPv4 state after an authorized probe;
record normal IPv6 state transitions separately.
