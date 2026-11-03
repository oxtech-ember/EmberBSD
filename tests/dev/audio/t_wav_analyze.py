#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD project

import io
import math
import struct
import unittest
import wave

from wav_analyze import analyze_wav


def make_wav(samples, rate=48000):
    output = io.BytesIO()
    with wave.open(output, 'wb') as writer:
        writer.setnchannels(1)
        writer.setsampwidth(2)
        writer.setframerate(rate)
        writer.writeframes(struct.pack('<' + 'h' * len(samples), *samples))
    return output.getvalue()


class AnalyzeWaveTests(unittest.TestCase):
    def test_silence_reports_frame_timing_and_zero_signal(self):
        result = analyze_wav(io.BytesIO(make_wav([0] * 24000)))
        self.assertEqual(result['frames'], 24000)
        self.assertEqual(result['sample_rate'], 48000)
        self.assertEqual(result['duration_seconds'], 0.5)
        self.assertEqual(result['peak_abs'], 0)
        self.assertEqual(result['dc_offset'], 0.0)
        self.assertEqual(result['ac_rms'], 0.0)
        self.assertEqual(result['clipped_samples'], 0)
        self.assertEqual(result['classification'], 'silence')

    def test_constant_signal_separates_dc_from_ac(self):
        result = analyze_wav(io.BytesIO(make_wav([1234] * 1000)))
        self.assertEqual(result['peak_abs'], 1234)
        self.assertEqual(result['dc_offset'], 1234.0)
        self.assertEqual(result['ac_rms'], 0.0)
        self.assertEqual(result['classification'], 'constant')

    def test_sine_reports_expected_ac_rms_without_dc(self):
        samples = [round(12000 * math.sin(2 * math.pi * i / 48))
                   for i in range(480)]
        result = analyze_wav(io.BytesIO(make_wav(samples)))
        self.assertEqual(result['frames'], 480)
        self.assertAlmostEqual(result['dc_offset'], 0.0, delta=0.1)
        self.assertAlmostEqual(result['ac_rms'], 12000 / math.sqrt(2), delta=1.0)
        self.assertEqual(result['clipped_samples'], 0)
        self.assertEqual(result['classification'], 'signal')

    def test_full_scale_samples_are_counted_as_clipping(self):
        result = analyze_wav(io.BytesIO(make_wav([32767, -32768, 0])))
        self.assertEqual(result['clipped_samples'], 2)

    def test_rejects_truncated_pcm_payload(self):
        data = make_wav([100, -100, 200, -200])[:-2]
        with self.assertRaisesRegex(ValueError, 'truncated'):
            analyze_wav(io.BytesIO(data))

    def test_enforces_duration_limit(self):
        with self.assertRaisesRegex(ValueError, 'duration limit'):
            analyze_wav(io.BytesIO(make_wav([1] * 10, rate=10)),
                        max_seconds=0.5)


if __name__ == '__main__':
    unittest.main()
