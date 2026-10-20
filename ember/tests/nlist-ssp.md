# a.out nlist stack protection

`lib/libc/gen/nlist_aout.c` uses heap storage for its variable-size symbol
scoreboard when stack protection is enabled. Allocation and release recognize
GCC's `__SSP__`, `__SSP_ALL__`, and `__SSP_STRONG__` macros. The unprotected
configuration retains its stack allocation.

Run the actual-function host contract with a native GCC16 compiler:

```sh
python3 ember/tests/nlist-ssp.py --cc /path/to/gcc-16 /new/output-directory
```

The contract extracts the native function with its license notices. Controlled
I/O peers provide one symbol table, a missing symbol, and a read failure. Four
compiler modes check lookup results and allocation/release counts. A mutation
that removes only the strong-protection release guard must fail.

To check complete target translation units, record the actual libc compile
command as a JSON array of arguments. Supply it with `--target-command` and
its object-directory path with `--target-cwd`. Keep the target sysroot, source
includes, and GCC16 compiler available. The runner retains those production
flags, changes only the source/output and stack-protector mode, and adds the
original source directory for quoted includes. All four modes must compile the
current source. The pre-fix source must fail only in strong mode with the
variable-length-buffer stack-protector diagnostic. Other failures are errors.
Commands, diagnostics and the selected source hash remain in `results.json`.

Host I/O peers are not an a.out parser qualification. Target object compilation
does not establish target execution, a complete libc build, or image acceptance.
