# Base libedit integration

EmberBSD-Ports owns the libedit release and patches in `pkgsrc/devel/editline`.
This directory owns the native OS build, target configuration, ABI, installed
paths and OS integration tests. It contains no second libedit implementation.

Prepare the source with EmberBSD-SDK `scripts/prepare-libedit.py`.
Pass the resulting absolute directory as `EMBER_LIBEDIT_SOURCE` to native make,
or as `-V EMBER_LIBEDIT_SOURCE=/absolute/prepared/source` to `build.sh`.
The SDK sysroot and release builders accept `--libedit-source` and validate the
source receipt against the selected clean Ports checkout.
The prepared directory contains the patched `src/`, upstream `doc/`, the exact
Ports `ports.distinfo`, and `libedit-source.json` provenance receipt.
Do not configure or edit this prepared source tree.

The native makefiles stop if the required source or metadata is absent.
They do not download sources or use a bundled or host implementation.
Kernel and host-tool bootstrap operations do not consume this source contract.
The base headers stage, libedit, sh and the tests in this directory do consume it.

The build uses EmberBSD libc and libterminfo. It does not compile the portable
libc replacements or link the Ports ncurses library into base libedit.
Quote-only private-header lookup prevents portable compatibility headers from
shadowing target libc headers. Generated editor headers use the build-host awk.
The original mdoc manual files are copied to the native manual build targets.

The installed library retains SONAME `libedit.so.3`. Minor version 2 adds the
upstream `rl_kill_full_line` symbol. `edit.expsym` checks the native exports.
Existing history, tokenizer and readline ABI remains available. Native headers
remain `/usr/include/histedit.h` and `/usr/include/readline/readline.h`, with
`history.h` as a symlink to `readline.h`. The portable package ABI and its
`include/editline` installation layout are not the base installation contract.

The build installs the exact Ports distinfo at
`/usr/share/emberbsd/ports/editline.distinfo`. Package consumers use this marker
to reject a base library from a different canonical source and patch set.
A matching marker establishes source selection. It does not replace ABI checks
or runtime acceptance.

Validate a change with the native headers, library, sh and TEST builds.
The shared-library build checks `edit.expsym` automatically.
Run `test_filecompletion` on a supported execution target. Use `wtc1`, `tc1`
and `rl1` for terminal input checks. These programs retain their upstream
licenses. Compiler, analyzer and execution checks belong to the selected target;
a successful make parse or host control does not qualify an EmberBSD binary.
