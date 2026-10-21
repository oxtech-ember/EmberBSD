#!/bin/sh
# Origin: EmberBSD actual-source TFU ownership contract, 2026-10-11.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
exec "${RUBY:-ruby}" "$(dirname -- "$0")/bcm2712-v3d-job-contract.rb" "$@"
