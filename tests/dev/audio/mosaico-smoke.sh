#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD project
#
# Bounded userland smoke test for a physical audio(4) device.

set -eu

usage()
{
	echo "usage: $0 [seconds [recording.wav]]" >&2
	exit 2
}

[ "$#" -le 2 ] || usage
seconds=${1:-3}
recording=${2:-/tmp/emberbsd-audio-smoke-$$.wav}
case "$seconds" in
''|*[!0-9]*) usage ;;
esac
[ "$seconds" -ge 1 ] && [ "$seconds" -le 10 ] || usage

audio_device=${AUDIODEVICE:-/dev/audio}
control_device=${AUDIOCTLDEVICE:-/dev/audioctl}
mixer_device=${MIXERDEVICE:-/dev/mixer}
[ ! -e "$recording" ] || {
	echo "refusing to overwrite recording: $recording" >&2
	exit 1
}

for command in audioctl mixerctl audiorecord audioplay timeout wc; do
	command -v "$command" >/dev/null 2>&1 || {
		echo "missing required command: $command" >&2
		exit 1
	}
done

echo "Audio control state ($control_device):"
audioctl -d "$control_device" -a
echo "Mixer controls ($mixer_device):"
mixerctl -d "$mixer_device" -a

echo "Recording up to $seconds seconds to $recording"
timeout -s INT -k 2s "$((seconds + 2))s" audiorecord \
	-d "$audio_device" -t "0:00:$seconds" -F wav \
	-c 1 -e slinear_le -P 16 -s 48000 "$recording"
[ -s "$recording" ] || {
	echo "recording is empty: $recording" >&2
	exit 1
}
requested_pcm_bytes=$((seconds * 48000 * 2))
actual_file_bytes=$(wc -c < "$recording")
actual_file_bytes=$((actual_file_bytes + 0))
minimum_file_bytes=$requested_pcm_bytes
echo "Requested PCM bytes: $requested_pcm_bytes; captured WAV bytes: $actual_file_bytes"
[ "$actual_file_bytes" -ge "$minimum_file_bytes" ] || {
	echo "WAV file is shorter than the requested PCM payload" >&2
	exit 1
}

echo "Playing the recorded WAV through $audio_device"
timeout -k 2s "$((seconds + 2))s" audioplay -d "$audio_device" "$recording"

echo "Audio device I/O commands completed; this does not verify audible output or recording signal quality."
