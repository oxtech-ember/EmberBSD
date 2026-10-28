#!/bin/sh
# Check the generated amd64 kernel flags and Clang's floating-point output.
set -eu
[ "$#" -eq 4 ] || { echo "usage: $0 NBMAKE KERNEL_OBJDIR CLANG LLVM_OBJDUMP" >&2; exit 2; }
make=$1 obj=$2 clang=$3 objdump=$4
flags=$($make -C "$obj" EMBER_COMPILER_FAMILY=clang -V '${COPTS.amdgpu_float}')
gccflags=$($make -C "$obj" EMBER_COMPILER_FAMILY=gcc HAVE_GCC=16 -V '${COPTS.amdgpu_float}')
case " $flags " in *' -mno-soft-float '*) ;; *) exit 1;; esac
case " $gccflags " in *' -mhard-float '*) ;; *) exit 1;; esac
work=$(mktemp -d "${TMPDIR:-/tmp}/amdgpu-float.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/float.c" <<'SRC'
double amdgpu_float_probe(double a, double b) { return a * b + a; }
SRC
# Kernel baseline flags precede the file-specific flags, as in the real build.
# Flags are selected by the generated makefile, not repeated in this test.
# shellcheck disable=SC2086
"$clang" --target=x86_64--netbsd --sysroot="$work" -nostdinc -ffreestanding -mcmodel=kernel -mno-sse -msoft-float $flags -O2 -c "$work/float.c" -o "$work/float.o"
"$objdump" -d "$work/float.o" > "$work/disassembly"
grep -Eq '[[:space:]]mulsd[[:space:]]' "$work/disassembly"
grep -Eq '[[:space:]]addsd[[:space:]]' "$work/disassembly"
if "$clang" --target=x86_64--netbsd --sysroot="$work" -nostdinc -ffreestanding -mcmodel=kernel -mno-sse -msoft-float -O2 -c "$work/float.c" -o "$work/negative.o" >"$work/negative.log" 2>&1; then
    "$objdump" -d "$work/negative.o" > "$work/negative.disassembly"
    if grep -Eq '[[:space:]](mulsd|addsd)[[:space:]]' "$work/negative.disassembly"; then
        echo 'negative control unexpectedly used SSE arithmetic' >&2
        exit 1
    fi
fi
if "$objdump" -t "$work/float.o" | grep -Eq '__((add|mul|sub|div)(sf|df)3|float|fix)'; then
    echo 'unexpected floating-point helper' >&2
    exit 1
fi
