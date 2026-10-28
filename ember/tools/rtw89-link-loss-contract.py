#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: RTL8852BE watchdog source extractor, AI-assisted.

import argparse
import pathlib
import subprocess


def function_body(source: str, name: str) -> str:
	marker = f"\n{name}("
	start = source.find(marker)
	if start < 0 or source.find(marker, start + len(marker)) >= 0:
		raise SystemExit(f"expected one definition of {name}")
	start += 1
	open_brace = source.find("{", start + len(name))
	if open_brace < 0:
		raise SystemExit(f"missing body for {name}")
	depth = 0
	for pos in range(open_brace, len(source)):
		if source[pos] == "{":
			depth += 1
		elif source[pos] == "}":
			depth -= 1
			if depth == 0:
				return source[start:pos + 1]
	raise SystemExit(f"unterminated body for {name}")


def main() -> int:
	parser = argparse.ArgumentParser(
		description="Compile the real rtw89 watchdog against a narrow state shim")
	parser.add_argument("--source-root", type=pathlib.Path, required=True)
	parser.add_argument("--work-dir", type=pathlib.Path, required=True)
	parser.add_argument("--cc", type=pathlib.Path, required=True,
		help="absolute path to the selected native GCC 16 compiler")
	args = parser.parse_args()

	root = args.source_root.resolve(strict=True)
	work = args.work_dir.resolve(strict=True)
	cc = args.cc.resolve(strict=True)
	if not cc.is_file() or not cc.stat().st_mode & 0o111:
		raise SystemExit(f"compiler is not executable: {cc}")
	source = (root / "sys/dev/pci/rtw89/if_rtw89.c").read_text()
	functions = {
		"rtw89_watchdog": "static void\n",
		"rtw89_beacon_observe": "static void\n",
		"rtw89_beacon_state": "static void\n",
		"rtw89_csa_scan_resume": "static void\n",
		"rtw89_csa_restart": "static int\n",
		"rtw89_csa_defer_state": "static enum ieee80211_state\n",
		"rtw89_csa_finish_init": "static void\n",
		"rtw89_nb_queues": "static void\n",
		"rtw89_sta_quiet": "static void\n",
		"rtw89_newstate": "static int\n",
		"rtw89_newstate_task": "static void\n",
	}
	beacon_source = (root / "sys/net80211/ieee80211_beacon_ies.c").read_text()
	parts = [
		"int\n" + function_body(beacon_source,
			"ieee80211_beacon_ies_parse"),
		"uint16_t\n" + function_body(beacon_source,
			"ieee80211_sta_beacon_interval"),
	]
	for name, prefix in functions.items():
		parts.append(prefix + function_body(source, name))
	include = work / "rtw89-link-loss.inc"
	include.write_text("\n\n".join(parts) + "\n")
	executable = work / "rtw89-link-loss-contract"
	subprocess.run([
		str(cc), "-std=c11", "-Wall", "-Wextra", "-Werror",
		"-Wno-unused-parameter",
		"-I", str(work),
		str(root / "ember/tools/rtw89-link-loss-contract.c"),
		"-o", str(executable),
	], check=True)
	return subprocess.run([str(executable)], check=False).returncode


if __name__ == "__main__":
	raise SystemExit(main())
