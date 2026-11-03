#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD project

"""Bounded PCM16 mono WAV signal summary for host-side audio checks."""

import argparse
import json
import math
import struct
import sys
import wave


MAX_DURATION_SECONDS = 60.0
MAX_FRAMES = 60 * 192000
PCM16_BYTES = 2


def analyze_wav(source, max_seconds=MAX_DURATION_SECONDS):
    """Return sample and timing metrics for a PCM16 mono WAV stream."""
    if not math.isfinite(max_seconds) or max_seconds <= 0 or \
            max_seconds > MAX_DURATION_SECONDS:
        raise ValueError('duration limit must be greater than zero and at most '
                         f'{MAX_DURATION_SECONDS:g} seconds')

    try:
        reader = wave.open(source, 'rb')
        with reader:
            channels = reader.getnchannels()
            width = reader.getsampwidth()
            sample_rate = reader.getframerate()
            frames = reader.getnframes()
            compression = reader.getcomptype()
            if channels != 1 or width != PCM16_BYTES or compression != 'NONE':
                raise ValueError('input must be uncompressed PCM16 mono WAV')
            if sample_rate <= 0 or frames <= 0:
                raise ValueError('WAV must contain frames at a positive sample rate')
            if frames > MAX_FRAMES:
                raise ValueError(f'frame limit exceeded ({MAX_FRAMES})')
            duration = frames / sample_rate
            if duration > max_seconds:
                raise ValueError(f'duration limit exceeded ({max_seconds:g} seconds)')

            pcm = reader.readframes(frames)
            expected_bytes = frames * PCM16_BYTES
            if len(pcm) != expected_bytes:
                raise ValueError(f'truncated PCM payload: expected {expected_bytes} '
                                 f'bytes, received {len(pcm)}')
    except (wave.Error, EOFError, OSError, struct.error) as exc:
        raise ValueError(f'invalid or truncated WAV: {exc}') from exc

    sample_sum = 0
    square_sum = 0
    peak = 0
    clipped = 0
    low = 32767
    high = -32768
    for (sample,) in struct.iter_unpack('<h', pcm):
        sample_sum += sample
        square_sum += sample * sample
        peak = max(peak, abs(sample))
        low = min(low, sample)
        high = max(high, sample)
        if sample == -32768 or sample == 32767:
            clipped += 1

    mean = sample_sum / frames
    ac_variance = max(0.0, square_sum / frames - mean * mean)
    if peak == 0:
        classification = 'silence'
    elif low == high:
        classification = 'constant'
    else:
        classification = 'signal'

    return {
        'frames': frames,
        'sample_rate': sample_rate,
        'duration_seconds': duration,
        'peak_abs': peak,
        'dc_offset': mean,
        'ac_rms': math.sqrt(ac_variance),
        'clipped_samples': clipped,
        'clipped_fraction': clipped / frames,
        'classification': classification,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--max-seconds', type=float, default=MAX_DURATION_SECONDS,
                        help='duration ceiling (maximum 60 seconds)')
    parser.add_argument('wav_file', help='PCM16 mono WAV recording')
    args = parser.parse_args(argv)
    try:
        with open(args.wav_file, 'rb') as source:
            result = analyze_wav(source, max_seconds=args.max_seconds)
    except (OSError, ValueError) as exc:
        print(f'{args.wav_file}: {exc}', file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == '__main__':
    sys.exit(main())
