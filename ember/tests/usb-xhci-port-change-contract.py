#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check that the xHCI root hub can clear each port change bit it reports.

Origin: EmberBSD (AI-assisted).  An xHCI port sends no new Port Status Change
Event while one PORTSC change bit stays set.  Thus each change bit that
UR_GET_STATUS reports must have a Clear Port Feature request that writes the
same PORTSC bit.  This is a source contract, not a hardware test.
"""
from pathlib import Path
import re
import sys

FEATURE = {
    "UPS_C_CONNECT_STATUS": "UHF_C_PORT_CONNECTION",
    "UPS_C_PORT_ENABLED": "UHF_C_PORT_ENABLE",
    "UPS_C_OVERCURRENT_INDICATOR": "UHF_C_PORT_OVER_CURRENT",
    "UPS_C_PORT_RESET": "UHF_C_PORT_RESET",
    "UPS_C_BH_PORT_RESET": "UHF_C_BH_PORT_RESET",
    "UPS_C_PORT_LINK_STATE": "UHF_C_PORT_LINK_STATE",
    "UPS_C_PORT_CONFIG_ERROR": "UHF_C_PORT_CONFIG_ERROR",
}


def require(condition, message):
    if not condition:
        raise SystemExit("usb-xhci-port-change-contract: " + message)


def main():
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 \
        else Path(__file__).resolve().parents[2]
    source = (root / "sys/dev/usb/xhci.c").read_text()
    reported = re.findall(
        r"if \(v & (XHCI_PS_[A-Z]+C)\)\s+i \|= (UPS_C_[A-Z_]+);", source)
    require(len(reported) == len(FEATURE),
            "expected %d reported change bits, found %d"
            % (len(FEATURE), len(reported)))
    start = source.index("case C(UR_CLEAR_FEATURE, UT_WRITE_CLASS_OTHER):")
    end = source.index("case C(UR_GET_DESCRIPTOR, UT_READ_CLASS_DEVICE):",
                       start)
    clear = source[start:end]
    for bit, change in reported:
        feature = FEATURE.get(change)
        require(feature is not None, "unknown change " + change)
        case = re.search(r"case %s:\s*\n((?:\s*case [A-Z_0-9]+:\s*\n)*)"
                         r"\s*(.*?);" % feature, clear)
        require(case is not None, "no clear case for " + feature)
        require(case.group(2).strip() ==
                "xhci_op_write_4(sc, port, v | %s)" % bit,
                "%s does not clear %s (%s)"
                % (feature, bit, case.group(2).strip()))
    print("usb-xhci-port-change-contract: %d change bits ok" % len(reported))


if __name__ == "__main__":
    main()
