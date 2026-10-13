#!/bin/sh
# Check real tools/Makefile selection with a configured AArch64 make wrapper.
set -eu
[ "$#" = 2 ] || { echo "Usage: $0 NBMAKE_WRAPPER FULL_SOURCE" >&2; exit 2; }
make=$1 src=$2
bits=$("$make" -C "$src/tools" TOOLCHAIN_MISSING=yes \
    EXTERNAL_TOOLCHAIN=/external/gcc16 MKCROSSGDB=no -V '${TOOLCHAIN_BITS}')
case " $bits " in *' gmake .WAIT binutils .WAIT dbsym mdsetimage '*) ;; *)
    echo "Missing ordered BFD dependency: $bits" >&2; exit 1 ;;
esac
for tool in gcc gmp mpfr mpc isl gdb; do
    case " $bits " in *" $tool "*) echo "Unexpected bootstrap tool: $tool" >&2; exit 1 ;; esac
done
program=$("$make" -C "$src/tools/binutils" -V '${MAKE_PROGRAM}')
case "$program" in */bin/nbgmake) ;; *) echo "Binutils needs host GNU make: $program" >&2; exit 1 ;; esac
echo 'PASS: external compiler keeps host BFD dependencies without bootstrap GCC/math tools'
