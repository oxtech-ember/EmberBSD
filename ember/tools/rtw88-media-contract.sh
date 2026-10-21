#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
[ "$#" -eq 0 ] || exit 2
case ${RTW88_MEDIA_ANALYZE:-0} in 0|1) ;; *) exit 2 ;; esac
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
exec python3 "$(dirname -- "$0")/rtw88-media-contract.py"
