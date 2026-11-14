#!/bin/sh
# Parse WAV headers with the production audio_wav_parse_hdr() of audioplay.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/audio-wav-header.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
common=${1:-$src/usr.bin/audio/common}
# Format tags, error codes and the on-disk header types.
awk '/^#define[[:space:]]+(WAVE_FORMAT_|AUDIO_E)/ { print; next }
/^typedef struct \{/ { block = $0 "\n"; copy = 1; next }
copy { block = block $0 "\n" }
copy && /^\}/ { if ($0 ~ /wav_audio/) printf "%s", block; copy = 0 }' \
    "$common/libaudio.h" > "$work/audio-wav-types.h"
# The chunk walker and the header parser, unchanged.
awk '/^#define[[:space:]]+RIFFNAMELEN/ { print }
/^(find_riff_chunk|audio_wav_parse_hdr)\(/ { print type; copy = 1 }
{ type = $0 }
copy { print }
copy && /^\}$/ { copy = 0 }' "$common/wav.c" > "$work/audio-wav-parse.h"
grep -q '^audio_wav_parse_hdr(' "$work/audio-wav-parse.h"
grep -q 'wav_audioheaderfmt;' "$work/audio-wav-types.h"
${CC:-cc} ${CFLAGS:-} -std=c99 -Wall -Wextra -Werror -I"$work" \
    "$src/ember/tests/audio-wav-header-fixture.c" ${LDFLAGS:-} -o "$work/check"
${TEST_RUNNER:-} "$work/check"
