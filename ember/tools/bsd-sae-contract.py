#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors. AI-assisted.
"""Compile actual BSD SAE callbacks with modeled ioctl/OS boundaries."""
import argparse
from contextlib import nullcontext
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def structure(text, name):
    return re.search(r"(?m)^struct " + name + r" \{[\s\S]+?^};", text)[0]


def function(text, name):
    found = re.search(r"(?m)^static (?:int|void)[ \n]+" + name +
                      r"\([\s\S]+?^}", text)
    return found[0] if found else ""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--cflags", default=os.environ.get("CFLAGS", ""))
    parser.add_argument("--source", type=Path)
    parser.add_argument("--wpa-source", type=Path,
                        help="prepared WPA source directory for the public headers")
    parser.add_argument("--build-dir", type=Path,
                        help="preserve generated source, executables and compile_commands.json")
    parser.add_argument("--variant", choices=("sae", "no-sae", "netbsd", "hostapd", "old-abi"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    wpa_source = (args.wpa_source or root / "external/bsd/wpa/dist").resolve()
    source = (args.source or root / "external/bsd/wpa/dist/src/drivers/driver_bsd.c").read_text()
    abi = (root / "sys/net80211/ieee80211_ioctl.h").read_text()
    crypto = (root / "sys/net80211/ieee80211_crypto.h").read_text()
    definitions = "\n".join(line for line in re.findall(
        r"(?m)^#define[ \t]+IEEE80211_(?:IOC_|SAE_|STA_|CIPHER_|KEY_|KEYIX_|KEYBUF_|MICBUF_)[^\n]+", abi + crypto)
        if not line.rstrip().endswith("\\"))
    definitions += "\n" + "\n".join(structure(abi, name) for name in (
        "ieee80211req", "ieee80211req_key", "ieee80211req_del_key",
        "ieee80211req_mlme", "ieee80211req_sae", "ieee80211req_sta_mgmt"))
    # Host and target use the real driver state and WPA public parameter types.
    state = structure(source, "bsd_driver_global") + "\n" + structure(source, "bsd_driver_data")
    sae = source[source.index("#if defined(IEEE80211_IOC_SAE) && defined(CONFIG_SAE)",
                              source.index("bsd_set_mediaopt(")):
                 source.index("\nstatic int\nbsd_del_key(")]
    shared = ("bsd_del_key", "bsd_set_key", "bsd_send_mlme_param")
    names = ("wpa_driver_bsd_set_wpa_ie", "wpa_driver_bsd_set_wpa_internal",
             "wpa_driver_bsd_set_wpa", "wpa_driver_bsd_set_drop_unencrypted",
             "wpa_driver_bsd_deauthenticate", "wpa_driver_bsd_set_auth_alg",
             "bsd_set_supp_port", "wpa_driver_bsd_associate", "wpa_driver_bsd_capa",
             "wpa_driver_bsd_deinit")
    functions = sae + "\n" + "\n".join(function(source, name) for name in shared)
    functions += "\n#ifdef HOSTAPD\n" + function(source, "bsd_sta_disassoc") + "\n#endif\n"
    functions += "\n#ifndef HOSTAPD\n"
    functions += "\n#if defined(CONFIG_SAE) && defined(IEEE80211_SAE_CAP_KEY_EPOCH)\n"
    functions += function(source, "bsd_get_iface_flags") + "\n"
    functions += function(source, "bsd_sae_prepare_iface") + "\n#endif\n"
    functions += "\n".join(function(source, name) for name in names) + "\n#endif\n"
    disconnected = function(source, "bsd_sta_disconnected")
    functions += "\n#if !defined(HOSTAPD) && defined(__NetBSD__)\n" + disconnected + "\n#endif\n"
    start = source.index("\t\tcase RTM_IEEE80211_DISASSOC:")
    end = source.index("\t\tcase RTM_IEEE80211_SCAN:", start)
    functions += "\n#ifndef HOSTAPD\nstatic void route_disassoc(struct bsd_driver_data *drv) {\n"
    functions += "switch (RTM_IEEE80211_DISASSOC) {\n" + source[start:end] + "}\n}\n#endif\n"
    port = re.search(r"(?m)^\s*\.set_supp_port\s*=\s*\w+,", source)
    key = re.search(r"(?m)^\s*\.set_key\s*=\s*\w+,", source)
    action = re.search(r"(?m)^\s*\.send_action\s*=\s*bsd_sae_send_action,", source)
    disassoc = re.search(r"(?m)^\s*\.sta_disassoc\s*=\s*\w+,", source)
    fixture = (Path(__file__).with_suffix(".c")).read_text()
    fixture = fixture.replace("/* ABI */", definitions).replace("/* STATE */", state)
    fixture = fixture.replace("/* PRODUCTION */", functions)
    fixture = fixture.replace("/* PORT_CALLBACK */", port[0] if port else "")
    fixture = fixture.replace("/* KEY_CALLBACK */", key[0] if key else "")
    fixture = fixture.replace("/* ACTION_CALLBACK */", action[0] if action else "")
    fixture = fixture.replace("/* DISASSOC_CALLBACK */", disassoc[0] if disassoc else "")
    if args.build_dir:
        args.build_dir.mkdir(parents=True, exist_ok=True)
    workspace = (nullcontext(args.build_dir.resolve()) if args.build_dir else
                 tempfile.TemporaryDirectory(prefix="bsd-sae-", dir=os.environ.get("TMPDIR", "/tmp")))
    with workspace as work:
        path = Path(work) / "contract.c"
        path.write_text(source[:source.index("#include")] + fixture)
        command = shlex.split(args.cc) + ["-std=c99", "-D_DEFAULT_SOURCE", "-D_DARWIN_C_SOURCE",
            "-Wall", "-Wextra", "-Werror",
            "-I" + str(wpa_source / "src")]
        command += shlex.split(args.cflags)
        runner = shlex.split(os.environ.get("TEST_RUNNER", ""))
        entries = []
        for variant, defines in (("sae", ["-DCONFIG_SAE"]), ("no-sae", []),
                                 ("netbsd", ["-DCONFIG_SAE", "-DBSD_TEST_NETBSD"]),
                                 ("hostapd", ["-DHOSTAPD", "-DCONFIG_SAE"]),
                                 ("old-abi", ["-DCONFIG_SAE", "-DBSD_TEST_OLD_ABI"])):
            if args.variant and variant != args.variant:
                continue
            output = Path(work) / variant
            compile_command = command + defines + [str(path), "-o", str(output)]
            entries.append({"directory": str(Path(work)), "file": str(path),
                            "output": str(output), "arguments": compile_command})
        (Path(work) / "compile_commands.json").write_text(json.dumps(entries, indent=2) + "\n")
        for entry in entries:
            subprocess.run(entry["arguments"], check=True)
            subprocess.run(runner + [entry["output"]], check=True)


if __name__ == "__main__":
    main()
