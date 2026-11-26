#!/bin/sh
# Build the production Orange Pi Zero 4 kernel with the shared builder.
set -eu
case $# in
1) ;;
*) printf '%s\n' 'usage: build-orangepi-zero4-kernel.sh ABSOLUTE_OUTPUT | --outputs' >&2; exit 2 ;;
esac
here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
exec sh "$here/build-kernel.sh" "$1" EMBER64_ORANGEPI_ZERO4
