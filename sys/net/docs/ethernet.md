# Ethernet driver integration

Read the [Ethernet architecture guide](https://github.com/oxtech-ember/EmberBSD-Docs/blob/main/modules/ethernet.md)
for module boundaries and standards. This page maps the exact interfaces used by controller, PHY, USB, and virtual NIC adapters.
The linked headers, implementations, and manual pages own their detailed behavior.
Hardware wiring and support records remain in [DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards).

## Choose the extension point

| Change | Start here | Adapter responsibility |
| --- | --- | --- |
| New MAC/controller | [`ifnet`](../if.h), [`ethercom`](../if_ether.h), [Ethernet helpers](../if_ethersubr.c) | Device queues, DMA, registers, IRQs, filters, capabilities, link reporting, and resource lifecycle |
| New attachment for an existing MAC | Existing common core and the bus/platform frontend | Resources, clocks, reset, address, PHY topology, and device properties; reuse the packet engine |
| New PHY | [`mii_data`, `mii_softc`, `mii_phy_funcs`](../../dev/mii/miivar.h), [PHY helpers](../../dev/mii/mii_physubr.c) | Match the PHY identity and supply service, status, and reset operations; reuse generic negotiation helpers where applicable |
| New USB Ethernet adapter | [`usbnet_ops`](../../dev/usb/usbnet.h), [framework](../../dev/usb/usbnet.c), [usbnet(9)](../../../share/man/man9/usbnet.9) | Device framing, setup, multicast filters, register/link operations, and optional interrupt/tick handling |
| Virtual NIC | Existing bus/protocol framework and [VirtIO network adapter](../../dev/pci/if_vioif.c) | Negotiated transport features, queue completion, packet metadata, and host/guest ownership |

Do not add VLAN, LACP, DHCP, or IP protocol implementations to a controller driver.
For a managed switch or special accelerator, first identify the actual control and frame ABI;
a software bridge is not a hardware switch programming interface.

## Interface and controller map

| Area | Exact entry point | What to implement or verify |
| --- | --- | --- |
| Registration | [`if_initialize`, `if_register`, `if_detach`](../if.c), declarations in [`if.h`](../if.h) | Prepare callbacks and private state before publication. New code should use the split initialize/register lifecycle; `if_attach` is marked deprecated in the header. Check initialization side effects before setting enabled capabilities. |
| Ethernet integration | [`ether_ifattach`, `ether_ifdetach`, `ether_ioctl`](../if_ethersubr.c), [`ethercom`](../if_ether.h), [ethersubr(9)](../../../share/man/man9/ethersubr.9) | Supply the link address and use common framing, input dispatch, multicast bookkeeping, and capture integration. Implement hardware filter updates and the relevant reset/reconfiguration result. |
| Administrative callbacks | `if_init`, `if_stop`, `if_ioctl` in [`ifnet`](../if.h) | Bring the device up/down, apply controls, and unwind partial setup. Common Ethernet requests stay with the common helper. |
| Transmit | `if_start`, `if_transmit`, and deferred-start interfaces in [`if.h`](../if.h); [queue implementation](../if.c) | Use the queue model appropriate to the adapter. Define ownership on every success/error path; reclaim only completed device work. `if_transmit` must be MP-safe. |
| Receive | [`if_percpuq_create`, `if_percpuq_enqueue`, `if_percpuq_destroy`](../if.c), [`ether_input`](../if_ethersubr.c) | Validate frames and descriptor metadata, set the receive interface and packet lengths, then transfer packet ownership to the common path. Enqueue consumes the mbuf, including on queue overflow. |
| Packet representation | [`mbuf` and checksum metadata](../../sys/mbuf.h), [mbuf(9)](../../../share/man/man9/mbuf.9) | Handle chains, lengths, alignment, checksum requests/results, VLAN metadata, and release rules. Match `M_HASFCS` to whether the delivered frame still includes its FCS. |
| DMA and register access | [bus_dma(9)](../../../share/man/man9/bus_dma.9), [bus_space(9)](../../../share/man/man9/bus_space.9) | Respect device address/segment/alignment constraints, map/unmap lifetimes, synchronization, and descriptor ordering. CPU virtual addresses are not DMA addresses. |
| Offloads and frame size | `if_capabilities` / `if_capenable` in [`if.h`](../if.h), `ec_capabilities` / `ec_capenable` in [`if_ether.h`](../if_ether.h), [software offload helpers](../ether_sw_offload.c) | Advertise only implemented checksum/segmentation and Ethernet capabilities. Keep VLAN MTU, tagging, filtering, and jumbo support distinct. Honor the enabled selection and packet-specific metadata. |
| Media and carrier | [`ifmedia`](../if_media.h), [media implementation](../if_media.c), [`if_link_state_change`](../if.c) | Report actual supported and active media, duplex, flow control, and link transitions. Do not confuse administrative `UP` with carrier. |
| Packet capture and counters | [BPF interface](../bpf.h), [interface statistics](../if_stats.h), [Ethernet capture integration](../if_ethersubr.c) | Account for delivered, dropped, and failed packets; preserve the driver's correct capture point, including hardware VLAN metadata. |
| Build and device lifecycle | [Network configuration declarations](../../conf/files), bus/driver `files.*`, [power management](../../../share/man/man9/pmf.9) | Select the common `ether` and applicable `mii`/bus dependencies. Implement supported suspend, resume, reset, and detach paths; state unsupported operations explicitly. |

The older [ethersubr(9)](../../../share/man/man9/ethersubr.9) manual is a useful introduction,
but the current headers and implementations define initialization and concurrency requirements.
Follow the actual registration order of a current consumer rather than treating an older attach example as a complete template.

## PHY and media interfaces

The controller supplies `mii_readreg`, `mii_writereg`, and `mii_statchg` through [`mii_data`](../../dev/mii/miivar.h).
The first two perform PHY register transactions; status changes let the MAC apply the resolved media state.
Use the common attach, media-change, poll, tick, and down operations as required by the chosen link path.
The MII and ifmedia locking contract is documented in the same header.

A PHY driver supplies `pf_service`, `pf_status`, and `pf_reset` through `mii_phy_funcs`.
Its service handles the framework's media-change, status-poll, tick, and down requests.
Use [mii_physubr.c](../../dev/mii/mii_physubr.c) for shared negotiation and status propagation,
[mii.c](../../dev/mii/mii.c) for framework dispatch, and [files.mii](../../dev/mii/files.mii) for driver selection.
Vendor register programming and PHY errata belong to the PHY adapter.

[mii.h](../../dev/mii/mii.h) defines basic registers;
[mdio.h](../../dev/mii/mdio.h) defines Clause 45 registers.
The `MMD_INDIRECT_*` helpers in [miivar.h](../../dev/mii/miivar.h) access MMD registers through the basic register path.
Do not infer native Clause 45 bus transactions from those helpers or from the register definitions.
Check the controller's actual MDIO implementation and propagate its errors.

For a fixed link, internal PCS, or firmware-managed media path, follow that adapter's real control interface.
Physical data-interface selection and delay ownership belong to platform properties and the matching MAC/PHY implementation.
They are independent of the common media vocabulary used by userland.

## Packet ownership and concurrency

Use the locking annotations in [`if.h`](../if.h), [`miivar.h`](../../dev/mii/miivar.h), and [`usbnet.h`](../../dev/usb/usbnet.h).
`IFEF_MPSAFE` removes implicit kernel-lock protection from the listed callbacks; it does not add driver locks.
`if_transmit` is always called without that protection.
The header also documents restrictions on `if_flags`, taking `IFNET_LOCK` in soft interrupt context, and watchdogs.

Keep a single owner for each pending packet and each descriptor slot.
DMA synchronization must precede CPU consumption or device publication as required by [bus_dma(9)](../../../share/man/man9/bus_dma.9).
An RX buffer handed to the stack cannot be reused by the device until its storage ownership permits it.
On TX completion, release the mapping and packet only after the device has finished with them.
Bounds and completion validation remain necessary even when interrupts and checksum offload are enabled.

Stop admission, quiesce device access, and drain interrupts, callouts, soft interrupts, and other deferred users before freeing their state.
Link loss alone does not establish DMA quiescence.
Use the USB framework's teardown for resources it owns; device callbacks must respect its lock and transfer-lifetime rules.
The [usbnet manual](../../../share/man/man9/usbnet.9) distinguishes required callbacks, optional callbacks, and framework-owned work.

## Read an existing implementation

These examples show integration patterns, not a hardware support matrix.

| Example | What to inspect |
| --- | --- |
| [GENET common MAC](../../dev/ic/bcmgenet.c), [FDT](../../dev/fdt/genet_fdt.c), [ACPI](../../dev/acpi/genet_acpi.c) | Separation of platform properties from queues, DMA, and MII. Existing legacy attach usage is not the preferred lifecycle for new code. |
| [DesignWare EQOS](../../dev/ic/dwc_eqos.c) | Descriptor handling, MDIO transactions, and applying resolved media/PAUSE state to the MAC. |
| [wm PCI controller](../../dev/pci/if_wm.c) | Split interface registration, per-CPU receive delivery, transmit paths, queue selection, and offload limitations. |
| [Realtek PHY](../../dev/mii/rgephy.c), [generic PHY](../../dev/mii/ukphy.c) | PHY function tables, shared helpers, vendor-specific behavior, and limits such as explicitly disabled EEE paths. |
| [axen USB adapter](../../dev/usb/if_axen.c), [CDC Ethernet adapter](../../dev/usb/if_cdce.c), [NCM adapter](../../dev/usb/if_ncm.c) | `usbnet_ops`, device packet parsing, transmit preparation, and class/vendor-specific setup. |
| [VirtIO network adapter](../../dev/pci/if_vioif.c) | Feature negotiation and queue ownership without a physical PHY. |

## Protocols and feature selection

| Mechanism | Owner reference |
| --- | --- |
| Ethernet framing and dispatch | [if_ether.h](../if_ether.h), [if_ethersubr.c](../if_ethersubr.c) |
| Link modes and flow control | [if_media.h](../if_media.h), [ifmedia(4)](../../../share/man/man4/ifmedia.4), [MII helpers](../../dev/mii/mii_physubr.c) |
| VLAN | [if_vlan.c](../if_vlan.c), [vlan(4)](../../../share/man/man4/vlan.4) |
| Bridge and STP | [if_bridge.c](../if_bridge.c), [bridgestp.c](../bridgestp.c), [bridge(4)](../../../share/man/man4/bridge.4) |
| Aggregation and LACP | [lagg implementation](../lagg), [lagg(4)](../../../share/man/man4/lagg.4), including its administrative limitations |
| Wired 802.1X | [Enabled supplicant backend](../../../external/bsd/wpa/bin/wpa_supplicant/Makefile), [wired driver](../../../external/bsd/wpa/dist/src/drivers/driver_wired.c), [common wired operations](../../../external/bsd/wpa/dist/src/drivers/driver_wired_common.c) |
| Userland configuration | [ifconfig(8)](../../../sbin/ifconfig/ifconfig.8), [network setup](../../../doc/guide/user-guide.md) |

## Validate the selected path

Build the affected driver and its consumers with the selected SDK/Ports toolchain.
Check actual packet contents and ownership, not just device enumeration or link status.

| Changed area | Evidence to obtain |
| --- | --- |
| Frame transport | Both directions, short and maximum supported frames, chained buffers, malformed completion lengths, allocation failure, queue exhaustion, and repeated buffer reuse |
| Media control | Supported negotiation/forced modes, link removal and return, resolved duplex/PAUSE, MDIO errors, and truthful notifications |
| Offload | Correct checksums and VLAN identity with each supported feature enabled and disabled; MTU/segmentation boundaries and unsupported requests |
| Composition | Selected VLAN, bridge, aggregation, multicast/promiscuous, IPv4/IPv6, and wired-authentication paths as applicable |
| Lifecycle | Repeated up/down, pending traffic during supported reset/detach/suspend, error unwind, and absence of callbacks or DMA after resource release |

Reuse the existing [interface tests](../../../tests/net/if), [VLAN tests](../../../tests/net/if_vlan),
[bridge tests](../../../tests/net/if_bridge), [lagg tests](../../../tests/net/if_lagg), and [BPF tests](../../../tests/net/bpf)
for changes to those mechanisms. Their software/rump coverage does not establish physical DMA, PHY timing, USB unplug, or controller offload behavior.
Keep new driver-specific regressions with the implementation and physical applicability in DevBoards.
