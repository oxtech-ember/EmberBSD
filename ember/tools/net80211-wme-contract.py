#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Extract complete native WME exchange consumers for a host contract."""
import pathlib
import re
import sys

src, out = map(pathlib.Path, sys.argv[1:])


def read(path):
    return (src / path).read_text()


def function(path, name, result):
    text = read(path)
    match = re.search(r"^" + name + r"\(.*?^\}$", text, re.M | re.S)
    if not match:
        raise ValueError(name)
    return result + "\n" + match.group() + "\n"


def structure(path, name):
    match = re.search(r"^struct " + name + r" \{.*?^\}[^;]*;", read(path), re.M | re.S)
    if not match:
        raise ValueError(name)
    return match.group() + "\n"


wire = "sys/net80211/ieee80211.h"
proto = "sys/net80211/ieee80211_proto.h"
ipt = "sys/net80211/ieee80211_input.c"
output = "sys/net80211/ieee80211_output.c"
node = "sys/net80211/ieee80211_node.c"
parts = [read(wire), read("sys/net80211/ieee80211_phy_ies.h")]
for path, names in ((proto, ("wmeParams", "chanAccParams", "ieee80211_wme_state")),
                    ("sys/net80211/ieee80211_ioctl.h", ("ieee80211_stats", "ieee80211_nodestats", "ieee80211req")),
                    ("sys/net80211/_ieee80211.h", ("ieee80211_rateset",))):
    parts.append(read(path)[:read(path).index("#")])
    for name in names:
        parts.append(structure(path, name))
parts.append(structure("sys/net80211/ieee80211_node.h", "ieee80211_scanparams"))
for path, pattern in (("sys/net80211/ieee80211_var.h", r"IEEE80211_[FC]_[A-Z0-9_]+"),
                      ("sys/net80211/ieee80211_node.h", r"IEEE80211_NODE_[A-Z0-9_]+")):
    for line in read(path).splitlines():
        if re.match(r"#define\s+" + pattern + r"\s+0x[0-9a-fA-F]+", line):
            parts.append(line.split("/*", 1)[0])
(out / "native-wme-types.inc").write_text("\n".join(parts))

parts = []
proto_source = read("sys/net80211/ieee80211_proto.c")
parts.append(proto_source[proto_source.index("typedef struct phyParamType"):proto_source.index("void\nieee80211_wme_initparams")])
parts.append("#undef IEEE80211_NO_HOSTAP\n#define HIGH_PRI_SWITCH_THRESH 10\nvoid ieee80211_wme_reset(struct ieee80211_node *);\n")
if "wme_sta_apply" in read(proto):
    parts.append("#define NATIVE_STA_APPLY 1\n")
for name, result in (("ieee80211_set_shortslottime", "void"),
                     ("ieee80211_wme_prepare", "static void"),
                     ("ieee80211_wme_apply_sta", "int"),
                     ("ieee80211_wme_updateparams_locked", "void"),
                     ("ieee80211_wme_updateparams", "void"),
                     ("ieee80211_wme_initparams", "void")):
    if "\n" + name + "(" in proto_source:
        parts.append(function("sys/net80211/ieee80211_proto.c", name, result))
parts.append("#define IEEE80211_NO_HOSTAP 1\n")
for path in (ipt, output, node, "sys/net80211/ieee80211_proto.c"):
    prefix = read(path).split("#include", 1)[0]
    parts.extend(re.findall(r"/\*.*?\*/", prefix, re.S))
macro = read(ipt)
parts.append(macro[macro.index("#define LE_READ_2"):macro.index("static __inline int\niswpaoui")])
for name in ("iswmeoui", "iswpaoui", "iswmeparam", "iswmeinfo"):
    parts.append(function(ipt, name, "static inline int"))
for name, result in (("ieee80211_wme_reset", "void"),):
    path = "sys/net80211/ieee80211_proto.c"
    if "\n" + name + "(" in read(path):
        parts.append(function(path, name, result))
for name, result in (("ieee80211_decode_wmeparams", "static int"), ("ieee80211_parse_wmeparams", "static int")):
    if "\n" + name + "(" not in read(ipt):
        continue
    parts.append(function(ipt, name, result))
for name, result in (("ieee80211_send_setup", "static void"),
                     ("ieee80211_mgmt_output", "static int"),
                     ("ieee80211_add_ssid", "static uint8_t *"),
                     ("ieee80211_add_rates", "static uint8_t *"),
                     ("ieee80211_add_xrates", "static uint8_t *"),
                     ("ieee80211_add_wme_info", "static uint8_t *"),
                     ("ieee80211_add_wme_param", "static uint8_t *"),
                     ("ieee80211_send_mgmt", "int"),
                     ("ieee80211_classify", "int"),
                     ("ieee80211_encap", "struct mbuf *")):
    if name == "ieee80211_add_wme_info":
        parts.append("#define WME_OUI_BYTES 0x00, 0x50, 0xf2\n")
    if name == "ieee80211_mgmt_output" and "\nieee80211_wme_request_offer(" in read(output):
        parts.append(function(output, "ieee80211_wme_request_offer", "static int"))
    parts.append(function(output, name, result))
parts.append("#define ISREASSOC(st) ((st) == IEEE80211_FC0_SUBTYPE_REASSOC_RESP)\n")
parts.append(function(ipt, "ieee80211_recv_mgmt_assoc_resp", "static void"))
parts.append(function(ipt, "ieee80211_recv_mgmt_beacon", "static void"))
for line in read("sys/net80211/ieee80211_ioctl.h").splitlines():
    if re.match(r"#define\s+IEEE80211_(IOC_WME_|WMEPARAM_)", line):
        parts.append(line)
parts.append(function("sys/net80211/ieee80211_ioctl.c", "ieee80211_ioctl_setwmeparam", "static int"))
parts.append(function(node, "copy_bss", "static inline void"))
parts.append(function(node, "ieee80211_sta_join", "int"))
parts.append(function(node, "ieee80211_sta_leave", "void"))
# This driver's narrow sequence consumer precedes unrelated DMA construction.
# Retain the complete native decision block; model only its local environment.
ralink = read("sys/dev/ic/rt2661.c")
begin = ralink.index("\ttid =", ralink.index("Packet Bursting:"))
end = ralink.index("flags |= RT2661_TX_IFS_SIFS;", begin) + len("flags |= RT2661_TX_IFS_SIFS;")
parts.extend(re.findall(r"/\*.*?\*/", ralink.split("#include", 1)[0], re.S))
for line in read("sys/dev/ic/rt2661reg.h").splitlines():
    if line.startswith("#define RT2661_TX_IFS_SIFS"):
        parts.append(line)
parts.append("static uint32_t native_rt2661_burst(struct ieee80211com *ic, struct mbuf *m0, struct ieee80211_node *ni) {\n"
             "struct ieee80211_frame *wh = mtod(m0, struct ieee80211_frame *);\n"
             "uint32_t flags = 0; int tid; (void)wh;\n" +
             ralink[begin:end] + "\nreturn flags;\n}\n")
(out / "native-wme.inc").write_text("\n".join(parts))
