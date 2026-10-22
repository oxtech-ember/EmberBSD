# tipout signal termination

`tipout` receives `SIGHUP` and `SIGTERM` while it reads the remote side and
writes the script file. `fclose` and `exit` are not safe in that handler.
The handler only stores the request. The read loop closes the script and exits.

This host contract compiles the current `usr.bin/tip/tipout.c` with GCC 16.
It does not build or run the full `tip` utility. Set `HOST_GCC16_FLAGS` when
the host compiler needs an SDK, including `-isysroot` on macOS.

```sh
python3 ember/tests/tip-stop.py --cc /path/to/gcc-16 \
  --baseline PRE_FIX_COMMIT /absolute/new-work-directory
```

The current sources must pass. The baseline revision must compile and fail.
Three child processes cover the contract:

- `SIGTERM` arrives while remote input is blocked. The process exits.
- `SIGHUP` arrives while the script write is blocked. The script keeps the
  remote bytes, and the process exits.
- Carrier loss closes the script without a signal.

A callback records whether `fclose` or `exit` ran inside the handler. The
parent kills a child that does not exit within two seconds and reports a hang.
The script write defers `SIGHUP` and `SIGTERM` through `sigmask`.
Plain `-Werror` and ASan/UBSan both run. Work files stay outside the source tree.

Valgrind Memcheck is not a macOS ARM64 mode. Do not record this host run as a
Memcheck pass. GCC `-fanalyzer`, Cppcheck, and Clang Static Analyzer cover the
same `tipout.c` translation unit separately.
