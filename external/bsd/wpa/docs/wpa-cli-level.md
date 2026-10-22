# Interactive monitor log level

`wpa_cli` uses separate command and event sockets in interactive mode.
The event socket has an `ATTACH` subscription.
`LEVEL` must use that socket.
The previous command used the command socket and received `FAIL`.
The fix selects `mon_conn` when it is present.
It preserves the supplied `ctrl` socket when no monitor exists.

## Host regression

The generator uses Python with the standard library.
It extracts five real WPA functions from the pinned source files.
It retains their BSD notices.
It creates the current fix and the previous causal variant.
The C fixture models two control endpoints and the attachment rule.
A protocol `FAIL` can have a successful transport return value.
The checks inspect both the selected endpoint and the protocol reply.

Run from the source root with a native GCC16 compiler:

```sh
python3 ember/tools/wpa-cli-level-contract.py --source-root . --output /path/to/host-test
HOST_CC=/path/to/gcc-16
"$HOST_CC" -std=c11 -O2 -Wall -Wextra -Werror /path/to/host-test/original.c -o /path/to/host-test/original
"$HOST_CC" -std=c11 -O2 -Wall -Wextra -Werror /path/to/host-test/patched.c -o /path/to/host-test/patched
/path/to/host-test/original
/path/to/host-test/patched
```

The original variant must report failures and return 1.
The patched variant must pass all 15 checks and return 0.
The output directory must not exist before generation.
The source receipt records the input and projected function hashes.

The fixture covers a per-interface client.
Its `ifname_prefix` is constant `NULL`.
Global `IFNAME` expansion is outside this check.
The test uses no real socket, supplicant, firmware or network.
A physical client check is separate: `level 2` must return `OK`.
A successful level command does not prove an IGTK installation or PMF replay rejection.
