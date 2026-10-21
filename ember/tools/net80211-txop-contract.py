#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Extract native WME consumers without rewriting their implementations."""
import pathlib
import re
import sys

src, out = map(pathlib.Path, sys.argv[1:])


def source(path):
    return (src / path).read_text()


def notices(path):
    text = source(path)
    prefix = text.split("#include", 1)[0]
    return "\n".join(re.findall(r"/\*.*?\*/", prefix, re.S))


def function(path, name, result):
    text = source(path)
    match = re.search(r"^" + name + r"\(.*?^\}$", text, re.M | re.S)
    if not match:
        raise ValueError(name)
    return result + "\n" + match.group() + "\n"


def structure(path, name):
    text = source(path)
    match = re.search(r"^struct " + name + r" \{.*?^\}[^;]*;", text, re.M | re.S)
    if not match:
        raise ValueError(name)
    return match.group() + "\n"


proto = "sys/net80211/ieee80211_proto.h"
wire = "sys/net80211/ieee80211.h"
parts = ["/* Native fragments retain their original source notices. */\n"]
for path in (proto, wire, "sys/net80211/ieee80211_input.c",
             "sys/net80211/ieee80211_output.c", "sys/net80211/ieee80211_ioctl.c"):
    parts.append(notices(path))
for name in ("wmeParams", "chanAccParams", "ieee80211_wme_state"):
    parts.append(structure(proto, name))
for name in ("ieee80211_wme_info", "ieee80211_wme_acparams", "ieee80211_wme_param"):
    parts.append(structure(wire, name))
for line in source(wire).splitlines():
    if re.match(r"#define\s+(WME_(PARAM_|QOSINFO_|OUI_TYPE|PARAM_OUI_SUBTYPE|VERSION)|IEEE80211_ELEMID_VENDOR)", line):
        parts.append(line)
parts.append("struct ieee80211_frame { unsigned unused; };\n")
parts.append("struct ieee80211_node { uint16_t ni_flags; };\n")
parts.append("struct ieee80211com { struct ieee80211_wme_state ic_wme; unsigned ic_caps; struct ieee80211_node *ic_bss; };\n")
for line in source("sys/net80211/ieee80211_node.h").splitlines():
    if re.match(r"#define\s+IEEE80211_NODE_WME_VALID\s", line):
        parts.append(line)
parts.append(structure("sys/net80211/ieee80211_ioctl.h", "ieee80211req"))
parts.append("static void ieee80211_wme_updateparams(struct ieee80211com *ic) { (void)ic; }\n")
for line in source("sys/net80211/ieee80211_ioctl.h").splitlines():
    if re.match(r"#define\s+IEEE80211_(IOC_WME_|WMEPARAM_)", line):
        parts.append(line)
parts.append("#define WME_OUI_BYTES 0x00, 0x50, 0xf2\n")
parts.append(function("sys/net80211/ieee80211_input.c", "ieee80211_parse_wmeparams", "static int"))
parts.append(function("sys/net80211/ieee80211_output.c", "ieee80211_add_wme_param", "static u_int8_t *"))
for name in ("ieee80211_ioctl_getwmeparam", "ieee80211_ioctl_setwmeparam"):
    parts.append(function("sys/net80211/ieee80211_ioctl.c", name, "static int"))
(out / "native-txop.inc").write_text("\n".join(parts))

types = [structure(proto, name) for name in
         ("wmeParams", "chanAccParams", "ieee80211_wme_state")]
for path, name in (("sys/dev/pci/if_iwireg.h", "iwi_wme_params"),
                   ("sys/dev/pci/if_iwnreg.h", "iwn_edca_params"),
                   ("sys/dev/pci/if_wpireg.h", "wpi_wme_setup")):
    types.append(structure(path, name))
(out / "native-txop-types.inc").write_text("\n".join(types))

drivers = []
for path, pattern in (("sys/dev/ic/rt2860reg.h", r"RT2860_(EDCA_AC_CFG|WMM_.*_CFG)"),
                      ("sys/dev/ic/rt2661reg.h", r"RT2661_(AC_TXOP_CSR[01]|CWMIN_CSR|CWMAX_CSR|AIFSN_CSR)"),
                      ("sys/dev/ic/rtwnreg.h", r"R92C_EDCA_.*")):
    for line in source(path).splitlines():
        if re.match(r"#define\s+" + pattern + r"(?:\s|\()", line):
            drivers.append(line)
for path, names in (
    ("sys/dev/ic/ath.c", (("ath_txq_update", "static int"), ("ath_wme_update", "static int"))),
    ("sys/dev/ic/rt2860.c", (("rt2860_updateedca", "static int"),)),
    ("sys/dev/ic/rt2661.c", (("rt2661_wme_update", "static int"),)),
    ("sys/dev/usb/if_run.c", (("run_updateedca_cb", "static void"), ("run_updateedca", "static int"))),
    ("sys/dev/pci/if_rtwn.c", (("rtwn_wme_update", "static int"),)),
    ("sys/dev/usb/if_urtwn.c", (("urtwn_wme_update_cb", "static void"),)),
    ("sys/dev/pci/if_iwi.c", (("iwi_wme_update", "static int"),)),
    ("sys/dev/pci/if_iwn.c", (("iwn_wme_update", "static int"),)),
    ("sys/dev/pci/if_wpi.c", (("wpi_wme_update", "static int"),)),
):
    drivers.append(notices(path))
    if path.endswith("if_iwi.c"):
        text = source(path)
        start = text.index("static const struct wmeParams iwi_wme_cck_params")
        end = text.index("static int\niwi_wme_update", start)
        drivers.append(text[start:end])
    for name, result in names:
        drivers.append(function(path, name, result))
(out / "native-txop-drivers.inc").write_text("\n".join(drivers))

intel = []
for path, names in (
    ("sys/dev/pci/if_iwm.c", (("iwm_mac_ctxt_cmd_common", "static void"),
                            ("iwm_mac_ctxt_cmd", "static int"))),
    ("ember/development/iwx-be200/src/if_iwx.c", (
        ("iwx_mac_ctxt_cmd_common", "void"), ("iwx_mac_ctxt_cmd", "int"),
        ("iwx_mld_modify_link_fill", "void"), ("iwx_mld_add_sta_cmd", "int"),
        ("iwx_mld_rm_sta_cmd", "int"))),
):
    intel.append(notices(path))
    for name, result in names:
        intel.append(function(path, name, result))
(out / "native-txop-intel.inc").write_text("\n".join(intel))
(out / "native-txop-intel-types.inc").write_text(
    structure("sys/dev/pci/if_iwmreg.h", "iwm_ac_qos") +
    structure("ember/development/iwx-be200/src/if_iwxreg.h", "iwx_ac_qos"))
