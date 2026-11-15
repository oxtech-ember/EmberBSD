# Bluetooth driver and profile interfaces

Read the [Bluetooth architecture and profiles](https://github.com/oxtech-ember/EmberBSD-Docs/blob/main/modules/bluetooth.md) guide first.
This index links current implementation interfaces for controller transports, kernel device adapters and userland profiles.
Headers and source own exact signatures, locking, packet layouts and errors. Hardware applicability belongs to [DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards).

## Choose the integration point

| Change | Existing owner and examples |
| --- | --- |
| USB HCI controller | [ubt](../../dev/usb/ubt.c), [ubt(4)](../../../share/man/man4/ubt.4); inspect endpoint/alternate-setting handling and lifecycle before adding device quirks |
| UART HCI controller | [H4 btuart](../../dev/bluetooth/btuart.c), [H5 bth5](../../dev/bluetooth/bth5.c), [BCSP](../../dev/bluetooth/bcsp.c); use the matching existing line discipline |
| UART initialization or firmware | [btattach type table and dispatcher](../../../usr.sbin/btattach/btattach.c), [initializers](../../../usr.sbin/btattach), [btattach(8)](../../../usr.sbin/btattach/btattach.8) |
| Vendor firmware through native HCI | [rtlbtfw](../../../usr.sbin/rtlbtfw/rtlbtfw.c), [image parser](../../../usr.sbin/rtlbtfw/rtlbt_fw.c), [rtlbtfw(8)](../../../usr.sbin/rtlbtfw/rtlbtfw.8) |
| Platform UART attachment | Native serial/bus layer; [bcm2712btcom](../../dev/ic/bcm2712btcom.c) shows one existing attachment, not a generic wiring recipe |
| Remote HID or audio device | [bthub](../../dev/bluetooth/bthub.c), [bthidev](../../dev/bluetooth/bthidev.c), [btsco](../../dev/bluetooth/btsco.c), and their existing report/audio consumers |
| Userland profile or application | [Native sockets](../bt_proto.c), [libbluetooth](../../../lib/libbluetooth), and the profile implementations below |

For H5, distinguish the source filename, configured pseudo-device and line-discipline names.
Their definitions are in [files.bluetooth](../../dev/bluetooth/files.bluetooth) and the `btattach` type table.
Keep protocol logic in the common stack; add only transport/device operations in an adapter.
Preserve firmware provenance, licenses and shared-resource ownership.

## Controller transport contract

The live contract is [`struct hci_if` in hci.h](../hci.h), implemented by [hci_unit.c](../hci_unit.c).
Use the current `_pcb` attachment names from these sources.

| Responsibility | Current interface | Required behavior |
| --- | --- | --- |
| Register/remove a controller | `hci_attach_pcb`, `hci_detach_pcb` | Maintain a valid unit and transport state across enable, disable and detach. Detach invokes common disable before freeing the unit. |
| Hardware lifecycle | `hci_if.enable`, `hci_if.disable` | Initialize the actual transport, propagate initialization failure, and stop/drain asynchronous work before its state can disappear. |
| Transmit HCI packets | `output_cmd`, `output_acl`, `output_sco` | Own the submitted mbuf and serialize transport output. Handle partial transfers and errors without replacing common command/data credit accounting. |
| Receive packets | `hci_input_event`, `hci_input_acl`, `hci_input_sco` | Deliver complete native HCI packets, including the packet-type header. Each function consumes its mbuf on both success and failure; use the boolean result for error accounting. |
| SCO completion | `hci_complete_sco` | Return completed SCO storage according to the transport's completion-context path. This consumes the mbuf; do not free it again. |
| Statistics and interrupt level | `get_stats`, `ipl` | Report actual transfer/error counters and supply the interrupt level used for the device queue lock. |

All `hci_if` callbacks above are required by attachment. This interface has no separate reset or suspend callback.
Common `hci_enable` sends HCI Reset after transport enable; device-specific hardware reset and recovery remain with the adapter.
Read the selected adapter's enable/disable and bus callbacks together; a successful attach does not prove working teardown.
H4 framing and USB packet conversion differ even though both present the same HCI packets to `netbt`.

[hci_unit.c](../hci_unit.c) owns bounded receive queues, soft-interrupt dispatch and command credits.
[hci_event.c](../hci_event.c) processes controller events; [hci_link.c](../hci_link.c) owns links, ACL fragmentation and completion.
Keep the distinction between releasing a transport buffer and receiving the controller's completion event.
Preserve `bt_lock` and the device queue lock boundaries used by the common implementation.
Do not deliver late callbacks through a freed `hci_unit`.

## Kernel protocols and device adapters

| Interface | Implementation and consumers |
| --- | --- |
| `struct btproto` callbacks | [bluetooth.h](../bluetooth.h): connection, input, completion and link-mode notifications to upper layers |
| L2CAP channels | [l2cap.h](../l2cap.h), [l2cap_upper.c](../l2cap_upper.c), [signaling](../l2cap_signal.c); used by [bthidev](../../dev/bluetooth/bthidev.c) |
| RFCOMM streams | [rfcomm.h](../rfcomm.h), [rfcomm_upper.c](../rfcomm_upper.c), [sessions](../rfcomm_session.c) and [DLC state](../rfcomm_dlc.c) |
| SCO channels | [sco.h](../sco.h), [sco_upper.c](../sco_upper.c); used by [btsco](../../dev/bluetooth/btsco.c) |
| Remote device attachment | [btdev.h](../../dev/bluetooth/btdev.h), [bthub](../../dev/bluetooth/bthub.c), [btdevctl](../../../usr.sbin/btdevctl) |
| HID reports | [bthidev.h](../../dev/bluetooth/bthidev.h), [btkbd](../../dev/bluetooth/btkbd.c), [btms](../../dev/bluetooth/btms.c); reuse the shared HID parser and `wscons` consumers |
| Kernel configuration | [files.netbt](../files.netbt), [files.bluetooth](../../dev/bluetooth/files.bluetooth), [bluetoothdevices.config](../../dev/bluetooth/bluetoothdevices.config) |

Kernel consumers use the protocol-specific `l2cap_*_pcb`, `rfcomm_*_pcb` or `sco_*_pcb` entry points.
Use their actual headers and existing consumers for ownership and lock context; there is no generic `btproto_attach` implementation here.
Keep reconnection, channel state and protocol validation with their existing owners.
Device report handling does not belong in the USB/UART controller driver.

## Profiles and native applications

[bt_proto.c](../bt_proto.c) registers HCI raw sockets, L2CAP/SCO sequential-packet sockets and RFCOMM stream sockets.
[bluetooth(4)](../../../share/man/man4/bluetooth.4) describes addressing, socket options and controller ioctls.
Use [bluetooth.h](../bluetooth.h) and the selected protocol header for current definitions.

| Consumer | Canonical source and reference |
| --- | --- |
| Controller access and requests | [libbluetooth public header](../../../lib/libbluetooth/bluetooth.h), [bt_dev implementation](../../../lib/libbluetooth/bt_dev.c), [bt_dev(3)](../../../lib/libbluetooth/bt_dev.3) |
| Service discovery and registration | [SDP API](../../../lib/libbluetooth/sdp.h), [sdp(3)](../../../lib/libbluetooth/sdp.3), [sdpquery](../../../usr.bin/sdpquery), [sdpd](../../../usr.sbin/sdpd) |
| Classic HID | [btdevctl SDP mapping](../../../usr.sbin/btdevctl/sdp.c), [bthidev(4)](../../../share/man/man4/bthidev.4) |
| SPP | [rfcomm_sppd](../../../usr.bin/rfcomm_sppd), [manual](../../../usr.bin/rfcomm_sppd/rfcomm_sppd.1) |
| PAN / BNEP | [btpand](../../../usr.sbin/btpand), [manual](../../../usr.sbin/btpand/btpand.8); `tap` connects the service to normal Ethernet configuration |
| A2DP / AVDTP / SBC | [bta2dpd](../../../usr.sbin/bta2dpd/bta2dpd), [manual](../../../usr.sbin/bta2dpd/bta2dpd/bta2dpd.8) |
| Headset control and SCO audio | [bthset](../../../usr.bin/bthset), [btsco](../../dev/bluetooth/btsco.c), [btsco(4)](../../../share/man/man4/btsco.4) |

HID here uses Classic L2CAP control/interrupt channels, not GATT.
The `HF` SDP mapping and SCO audio attachment do not implement a complete HFP service-level connection.
AVRCP and extra audio codecs are not supplied merely by A2DP transport.

## Security and startup

| Owner | Read for |
| --- | --- |
| [bthcid event handling](../../../usr.sbin/bthcid/hci.c), [key/config handling](../../../usr.sbin/bthcid/config.c), [manual](../../../usr.sbin/bthcid/bthcid.8) | Classic PIN requests, stored link-key replies and key notifications |
| [bluetooth-control](../../../ember/tools/bluetooth-control.c) | Controller identity and BR/EDR SSP/Secure Connections initialization with capability checks and readback |
| [bluetooth-pair](../../../ember/tools/bluetooth-pair.c) | Outgoing selected-peer SSP, explicit confirmation, optional Just Works/legacy/bonded modes and protected L2CAP channel checks |
| [Base rc service](../../../etc/rc.d/bluetooth), [defaults](../../../etc/defaults/rc.conf) | Serial attachment, firmware preparation, controller setup, key service and remote device attachment |
| [Platform startup integration](../../../ember/etc/rc.d/ember_bluetooth) | Its explicitly scoped attachment and security-initialization sequence, helper dependencies and restart behavior |
| [HCI socket access](../hci_socket.c), [controller ioctls](../hci_ioctl.c) | Native authorization, packet/event filters and request handling |

The pairing helper does not implement general incoming pairing, passkey entry, OOB association or LE SMP.
Its link-key notifications do not create a second persistent key store; `bthcid` retains that responsibility.
`L2CAP_LM_SECURE` and `RFCOMM_LM_SECURE` refer to the existing link-key-change mode, not a declaration of LE security support.
Service setup must select its actual required link mode.
Do not infer a successful security handshake from a controller setup command alone.

## Current extension boundary

[hci_le.h](../hci_le.h) contains LE wire definitions.
[hci_event.c](../hci_event.c), [hci_link.c](../hci_link.c), [L2CAP signaling](../l2cap_signal.c) and [socket registration](../bt_proto.c)
show the implemented Classic path. There is no integrated native LE connection/ATT/GATT/SMP/ISO stack in this reviewed implementation.
An ATT PSM constant or raw HCI command does not supply the missing state machines and application APIs.

Ports' [BluezQt](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/pkgsrc/net/kf6-bluez-qt/DESCR)
and [Bluedevil](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/pkgsrc/x11/plasma6-bluedevil/DESCR) are D-Bus clients, not native Bluetooth backends.
The [proposed architecture](https://github.com/oxtech-ember/EmberBSD-Docs/blob/main/modules/bluetooth.md#proposed-direction-for-missing-capabilities)
separates missing common mechanisms from profile work. It does not define an implemented ABI.
Keep concrete extension contracts and executable checks with the receiving source change.

## Validation entry points

- [libbluetooth tests](../../../tests/lib/libbluetooth): native address and SDP data handling.
- [H4 parser checks](../../../ember/tools/btuart-contract.py): actual receiver with allocation/delivery doubles; framing, not UART timing.
- [Platform UART checks](../../../ember/tools/bluetooth-uart-contract.py): actual attachment with controlled hardware boundaries.
- [Firmware parser checks](../../../ember/tools/rtlbtfw-contract.sh): patch-image selection and bounds without a controller.
- [Controller initialization](../../../ember/tools/bluetooth-control-contract.py) and [pairing events](../../../ember/tools/bluetooth-pair-contract.py): controlled HCI replies, peer/handle matching, malformed events and security failures.
- [Startup checks](../../../ember/tools/bluetooth-rc-contract.py): the rc script with command-boundary doubles.
- [A2DP RTP checks](../../../ember/tools/bta2dpd-rtp-contract.py): packet sequence and sample-clock progression from the encoder, without Bluetooth.
- [Native HCI recorder](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/network-diagnostics/src/hcisnoop.c): socket-tap traffic in BTSnoop format for analysis.

Use the checks' declared platform and tool inputs. Select SDK/Ports tools when compiling changed code.
These tests do not establish radio operation, profile interoperability, complete security conformance or sustained performance.
For a new transport, additionally test short transfers, malformed lengths, flow-control restart and detach with pending traffic.
Check the intended profile after pairing, then reconnect and sustained operation on the actual device.
An HCI capture observes the host/controller boundary, not the over-the-air radio exchange.
Keep keys and private traffic out of published fixtures. Physical applicability and acceptance belong to DevBoards.
