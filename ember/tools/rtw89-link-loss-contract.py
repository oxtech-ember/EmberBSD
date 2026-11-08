#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: RTL8852BE watchdog source extractor, AI-assisted.

import argparse
import json
import os
import pathlib
import shlex
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
		description="Compile actual rtw89 state/TX paths against a narrow shim",
		epilog="TEST_RUNNER may name one executable wrapper for the test binary.")
	parser.add_argument("--source-root", type=pathlib.Path, required=True)
	parser.add_argument("--work-dir", type=pathlib.Path, required=True)
	parser.add_argument("--cc", type=pathlib.Path, required=True,
		help="absolute path to the selected native compiler")
	parser.add_argument("--cflags", default="",
		help="additional native compiler/linker flags, e.g. sanitizers")
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
		"rtw89_tx": "static int\n",
		"rtw89_stop": "static void\n",
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
	command = [
		str(cc), "-std=c11", "-Wall", "-Wextra", "-Werror",
		*shlex.split(args.cflags),
		"-I", str(work),
		str(root / "ember/tools/rtw89-link-loss-contract.c"),
		"-o", str(executable),
	]
	(work / "compile_commands.json").write_text(json.dumps([{
		"directory": str(pathlib.Path.cwd()),
		"file": str(root / "ember/tools/rtw89-link-loss-contract.c"),
		"arguments": command,
		"output": str(executable),
	}], indent=2) + "\n")
	subprocess.run(command, check=True)
	runner = os.environ.get("TEST_RUNNER")
	invocation = [runner, str(executable)] if runner else [str(executable)]
	return subprocess.run(invocation, check=False).returncode


if __name__ == "__main__":
	raise SystemExit(main())
